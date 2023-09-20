# Environment variables passed via elixir_make
# ERTS_INCLUDE_DIR
# MIX_APP_PATH

# erts version needs to be fetched dynamically!
ifndef ERTS_INCLUDE_DIR
ERTS_INCLUDE_DIR := $(shell rtx where erlang)/erts-14.0.2/include
endif

CC = clang
CFLAGS = -fPIC -g3 -fno-omit-frame-pointer $(shell pkg-config --cflags libavcodec libavformat libavutil) -I$(ERTS_INCLUDE_DIR)
LDFLAGS = -dynamiclib -undefined dynamic_lookup $(shell pkg-config --libs libavcodec libavformat libavutil)

OUTPUT = demuxer.so

all: $(OUTPUT)

demuxer.so: demuxer.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(OUTPUT)

