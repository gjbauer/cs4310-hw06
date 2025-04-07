
SRCS := $(wildcard *.c)
BINS := $(SRCS:.c=)
CFLAGS := -g -std=gnu99 -I.. -lm

all: $(BINS)

$(BINS): $(SRCS)
	gcc $(CFLAGS) -o $@ $@.c ../pmalloc.c

clean:
	rm -f $(BINS)
