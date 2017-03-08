-include .config

INSTALL ?= install

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS += -std=gnu1x -Wall -pedantic -O2

LDLIBS_OPUS ?= -lopus
LDLIBS_ASOUND ?= -lasound

LDLIBS += -lm -lrt -lpthread $(LDLIBS_OPUS) $(LDLIBS_ASOUND)

.PHONY:		all install clean

all:		mtx mrx mtrx

mtx:		mtx.c common.c

mrx:		mrx.c common.c

mtx_multi.o:	mtx.c
		$(CC) -c -Dmain=mtx_main $(CFLAGS) mtx.c -o mtx_multi.o

mrx_multi.o:	mrx.c
		$(CC) -c -Dmain=mrx_main $(CFLAGS) mrx.c -o mrx_multi.o

mtrx:		multicall.c mtx_multi.o mrx_multi.o common.c
		$(CC) $(CFLAGS) multicall.c mtx_multi.o mrx_multi.o common.c $(LDLIBS) -o mtrx

install:	mtx mrx
		$(INSTALL) -D -s mtx mrx -t $(DESTDIR)$(BINDIR)

install_multi:	mtrx
		$(INSTALL) -D -s mtrx -t $(DESTDIR)$(BINDIR)

clean:
		rm -f mtx mrx mtrx mtx_multi.o mrx_multi.o
