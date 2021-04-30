.POSIX:
.SUFFIXES:
OUTDIR=.build
include $(OUTDIR)/config.mk

boson: $(boson_objects)
	@printf 'CCLD\t$@\n'
	@$(CC) $(LDFLAGS) -o $@ $(boson_objects) $(LIBS)

.SUFFIXES: .c .o .ha

.c.o:
	@printf 'CC\t$@\n'
	@$(CC) -c $(CFLAGS) -o $@ $<

docs:

clean:
	@rm -f boson $(boson_objects)

distclean: clean
	@rm -rf "$(OUTDIR)"

install: boson
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m755 boson $(DESTDIR)$(BINDIR)/boson

.PHONY: docs clean distclean install
