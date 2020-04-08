.POSIX:
VERSION=0.0.1

INCLUDE=-Iinclude
CFLAGS+=-g -Wall -Wextra -Werror -Wno-unused-parameter -DVERSION='"$(VERSION)"'
LDFLAGS+=-static

OUTDIR?=build
.DEFAULT_GOAL=all

OBJECTS=\
	$(OUTDIR)/getopt_long.o \
	$(OUTDIR)/command.o \
	$(OUTDIR)/main.o

$(OUTDIR)/%.o: src/%.c
	@mkdir -p $(OUTDIR)
	$(CC) -std=c99 -pedantic -c -o $@ $(CFLAGS) $(INCLUDE) $<

boson: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

all: boson

clean:
	rm -rf boson $(OUTDIR)

.PHONY: all clean
