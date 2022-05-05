TARGET=
OBJ=

ROOT_DIR=.
HIREDIS_LIB=$(ROOT_DIR)/3rd/hiredis/libhiredis.a
HIREDIS_INCLUDE=$(ROOT_DIR)/3rd/hiredis

LDFLAGS=-Wl,$(HIREDIS_LIB)

# Fallback to gcc when $CC is not in $PATH.
CC:=$(shell sh -c 'type $${CC%% *} >/dev/null 2>/dev/null && echo $(CC) || echo gcc')
CXX:=$(shell sh -c 'type $${CXX%% *} >/dev/null 2>/dev/null && echo $(CXX) || echo g++')
OPTIMIZATION?=-O3
WARNINGS=-Wall -W -Wstrict-prototypes -Wwrite-strings -Wno-missing-field-initializers
DEBUG_FLAGS?= -g -ggdb
REAL_CFLAGS=$(OPTIMIZATION) -fPIC $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(DEBUG_FLAGS)
REAL_LDFLAGS=$(LDFLAGS)

all: $(TARGET)

alloc.o: alloc.c fmacros.h alloc.h
async.o: async.c fmacros.h alloc.h async.h hiredis.h read.h sds.h net.h dict.c dict.h win32.h async_private.h
dict.o: dict.c fmacros.h alloc.h dict.h
hiredis.o: hiredis.c fmacros.h hiredis.h read.h sds.h alloc.h net.h async.h win32.h
net.o: net.c fmacros.h net.h hiredis.h read.h sds.h alloc.h sockcompat.h win32.h
read.o: read.c fmacros.h alloc.h read.h sds.h win32.h
sds.o: sds.c sds.h sdsalloc.h alloc.h
sockcompat.o: sockcompat.c sockcompat.h
test.o: test.c fmacros.h hiredis.h read.h sds.h alloc.h net.h sockcompat.h win32.h

# Binaries:
hiredis: $(ROOT_DIR)/3rd/hiredis/
	git submodule update --init --recursive
	$(MAKE) -C $(ROOT_DIR)/3rd/hiredis/

cargador: src/cargador.c $(HIREDIS_LIB)
	$(CC) -o $@ $(HIREDIS_LIB) $(REAL_CFLAGS) -I$(HIREDIS_INCLUDE) $< -levent $(REAL_LDFLAGS)

clean:
	rm -rf *.o

dep:
	$(CC) $(CPPFLAGS) $(CFLAGS) -MM *.c

