#include "erl_drv_nif.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/packet.h"
#include <erl_nif.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <stddef.h>
#include <string.h>

// Arbitrary choice.
// #define AV_BUF_SIZE 48000
#define AV_BUF_SIZE 6724936

// TODO instead of using a super large file here
// #define IO_BUF_SIZE 709944912
// #define IO_BUF_SIZE 6724936
// #define IO_BUF_SIZE 28 + 584

ErlNifResourceType *CTX_RES_TYPE;

typedef struct {
  void *ptr;
  // The total size of ptr
  int size;
  // Where the next bytes should be written at.
  int buf_end;

  // position of the last read.
  int pos;
} Ioq;

int queue_is_filled(Ioq *q) { return q->buf_end == q->size; }

void queue_copy(Ioq *q, void *src, int size) {
  // Do we have enough space for the data? If not, reallocate some space.
  memcpy(q->ptr + q->buf_end, src, size);
  q->buf_end += size;
}

void queue_grow(Ioq *q, int factor) {}

typedef struct {
  // Used to write binary data coming from membrane and as source for the
  // AVFormatContext.
  Ioq *queue;
  // The context responsible for reading data from the queue. It is
  // configured to use the read_packet function as source.
  AVIOContext *io_ctx;
  // The actual libAV demuxer.
  AVFormatContext *fmt_ctx;

  int has_header;
} Ctx;

void free_ctx_res(ErlNifEnv *env, void *res) {
  Ctx **ctx = (Ctx **)res;
  free((*ctx)->queue->ptr);
  avio_context_free(&(*ctx)->io_ctx);
  avformat_close_input(&(*ctx)->fmt_ctx);
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

int read_ioq(void *opaque, uint8_t *buf, int buf_size) {
  Ctx *ctx;
  int size, avail;

  ctx = (Ctx *)opaque;
  avail = ctx->queue->buf_end - ctx->queue->pos;
  if (avail <= 0)
    return AVERROR_EOF;

  size = buf_size > avail ? avail : buf_size;

  memcpy(buf, ctx->queue->ptr + ctx->queue->pos, size);
  ctx->queue->pos += size;

  if (ctx->has_header) {
    // TODO: the data needs to be shifted to the start of
    // the list, deleting the old entries.
  }

  return size;
}

int read_header(Ctx *ctx) {
  AVIOContext *io_ctx;
  AVFormatContext *fmt_ctx;
  void *io_buffer;
  int errnum;
  int ret;

  // TODO can we avoid allocating this buffer each time we do a read attempt?
  io_buffer = av_malloc(AV_BUF_SIZE);
  // Context that reads from queue and uses io_buffer as scratch space.
  io_ctx =
      avio_alloc_context(io_buffer, AV_BUF_SIZE, 0, ctx, &read_ioq, NULL, NULL);
  io_ctx->seekable = 0;

  fmt_ctx = avformat_alloc_context();
  fmt_ctx->pb = io_ctx;
  fmt_ctx->probesize = ctx->queue->size;

  if ((errnum = avformat_open_input(&fmt_ctx, NULL, NULL, NULL)))
    goto open_error;

  if ((errnum = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
    goto open_error;

  ctx->has_header = 1;
  ctx->io_ctx = io_ctx;
  ctx->fmt_ctx = fmt_ctx;
  return errnum;

open_error:
  avio_context_free(&io_ctx);
  avformat_close_input(&fmt_ctx);
  queue_grow(ctx->queue, 2);

  return errnum;
}

ERL_NIF_TERM alloc_context(ErlNifEnv *env, int argc,
                           const ERL_NIF_TERM argv[]) {
  Ioq *queue = (Ioq *)malloc(sizeof(Ioq));
  queue->ptr = malloc(AV_BUF_SIZE);
  queue->size = AV_BUF_SIZE;
  queue->pos = 0;
  queue->buf_end = 0;

  Ctx *ctx = (Ctx *)malloc(sizeof(Ctx));
  ctx->queue = queue;
  ctx->has_header = 0;

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

  // Copy the data in the buffer.
  queue_copy(ctx->queue, binary.data, binary.size);

  // TODO: release the binary? Do we have to?

  // Make an attemp reading the header only when the ioq buffer is filled.
  if (!ctx->has_header && queue_is_filled(ctx->queue))
    read_header(ctx);

  return enif_make_atom(env, "ok");
}

ERL_NIF_TERM is_ready(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  Ctx *ctx;
  get_ctx(env, argv[0], &ctx);

  return ctx->has_header ? enif_make_atom(env, "true")
                         : enif_make_atom(env, "false");
}

// ERL_NIF_TERM read_packet(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
// {
//   Ctx *ctx;
//   AVPacket *packet;
//   int errnum;

//   get_ctx(env, argv[0], &ctx);
//   errnum = av_read_frame(ctx->fmt_ctx, packet);
// }

ERL_NIF_TERM streams(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  Ctx *ctx;
  ERL_NIF_TERM *codecs;
  int errnum;
  char err[256];

  get_ctx(env, argv[0], &ctx);

  // Called on EOS: we're not ready, meaning that we did not get
  // the amount of data we wanted, but we may still be able to
  // obtain the streams.
  if (!ctx->has_header) {
    if ((errnum = read_header(ctx))) {
      av_strerror(errnum, err, sizeof(err));
      return enif_make_tuple2(env, enif_make_atom(env, "error"),
                              enif_make_string(env, err, ERL_NIF_UTF8));
    }
  }

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
}

ERL_NIF_TERM demand(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  Ctx *ctx;
  get_ctx(env, argv[0], &ctx);

  return enif_make_int(env, ctx->queue->size - ctx->queue->buf_end);
}

static ErlNifFunc nif_funcs[] = {
    // {erl_function_name, erl_function_arity, c_function}
    {"alloc_context", 0, alloc_context},
    {"add_data", 2, add_data},
    {"is_ready", 1, is_ready},
    {"demand", 1, demand},
    {"streams", 1, streams},
};

ERL_NIF_INIT(Elixir.Membrane.LibAV.Demuxer.Nif, nif_funcs, load, NULL, NULL,
             NULL)
