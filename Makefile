.POSIX:
VERSION=0.0.1

INCLUDE=-Iinclude
FLAGS=-g -Wall -Wextra -Werror -Wno-unused-parameter -DVERSION='"$(VERSION)"' -fno-common $(CFLAGS)
LDFLAGS=-static

OUTDIR?=build
.DEFAULT_GOAL=all

OBJECTS=\
	$(OUTDIR)/getopt_long.o \
	$(OUTDIR)/hash_table.o \
	$(OUTDIR)/log.o \
	$(OUTDIR)/ninja.o \
	$(OUTDIR)/options.o \
	$(OUTDIR)/object.o \
	$(OUTDIR)/builtin.o \
	$(OUTDIR)/interpreter.o \
	$(OUTDIR)/ast.o \
	$(OUTDIR)/token.o \
	$(OUTDIR)/lexer.o \
	$(OUTDIR)/parser.o \
	$(OUTDIR)/setup.o \
	$(OUTDIR)/main.o

$(OUTDIR)/%.o: src/%.c
	@mkdir -p $(OUTDIR)
	cc -std=c11 -pedantic -c -o $@ $(FLAGS) $(INCLUDE) $<

boson: $(OBJECTS)
	cc $(LDFLAGS) -o $@ $^

all: boson

clean:
	rm -rf boson $(OUTDIR)

.PHONY: all clean
