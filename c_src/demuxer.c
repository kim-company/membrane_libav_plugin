#include "erl_drv_nif.h"
#include <erl_nif.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <stddef.h>
#include <string.h>

// Arbitrary choice.
#define IO_BUF_SIZE 48000

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
} Ctx;

int read_packet(void *opaque, uint8_t *buf, int buf_size) {
  ErlNifIOQueue *queue;
  SysIOVec *vec;
  int size;

  queue = (ErlNifIOQueue *)opaque;
  size = enif_ioq_size(queue);

  // Take the minimum value, we cannot extract more bytes from the queue than
  // the available ones.
  size = buf_size > size ? size : buf_size;
  vec = enif_ioq_peek(queue, &size);

  memcpy(buf, vec->iov_base, vec->iov_len);

  // TODO: dequeue the data that was read.
  // enif_ioq_deq(queue, size, NULL);

  return vec->iov_len;
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
  // Freed by io_ctx.
  void *io_buffer = av_malloc(IO_BUF_SIZE);
  ErlNifIOQueue *queue = enif_ioq_create(ERL_NIF_IOQ_NORMAL);

  // Context that reads from queue and uses io_buffer as scratch space.
  AVIOContext *io_ctx = avio_alloc_context(io_buffer, IO_BUF_SIZE, 0, queue,
                                           read_packet, NULL, NULL);

  AVFormatContext *fmt_ctx = avformat_alloc_context();
  fmt_ctx->pb = io_ctx;

  Ctx *ctx = (Ctx *)malloc(sizeof(Ctx));
  ctx->queue = queue;
  ctx->io_ctx = io_ctx;
  ctx->fmt_ctx = fmt_ctx;

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

  // In theory we do not have to return the ctx as we're mutating that directly.
  return enif_make_atom(env, "ok");
}

ERL_NIF_TERM detect_streams(ErlNifEnv *env, int argc,
                            const ERL_NIF_TERM argv[]) {
  Ctx *ctx;

  get_ctx(env, argv[0], &ctx);

  // TODO open input should not be called more than once.
  avformat_open_input(&ctx->fmt_ctx, "", NULL, NULL);
  avformat_find_stream_info(ctx->fmt_ctx, NULL);

  // TODO: create an array of available streams and send it back.

  return enif_make_atom(env, "ok");
}

// Let's define the array of ErlNifFunc beforehand:
static ErlNifFunc nif_funcs[] = {
    // {erl_function_name, erl_function_arity, c_function}
    {"alloc_context", 0, alloc_context},
    {"add_data", 2, add_data},
    {"detect_streams", 1, detect_streams},
};

ERL_NIF_INIT(Elixir.Membrane.LibAV.Demuxer.Nif, nif_funcs, load, NULL, NULL,
             NULL)
