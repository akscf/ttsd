PREFIX ?= /opt/ttsd

UNAME_S=$(shell uname -s)
LIB_OUT=libttsd.so
BIN_OUT=ttsd

WSTK_INC=-I../../wstk_c/include
WSTK_LIB=-L../../wstk_c -lwstk

LAME_INC=-I../../lame/include
LAME_LIB=-L../../lame/libmp3lame/.libs -lmp3lame

SPEEXSDP_INC=-I../../speexdsp/include
SPEEXDSP_LIB=-L../../speexdsp/libspeexdsp/.libs -lspeexdsp

TTSD_INC=-I./include
TTSD_LIB=-L. -lttsd

LDFLAGS=-s
CFLAGS=-O2 -s -fPIC
#CFLAGS=-g -O2 -fPIC -DTTSD_DEBUG
LIBS=-lc -lm -lpthread $(TTSD_LIB) $(WSTK_LIB) $(LAME_LIB) $(SPEEXDSP_LIB)
INCS=$(TTSD_INC) $(WSTK_INC) $(LAME_INC) $(SPEEXSDP_INC)

BIN_SOURCES=./src/main.c
LIB_SOURCES=./src/ttsd-main.c ./src/ttsd-misc.c ./src/ttsd-config.c ./src/ttsd-http-server.c ./src/ttsd-modules.c ./src/ttsd-codecs.c

BIN_OBJECTS=$(BIN_SOURCES:.c=.o)
LIB_OBJECTS=$(LIB_SOURCES:.c=.o)

ifeq ($(UNAME_S),Linux)
	CC=gcc
	LD=gcc
	STRIP=strip -s
	CFLAGS+=-DWSTK_OS_LINUX
endif

ifeq ($(UNAME_S),FreeBSD)
	CC=cc
	LD=cc
	STRIP=strip -s
	CFLAGS+=-DWSTK_OS_FREEBSD
endif

ifeq ($(UNAME_S),Dawrin)
	CC=cc
	LD=cc
	STRIP=strip -s
	CFLAGS+=-DWSTK_OS_DARWIN
endif


all: $(LIB_SOURCES) $(BIN_SOURCE) $(LIB_OUT) $(BIN_OUT)

$(LIB_OUT): $(LIB_OBJECTS)
	$(LD) -shared -o $@ $(LIB_OBJECTS)
	$(STRIP) $(LIB_OUT)

$(BIN_OUT): $(BIN_OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(BIN_OBJECTS) $(LIBS)
	$(STRIP) $(BIN_OUT)
.c.o:
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

install:
	mkdir -p $(PREFIX)/bin/
	mkdir -p $(PREFIX)/lib/
	mkdir -p $(PREFIX)/lib/mods/
	mkdir -p $(PREFIX)/include/
	mkdir -p $(PREFIX)/configs/
	cp $(LIB_OUT) $(PREFIX)/lib/
	cp $(BIN_OUT) $(PREFIX)/bin/
	cp misc/ttsd-run.sh $(PREFIX)/bin/
	cp misc/ttsd-conf.xml $(PREFIX)/configs/
	rm -drf $(PREFIX)/include/ttsd
	cp -r ./include $(PREFIX)/include/ttsd

clean:
	@rm -f src/*.o
	@rm -f $(BIN_OUT)
	@rm -f $(LIB_OUT)


