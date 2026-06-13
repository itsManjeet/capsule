DESTDIR ?= /
PREFIX ?= usr
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib

CFLAGS ?= -Wall -Wextra
LDFLAGS ?=

SOURCE_FILES := $(wildcard src/*.c)
OBJECT_FILES := $(SOURCE_FILES:.c=.o)
.PHONY: all clean test

all: lipi

lipi: $(OBJECT_FILES)
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(OBJECT_FILES) lipi

test: lipi
	./tests/run_tests.sh
	@for i in lib/*.lipi; do \
		./lipi -test $$i >/dev/null && echo "[PASS] $$i" || echo "[FAIL] $$i"; \
	done

install: lipi
	install -D -m 755 $< $(DESTDIR)$(BINDIR)/lipi
	install -D -m 0644 lib/*.lipi -t $(DESTDIR)$(LIBDIR)/lipi/
