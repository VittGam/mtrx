-include .config

INSTALL ?= install

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS += -std=gnu1x -Wall -pedantic -O2

LDLIBS_OPUS ?= -lopus
LDLIBS_ASOUND ?= -lasound

LDLIBS += -lm -lrt -lpthread $(LDLIBS_OPUS) $(LDLIBS_ASOUND)

.PHONY:		all install clean

all:		mtx mrx

mtx:		mtx.c

mrx:		mrx.c

install:	mtx mrx
		$(INSTALL) -D -s mtx mrx -t $(DESTDIR)$(BINDIR)

clean:
		rm -f mtx mrx
