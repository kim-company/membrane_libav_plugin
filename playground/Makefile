CC = clang
# TODO
# erts version needs to be fetched dynamically!
CFLAGS = -g $(shell pkg-config --cflags libavcodec libavformat libavutil) -I$(shell rtx where erlang)/erts-14.0.2/include
LDFLAGS = $(shell pkg-config --libs libavcodec libavformat libavutil)

SRC = nif.c detect_format.c detect_streams.c
OUTPUT = $(patsubst %.c,%,$(SRC))

all: $(OUTPUT)

%: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(OUTPUT)

