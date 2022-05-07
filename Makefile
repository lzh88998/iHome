TARGET=
OBJ=log.o to_socket.o

STLIB_MAKE_CMD=$(AR) rcs
# DYLIB_MAKE_CMD=$(CC) -shared -Wl,-soname,log.so

ROOT_DIR=.
HIREDIS_LIB=$(ROOT_DIR)/3rd/hiredis/libhiredis.a
HIREDIS_INCLUDE=$(ROOT_DIR)/3rd/hiredis

COMMON_LIB=lib_ihome_common.a

LDFLAGS=-Wl,$(HIREDIS_LIB),$(COMMON_LIB)

# Fallback to gcc when $CC is not in $PATH.
CC:=$(shell sh -c 'type $${CC%% *} >/dev/null 2>/dev/null && echo $(CC) || echo gcc')
CXX:=$(shell sh -c 'type $${CXX%% *} >/dev/null 2>/dev/null && echo $(CXX) || echo g++')
OPTIMIZATION?=-O3
WARNINGS=-Wall -W -Wstrict-prototypes -Wwrite-strings -Wno-missing-field-initializers
DEBUG_FLAGS?= -g -ggdb
REAL_CFLAGS=$(OPTIMIZATION) -fPIC $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(DEBUG_FLAGS)
REAL_LDFLAGS=$(LDFLAGS)

all: $(TARGET)

log.o: src/log.c src/log.h
	$(CC) -std=c99 -c $(REAL_CFLAGS) $<
	
to_socket.o: src/to_socket.c src/to_socket.h
	$(CC) -std=c99 -c $(REAL_CFLAGS) $<


# Binaries:
$(HIREDIS_LIB):$(ROOT_DIR)/3rd/hiredis/
	git submodule update --init --recursive
	$(MAKE) -C $(ROOT_DIR)/3rd/hiredis/

$(COMMON_LIB):$(OBJ)
	$(STLIB_MAKE_CMD) $(COMMON_LIB) $(OBJ)

cargador:src/cargador.c $(HIREDIS_LIB) $(COMMON_LIB)
	$(CC) -o $@ $(HIREDIS_LIB) $(REAL_CFLAGS) -I$(HIREDIS_INCLUDE) $< -levent $(REAL_LDFLAGS)
	
godown_keeper:src/godown_keeper.c $(HIREDIS_LIB) $(COMMON_LIB)
	$(CC) -o $@ $(HIREDIS_LIB) $(REAL_CFLAGS) -I$(HIREDIS_INCLUDE) $< -levent $(REAL_LDFLAGS)
	
touch:src/touch.c $(HIREDIS_LIB) $(COMMON_LIB)
	$(CC) -o $@ $(HIREDIS_LIB) $(REAL_CFLAGS) -I$(HIREDIS_INCLUDE) $< -levent $(REAL_LDFLAGS)
	
sensor:src/sensor.c $(HIREDIS_LIB) $(COMMON_LIB)
	$(CC) -o $@ $(HIREDIS_LIB) $(REAL_CFLAGS) -I$(HIREDIS_INCLUDE) $< -levent $(REAL_LDFLAGS)
	
lcd:src/LCD.c $(HIREDIS_LIB) $(COMMON_LIB)
	$(CC) -o $@ $(HIREDIS_LIB) $(REAL_CFLAGS) -I$(HIREDIS_INCLUDE) $< -levent $(REAL_LDFLAGS)

clean:
	rm -rf *.o *.a cargador godown_keeper touch sensor
	rm -rf src/*.o

dep:
	$(CC) $(CPPFLAGS) $(CFLAGS) -MM src/*.c

