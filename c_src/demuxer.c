#include "erl_drv_nif.h"
#include "libavcodec/codec_id.h"
#include <erl_nif.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <stddef.h>
#include <string.h>

// Arbitrary choice.
#define AV_BUF_SIZE 48000

// TODO instead of using a super large file here
#define IO_BUF_SIZE 709944912
// #define IO_BUF_SIZE 6724936
// #define IO_BUF_SIZE 28 + 584

ErlNifResourceType *CTX_RES_TYPE;

typedef struct {
  // Used to write binary data coming from membrane and as source for the
  // AVFormatContext.
  ErlNifIOQueue *queue;
  // The context responsible for reading data from the queue. It is
  // configured to use the read_packet function as source.
  AVIOContext *io_ctx;
  // The actual libAV demuxer.
  AVFormatContext *fmt_ctx;

  int size;
  int streams_detected;
} Ctx;

int read_packet(void *opaque, uint8_t *buf, int buf_size) {
  ErlNifIOQueue *queue;
  SysIOVec *vec;
  int size;
  int nb_elem;

  queue = (ErlNifIOQueue *)opaque;
  size = enif_ioq_size(queue);

  vec = enif_ioq_peek(queue, &nb_elem);
  if (!nb_elem)
    return AVERROR_EOF;

  size = buf_size > vec->iov_len ? vec->iov_len : buf_size;

  memcpy(buf, vec->iov_base, size);
  // Remove the data from the queue once read.
  enif_ioq_deq(queue, size, NULL);

  return size;
}

void free_ctx_res(ErlNifEnv *env, void *res) {
  Ctx **ctx = (Ctx **)res;
  enif_ioq_destroy((*ctx)->queue);
  avio_context_free(&(*ctx)->io_ctx);
  avformat_free_context((*ctx)->fmt_ctx);
  free(*ctx);
}

void get_ctx(ErlNifEnv *env, ERL_NIF_TERM term, Ctx **ctx) {
  Ctx **ctx_res;
  enif_get_resource(env, term, CTX_RES_TYPE, (void *)&ctx_res);
  *ctx = *ctx_res;
}

// Called when the nif is loaded, as specified in the ERL_NIF_INIT call.
int load(ErlNifEnv *env, void **priv_data, ERL_NIF_TERM load_info) {
  int flags = ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER;
  CTX_RES_TYPE =
      enif_open_resource_type(env, NULL, "db", free_ctx_res, flags, NULL);
  return 0;
}

ERL_NIF_TERM alloc_context(ErlNifEnv *env, int argc,
                           const ERL_NIF_TERM argv[]) {
  ErlNifIOQueue *queue = enif_ioq_create(ERL_NIF_IOQ_NORMAL);
  // Freed by io_ctx.
  void *io_buffer = av_malloc(AV_BUF_SIZE);
  // Context that reads from queue and uses io_buffer as scratch space.
  AVIOContext *io_ctx = avio_alloc_context(io_buffer, AV_BUF_SIZE, 0, queue,
                                           &read_packet, NULL, NULL);
  AVFormatContext *fmt_ctx = avformat_alloc_context();
  fmt_ctx->pb = io_ctx;

  Ctx *ctx = (Ctx *)malloc(sizeof(Ctx));
  ctx->queue = queue;
  ctx->io_ctx = io_ctx;
  ctx->fmt_ctx = fmt_ctx;
  ctx->size = AV_BUF_SIZE;
  ctx->streams_detected = 0;

  // Make the resource take ownership on the context.
  Ctx **ctx_res = enif_alloc_resource(CTX_RES_TYPE, sizeof(Ctx *));
  *ctx_res = ctx;

  ERL_NIF_TERM term = enif_make_resource(env, ctx_res);

  // This is done to allow the erlang garbage collector to take care
  // of freeing this resource when needed.
  enif_release_resource(ctx_res);

  return term;
}

ERL_NIF_TERM add_data(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  Ctx *ctx;
  ErlNifBinary binary;

  get_ctx(env, argv[0], &ctx);

  enif_inspect_binary(env, argv[1], &binary);
  // data is owned by the ioq from this point on.
  enif_ioq_enq_binary(ctx->queue, &binary, 0);

  return enif_make_atom(env, "ok");
}

ERL_NIF_TERM is_ready(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  Ctx *ctx;
  get_ctx(env, argv[0], &ctx);

  return (enif_ioq_size(ctx->queue) >= IO_BUF_SIZE)
             ? enif_make_atom(env, "true")
             : enif_make_atom(env, "false");
}

ERL_NIF_TERM demand(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  Ctx *ctx;
  get_ctx(env, argv[0], &ctx);

  return enif_make_int(env, IO_BUF_SIZE - enif_ioq_size(ctx->queue));
}

ERL_NIF_TERM detect_streams(ErlNifEnv *env, int argc,
                            const ERL_NIF_TERM argv[]) {
  int errnum;
  char err[256];
  Ctx *ctx;
  ERL_NIF_TERM *codecs;

  get_ctx(env, argv[0], &ctx);

  // const AVInputFormat *input_fmt = NULL;
  // av_probe_input_buffer2(ctx->io_ctx, &input_fmt, NULL, NULL, 0,
  // IO_BUF_SIZE); input_fmt->read_header(ctx->fmt_ctx);

  // This call succeeds when it is able to read the full header.
  errnum = avformat_open_input(&ctx->fmt_ctx, NULL, NULL, NULL);
  if (errnum != 0) {
    goto open_input_err;
  }

  avformat_find_stream_info(ctx->fmt_ctx, NULL);

  codecs = calloc(ctx->fmt_ctx->nb_streams, sizeof(ERL_NIF_TERM));
  for (int i = 0; i < ctx->fmt_ctx->nb_streams; i++) {
    ERL_NIF_TERM codec_term;
    ErlNifBinary *binary;
    AVStream *av_stream;
    const char *codec_name;

    // TODO: wrap the codec in a resource, it will be used later.

    av_stream = ctx->fmt_ctx->streams[i];
    codec_name = avcodec_get_name(av_stream->codecpar->codec_id);

    codecs[i] =
        enif_make_tuple2(env, enif_make_string(env, codec_name, ERL_NIF_UTF8),
                         enif_make_int(env, av_stream->index));
  }

  return enif_make_tuple2(
      env, enif_make_atom(env, "ok"),
      enif_make_list_from_array(env, codecs, ctx->fmt_ctx->nb_streams));

open_input_err:
  av_strerror(errnum, err, sizeof(err));
  return enif_make_tuple2(env, enif_make_atom(env, "error"),
                          enif_make_string(env, err, ERL_NIF_UTF8));
}

static ErlNifFunc nif_funcs[] = {
    // {erl_function_name, erl_function_arity, c_function}
    {"alloc_context", 0, alloc_context},
    {"add_data", 2, add_data},
    {"is_ready", 1, is_ready},
    {"demand", 1, demand},
    {"detect_streams", 1, detect_streams},
};

ERL_NIF_INIT(Elixir.Membrane.LibAV.Demuxer.Nif, nif_funcs, load, NULL, NULL,
             NULL)
