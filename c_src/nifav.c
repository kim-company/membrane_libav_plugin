#include "erl_drv_nif.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/codec_par.h"
#include "libavcodec/packet.h"
#include <erl_nif.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

// Arbitrary choice.
#define IO_BUF_SIZE 48000

ErlNifResourceType *CTX_RES_TYPE;
ErlNifResourceType *CODEC_PARAMS_RES_TYPE;

typedef enum { QUEUE_MODE_SHIFT, QUEUE_MODE_GROW } QUEUE_MODE;

typedef struct {
  void *ptr;
  // The total size of ptr
  u_long size;
  // Where the next bytes should be written at.
  u_long buf_end;

  // position of the last read.
  u_long pos;

  // Used to differentiate wether the queue is removing
  // the bytes each time they're read or it is growing to
  // accomodate more. The latter is used when probing the
  // input to find the header.
  QUEUE_MODE mode;
} Ioq;

int queue_is_filled(Ioq *q) { return q->buf_end == q->size; }
int queue_freespace(Ioq *q) { return q->size - q->buf_end; }

void queue_grow(Ioq *q, int factor) {
  u_long new_size;

  new_size = q->size * factor;
  q->ptr = realloc(q->ptr, new_size);
  q->size = new_size;
}

void queue_copy(Ioq *q, void *src, int size) {
  // Do we have enough space for the data? If not, reallocate some space.
  if (queue_freespace(q) < size)
    queue_grow(q, 2);

  memcpy(q->ptr + q->buf_end, src, size);
  q->buf_end += size;
}

void queue_deq(Ioq *q) {
  int unread;

  unread = q->buf_end - q->pos;
  if (unread == 0) {
    q->pos = 0;
    q->buf_end = 0;
  } else {
    memmove(q->ptr, q->ptr + q->pos, unread);
    q->pos = 0;
    q->buf_end = unread;
  }
}

int queue_read(Ioq *q, void *dst, int buf_size) {
  int unread;
  int size;

  unread = q->buf_end - q->pos;
  if (unread <= 0)
    return AVERROR_EOF;

  size = buf_size > unread ? unread : buf_size;
  memcpy(dst, q->ptr + q->pos, size);
  q->pos += size;

  if (q->mode == QUEUE_MODE_SHIFT)
    queue_deq(q);

  return size;
}

typedef enum { CTX_MODE_DRAIN, CTX_MODE_BUF } CTX_MODE;

typedef struct {
  // Used to write binary data coming from membrane and as source for the
  // AVFormatContext.
  Ioq *queue;
  // The context responsible for reading data from the queue. It is
  // configured to use the read_packet function as source.
  AVIOContext *io_ctx;
  // The actual libAV demuxer.
  AVFormatContext *fmt_ctx;

  CTX_MODE mode;

  int has_header;
} DemuxerContext;

void free_ctx_res(ErlNifEnv *env, void *res) {
  DemuxerContext **ctx = (DemuxerContext **)res;
  free((*ctx)->queue->ptr);
  avio_context_free(&(*ctx)->io_ctx);
  avformat_close_input(&(*ctx)->fmt_ctx);
  free(*ctx);
}

void free_codec_params_res(ErlNifEnv *env, void *res) {
  AVCodecParameters **params = (AVCodecParameters **)res;
  avcodec_parameters_free(params);
}

void demuxer_get_ctx(ErlNifEnv *env, ERL_NIF_TERM term, DemuxerContext **ctx) {
  DemuxerContext **ctx_res;
  enif_get_resource(env, term, CTX_RES_TYPE, (void *)&ctx_res);
  *ctx = *ctx_res;
}

int read_ioq(void *opaque, uint8_t *buf, int buf_size) {
  return queue_read((Ioq *)opaque, buf, buf_size);
}

int demuxer_read_header(DemuxerContext *ctx) {
  AVIOContext *io_ctx;
  AVFormatContext *fmt_ctx;
  void *io_buffer;
  int errnum;
  int ret;

  // TODO can we avoid allocating this buffer each time we do a read attempt?
  io_buffer = av_malloc(ctx->queue->size);
  // Context that reads from queue and uses io_buffer as scratch space.
  io_ctx = avio_alloc_context(io_buffer, ctx->queue->size, 0, ctx->queue,
                              &read_ioq, NULL, NULL);
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

  // From now on, the queue will not grow but rather override data
  // read by the io_ctx. Dequeue every information read by the
  queue_deq(ctx->queue);
  ctx->queue->mode = QUEUE_MODE_SHIFT;

  return errnum;

open_error:
  avio_context_free(&io_ctx);
  avformat_close_input(&fmt_ctx);

  queue_grow(ctx->queue, 2);
  ctx->queue->pos = 0;

  return errnum;
}

