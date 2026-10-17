CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

.PHONY: all clean install uninstall test
all: lameorg

lameorg: src/lameorg.c
	$(CC) $(CFLAGS) -o $@ $<

install: lameorg
	install -Dm755 lameorg "$(DESTDIR)$(BINDIR)/lameorg"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/lameorg"

clean:
	rm -f lameorg

test: lameorg
	./tests/test.sh
