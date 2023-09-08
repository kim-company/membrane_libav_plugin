#include <libavformat/avformat.h>
#include <libavformat/avio.h>

#define IO_BUF_SIZE 48000

int read_packet(void *opaque, uint8_t *buf, int buf_size) {
  return fread(buf, 1, buf_size, opaque);
}

int main() {
  char *path = "../test/data/safari";
  FILE *f = fopen(path, "rb");

  void *io_buffer = av_malloc(IO_BUF_SIZE);
  AVIOContext *io_ctx =
      avio_alloc_context(io_buffer, IO_BUF_SIZE, 0, f, read_packet, NULL, NULL);

  AVFormatContext *fmt_ctx = avformat_alloc_context();
  fmt_ctx->pb = io_ctx;

  avformat_open_input(&fmt_ctx, "", NULL, NULL);
  avformat_find_stream_info(fmt_ctx, NULL);

  const AVCodec *codec = NULL;
  for (int i = 0; i < fmt_ctx->nb_streams; i++) {
    codec = avcodec_find_decoder(fmt_ctx->streams[i]->codecpar->codec_id);
    printf("Stream #%d: codec ID %s\n", fmt_ctx->streams[i]->id,
           codec->long_name);
  }

  return 0;
}