ERL_NIF_TERM demuxer_alloc_context(ErlNifEnv *env, int argc,
                                   const ERL_NIF_TERM argv[]) {
  Ioq *queue = (Ioq *)malloc(sizeof(Ioq));
  queue->ptr = malloc(IO_BUF_SIZE);
  queue->mode = QUEUE_MODE_GROW;
  queue->size = IO_BUF_SIZE;
  queue->pos = 0;
  queue->buf_end = 0;

  DemuxerContext *ctx = (DemuxerContext *)malloc(sizeof(DemuxerContext));
  ctx->queue = queue;
  ctx->mode = CTX_MODE_BUF;
  ctx->has_header = 0;

  // Make the resource take ownership on the context.
  DemuxerContext **ctx_res =
      enif_alloc_resource(CTX_RES_TYPE, sizeof(DemuxerContext *));
  *ctx_res = ctx;

  ERL_NIF_TERM term = enif_make_resource(env, ctx_res);

  // This is done to allow the erlang garbage collector to take care
  // of freeing this resource when needed.
  enif_release_resource(ctx_res);

  return term;
}

ERL_NIF_TERM demuxer_add_data(ErlNifEnv *env, int argc,
                              const ERL_NIF_TERM argv[]) {
  DemuxerContext *ctx;
  ErlNifBinary binary;

  demuxer_get_ctx(env, argv[0], &ctx);
  enif_inspect_binary(env, argv[1], &binary);

  // Indicates EOS.
  if (!binary.data) {
    ctx->mode = CTX_MODE_DRAIN;
    return enif_make_atom(env, "ok");
  }

  // Copy the data in the buffer.
  queue_copy(ctx->queue, binary.data, binary.size);

  // TODO: release the binary? Do we have to?

  // Make an attemp reading the header only when the ioq buffer is filled.
  if (!ctx->has_header && queue_is_filled(ctx->queue))
    demuxer_read_header(ctx);

  return enif_make_atom(env, "ok");
}

ERL_NIF_TERM demuxer_is_ready(ErlNifEnv *env, int argc,
                              const ERL_NIF_TERM argv[]) {
  DemuxerContext *ctx;
  demuxer_get_ctx(env, argv[0], &ctx);

  return ctx->has_header ? enif_make_atom(env, "true")
                         : enif_make_atom(env, "false");
}

ERL_NIF_TERM demuxer_read_packet(ErlNifEnv *env, int argc,
                                 const ERL_NIF_TERM argv[]) {
  DemuxerContext *ctx;
  AVPacket *packet;
  int errnum, freespace;
  char err[256];

  packet = av_packet_alloc();
  demuxer_get_ctx(env, argv[0], &ctx);

  freespace = queue_freespace(ctx->queue);

  if (freespace > 0 && ctx->mode == CTX_MODE_BUF)
    return enif_make_tuple2(env, enif_make_atom(env, "demand"),
                            enif_make_long(env, freespace));

  if ((errnum = av_read_frame(ctx->fmt_ctx, packet)) < 0) {
    if (errnum == AVERROR_EOF)
      return enif_make_atom(env, "eof");

    av_strerror(errnum, err, sizeof(err));
    return enif_make_tuple2(env, enif_make_atom(env, "error"),
                            enif_make_string(env, err, ERL_NIF_UTF8));
  }

  ERL_NIF_TERM data;
  void *ptr = enif_make_new_binary(env, packet->size, &data);
  memcpy(ptr, packet->data, packet->size);

  ERL_NIF_TERM map;
  map = enif_make_new_map(env);

  enif_make_map_put(env, map, enif_make_atom(env, "stream_index"),
                    enif_make_long(env, packet->stream_index), &map);

  enif_make_map_put(env, map, enif_make_atom(env, "pts"),
                    enif_make_long(env, packet->pts), &map);

  enif_make_map_put(env, map, enif_make_atom(env, "dts"),
                    enif_make_long(env, packet->dts), &map);

  enif_make_map_put(env, map, enif_make_atom(env, "duration"),
                    enif_make_long(env, packet->duration), &map);

  enif_make_map_put(env, map, enif_make_atom(env, "data"), data, &map);

  av_packet_unref(packet);

  return enif_make_tuple2(env, enif_make_atom(env, "ok"), map);
}

