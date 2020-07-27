.POSIX:
VERSION=0.0.1

INCLUDE=-Iinclude
FLAGS=-g -Wall -Wextra -Werror -Wno-unused-parameter -DVERSION='"$(VERSION)"' -fno-common $(CFLAGS)
LDFLAGS=-static

OUTDIR?=build
.DEFAULT_GOAL=all

OBJECTS=\
	$(OUTDIR)/getopt_long.o \
	$(OUTDIR)/log.o \
	$(OUTDIR)/token.o \
	$(OUTDIR)/lexer.o \
	$(OUTDIR)/parse.o \
	$(OUTDIR)/setup.o \
	$(OUTDIR)/main.o

$(OUTDIR)/%.o: src/%.c
	@mkdir -p $(OUTDIR)
	$(CC) -std=c99 -pedantic -c -o $@ $(FLAGS) $(INCLUDE) $<

boson: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

all: boson

clean:
	rm -rf boson $(OUTDIR)

.PHONY: all clean
