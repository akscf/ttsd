PREFIX ?= /opt/ttsd

UNAME_S=$(shell uname -s)

MOD_PATH=$(PREFIX)/lib/mods/
CFG_PATH=$(PREFIX)/configs/

MOD_OUT=mod-piper.so

PIPER_INC=-I/opt/piper/include
PIPER_LIB=-L/opt/piper/lib -lespeak-ng -lonnxruntime -lpiper_phonemize

TTSD_INC=-I../../../wstk_c/include -I../../ttsd/include
TTSD_LIB=-L/opt/ttsd/lib -lttsd -lwstk


CFLAGS=-O2 -s -fPIC
CXXFLAGS=-g -O2 -fPIC
#CFLAGS=-g -O2 -fPIC -DTTSD_DEBUG
#CXXFLAGS=-g -O2 -fPIC -DTTSD_DEBUG

LIBS=$(TTSD_LIB) $(PIPER_LIB)
INCS=-I. $(TTSD_INC) $(PIPER_INC)
CPP_LIBS=-lstdc++

MOD_SOURCES=mod-piper.c mod-piper-misc.c piper_api.cpp
MOD_OBJECTS=mod-piper.o mod-piper-misc.o piper_api.o

ifeq ($(UNAME_S),Linux)
	CPP=g++
	CC=gcc
	LD=gcc
	STRIP=strip -s
	CFLAGS+=-DWSTK_OS_LINUX
endif

ifeq ($(UNAME_S),FreeBSD)
	CPP=cc
	CC=cc
	LD=cc
	STRIP=strip -s
	CFLAGS+=-DWSTK_OS_FREEBSD
endif

ifeq ($(UNAME_S),Dawrin)
	CPP=cc
	CC=cc
	LD=cc
	STRIP=strip -s
	CFLAGS+=-DWSTK_OS_DARWIN
endif


all: $(MOD_SOURCES) $(MOD_OUT)

$(MOD_OUT): $(MOD_OBJECTS)
	$(LD) -shared -o $@ $(MOD_OBJECTS) $(LIBS) $(CPP_LIBS)
	$(STRIP) $(MOD_OUT)
.c.o:
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@
.cpp.o:
	$(CPP) $(CFLAGS) $(INCS) -c $< -o $@

install:
	cp $(MOD_OUT) $(MOD_PATH)
	cp mod-piper-conf.xml $(CFG_PATH)	

clean:
	@rm -f *.o *.so