ERL_NIF_TERM demuxer_streams(ErlNifEnv *env, int argc,
                             const ERL_NIF_TERM argv[]) {
  DemuxerContext *ctx;
  ERL_NIF_TERM *codecs;
  int errnum;
  char err[256];

  demuxer_get_ctx(env, argv[0], &ctx);

  // Called on EOS: we're not ready, meaning that we did not get
  // the amount of data we wanted, but we may still be able to
  // obtain the streams.
  if (!ctx->has_header) {
    if ((errnum = demuxer_read_header(ctx))) {
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
    AVCodecParameters *params = NULL;
    const char *codec_name;

    av_stream = ctx->fmt_ctx->streams[i];

    // Parameters are used to preserve as much information
    // as possible when creating a new Codec. We're making
    // a copy to ensure we own this data.

    params = avcodec_parameters_alloc();
    avcodec_parameters_copy(params, av_stream->codecpar);

    // Make the resource
    AVCodecParameters **codec_params_res =
        enif_alloc_resource(CODEC_PARAMS_RES_TYPE, sizeof(AVCodecParameters *));
    *codec_params_res = params;

    ERL_NIF_TERM res_term = enif_make_resource(env, codec_params_res);

    // This is done to allow the erlang garbage collector to take care
    // of freeing this resource when needed.
    enif_release_resource(codec_params_res);

    // Create the returned map of information.
    codec_name = avcodec_get_name(params->codec_id);

    ERL_NIF_TERM map;
    map = enif_make_new_map(env);

    enif_make_map_put(env, map, enif_make_atom(env, "codec_id"),
                      enif_make_int(env, params->codec_id), &map);
    enif_make_map_put(env, map, enif_make_atom(env, "codec_name"),
                      enif_make_string(env, codec_name, ERL_NIF_UTF8), &map);
    enif_make_map_put(env, map, enif_make_atom(env, "codec_params"), res_term,
                      &map);
    enif_make_map_put(env, map, enif_make_atom(env, "stream_index"),
                      enif_make_int(env, av_stream->index), &map);

    // TODO
    // Expand the information available to Elixir: here we can only select a
    // stream by index and codec, quality or language should be available too.

    codecs[i] = map;
  }

  return enif_make_tuple2(
      env, enif_make_atom(env, "ok"),
      enif_make_list_from_array(env, codecs, ctx->fmt_ctx->nb_streams));
}

ERL_NIF_TERM demuxer_demand(ErlNifEnv *env, int argc,
                            const ERL_NIF_TERM argv[]) {
  DemuxerContext *ctx;
  demuxer_get_ctx(env, argv[0], &ctx);

  return enif_make_int(env, ctx->queue->size - ctx->queue->buf_end);
}

// Called when the nif is loaded, as specified in the ERL_NIF_INIT call.
int load(ErlNifEnv *env, void **priv_data, ERL_NIF_TERM load_info) {
  int flags = ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER;
  CTX_RES_TYPE =
      enif_open_resource_type(env, NULL, "ctx", free_ctx_res, flags, NULL);

  CODEC_PARAMS_RES_TYPE = enif_open_resource_type(
      env, NULL, "codec_params", free_codec_params_res, flags, NULL);

  return 0;
}

static ErlNifFunc nif_funcs[] = {
    // {erl_function_name, erl_function_arity, c_function}
    {"demuxer_alloc_context", 0, demuxer_alloc_context},
    {"demuxer_add_data", 2, demuxer_add_data},
    {"demuxer_is_ready", 1, demuxer_is_ready},
    {"demuxer_demand", 1, demuxer_demand},
    {"demuxer_streams", 1, demuxer_streams},
    {"demuxer_read_packet", 1, demuxer_read_packet},
};

ERL_NIF_INIT(Elixir.Membrane.LibAV, nif_funcs, load, NULL, NULL, NULL)