DESTDIR   ?=
PREFIX    ?= /usr/local
MANPREFIX ?= $(PREFIX)/man

CFLAGS += -Wall -Wextra

all: within

check: within
	test `./within . . - pwd | wc -l` -eq 2

clean:
	rm -f within

install: within
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(MANPREFIX)/man1
	install within $(DESTDIR)$(PREFIX)/bin/
	install within.1 $(DESTDIR)$(MANPREFIX)/man1/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/within
	rm -f $(DESTDIR)$(MANPREFIX)/man1/within.1

.PHONY: all check clean install uninstall
