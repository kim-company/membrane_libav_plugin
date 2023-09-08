#include <libavformat/avformat.h>
#include <stdio.h>

#define PROBE_SIZE 2048

int main() {
  // char *path = "../test/data/safari";
  // FILE *f = fopen(path, "rb");

  unsigned char buffer[PROBE_SIZE + AVPROBE_PADDING_SIZE];
  int read = fread(buffer, 1, PROBE_SIZE, stdin);

  memset(&buffer[PROBE_SIZE], 0, AVPROBE_PADDING_SIZE);

  AVProbeData probe = {"", buffer, PROBE_SIZE, NULL};
  const AVInputFormat *input_fmt = av_probe_input_format(&probe, 1);

  fprintf(stderr, "Format: %s (%s)\n", input_fmt->long_name,
          input_fmt->extensions);

  AVFormatContext *fmt_ctx = avformat_alloc_context();

  return 0;
}
