CC = clang
CFLAGS = -g $(shell pkg-config --cflags libavcodec libavformat libavutil)
LDFLAGS = $(shell pkg-config --libs libavcodec libavformat libavutil)
# LIBS = -lavcodec -lavformat -lavutil

SRC = main.c
OUTPUT = main

all: $(OUTPUT)

$(OUTPUT): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(OUTPUT)

