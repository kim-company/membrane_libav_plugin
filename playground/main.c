#include "libavcodec/codec.h"
#include "libavcodec/packet.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <stdio.h>
#include <string.h>

#define PROBE_SIZE 2048

static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame,
                   FILE *outfile) {
  int i, ch;
  int ret, data_size;

  /* send the packet with the compressed data to the decoder */
  ret = avcodec_send_packet(dec_ctx, pkt);
  if (ret < 0) {
    fprintf(stderr, "Error submitting the packet to the decoder\n");
    exit(1);
  }

  /* read all the output frames (in general there may be any number of them */
  while (ret >= 0) {
    ret = avcodec_receive_frame(dec_ctx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      return;
    else if (ret < 0) {
      fprintf(stderr, "Error during decoding\n");
      exit(1);
    }
    data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
    if (data_size < 0) {
      /* This should not occur, checking just for paranoia */
      fprintf(stderr, "Failed to calculate data size\n");
      exit(1);
    }
    for (i = 0; i < frame->nb_samples; i++)
      for (ch = 0; ch < dec_ctx->ch_layout.nb_channels; ch++)
        fwrite(frame->data[ch] + data_size * i, 1, data_size, outfile);
  }
}

int main() {
  // char *path = "/Users/dmorn/Downloads/safari.mp4";
  char *path = "../test/data/safari";
  // char *path = "/Users/dmorn/Downloads/multi-lang.mp4";

  AVFormatContext *fmt_ctx = avformat_alloc_context();
  if (avformat_open_input(&fmt_ctx, path, NULL, NULL) != 0) {
    fprintf(stderr, "Failed to open input file\n");
    return -1;
  }

  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    fprintf(stderr, "Failed to find stream information\n");
    avformat_close_input(&fmt_ctx);
    return -1;
  }

  printf("%s\n", fmt_ctx->iformat->name);

  const AVInputFormat *format = av_find_input_format(fmt_ctx->iformat->name);
  if (!format) {
    fprintf(stderr, "Could not find input format\n");
    avformat_close_input(&fmt_ctx);
    return -1;
  }

  printf("Format detected: %s\n", format->long_name);
  AVStream *stream = NULL;
  const AVCodec *codec = NULL;
  for (int i = 0; i < fmt_ctx->nb_streams; i++) {
    stream = fmt_ctx->streams[i];
    codec = avcodec_find_decoder(stream->codecpar->codec_id);
    printf("codec found: %s\n", codec->name);
    break;
  }

  AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
  avcodec_open2(codec_ctx, codec, NULL);

  AVPacket packet;
  while (av_read_frame(fmt_ctx, &packet) >= 0) {
    int ret = avcodec_send_packet(codec_ctx, &packet);
    if (ret != 0) {
      perror("avcode_send_packet/2");
      return 1;
    }
  }

  // Codec initialization done.

  // for (stream = fmt_ctx->streams; stream != NULL;

  return 0;
}
