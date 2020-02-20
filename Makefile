.POSIX:

PREFIX=/usr/local
MANDIR=$(PREFIX)/share/man
ALL_CFLAGS=$(CFLAGS) -Wall -Wextra -std=c99 -pedantic
OBJ=\
	main.o

.c.o:
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

boson: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

$(OBJ): $(HDR)

install: boson boson.1
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp boson $(DESTDIR)$(PREFIX)/bin/
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp boson.1 $(DESTDIR)$(MANDIR)/man1/

clean:
	rm -f boson $(OBJ)

.PHONY: install clean
