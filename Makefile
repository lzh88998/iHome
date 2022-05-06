TARGET=
OBJ=log.o

STLIB_MAKE_CMD=$(AR) rcs
# DYLIB_MAKE_CMD=$(CC) -shared -Wl,-soname,log.so

ROOT_DIR=.
HIREDIS_LIB=$(ROOT_DIR)/3rd/hiredis/libhiredis.a
HIREDIS_INCLUDE=$(ROOT_DIR)/3rd/hiredis

LOG_LIB=liblog.a

LDFLAGS=-Wl,$(HIREDIS_LIB),$(LOG_LIB)

# Fallback to gcc when $CC is not in $PATH.
CC:=$(shell sh -c 'type $${CC%% *} >/dev/null 2>/dev/null && echo $(CC) || echo gcc')
CXX:=$(shell sh -c 'type $${CXX%% *} >/dev/null 2>/dev/null && echo $(CXX) || echo g++')
OPTIMIZATION?=-O3
WARNINGS=-Wall -W -Wstrict-prototypes -Wwrite-strings -Wno-missing-field-initializers
DEBUG_FLAGS?= -g -ggdb
REAL_CFLAGS=$(OPTIMIZATION) -fPIC $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(DEBUG_FLAGS)
REAL_LDFLAGS=$(LDFLAGS)

all: $(TARGET)

LCD.o: src/LCD.c
cargador.o: src/cargador.c src/log.h
godown_keeper.o: src/godown_keeper.c
log.o: src/log.c src/log.h
	$(CC) -std=c99 -c $(REAL_CFLAGS) $<
	
monitor.o: src/monitor.c
sw_controller.o: src/sw_controller.c
test.o: src/test.c
test2.o: src/test2.c
touch.o: src/touch.c
touch_processor.o: src/touch_processor.c

# Binaries:
hiredis:$(ROOT_DIR)/3rd/hiredis/
	git submodule update --init --recursive
	$(MAKE) -C $(ROOT_DIR)/3rd/hiredis/

$(LOG_LIB):$(OBJ)
	$(STLIB_MAKE_CMD) $(LOG_LIB) $(OBJ)

cargador:src/cargador.c $(HIREDIS_LIB) $(LOG_LIB)
	$(CC) -o $@ $(HIREDIS_LIB) $(REAL_CFLAGS) -I$(HIREDIS_INCLUDE) $< -levent $(REAL_LDFLAGS)

clean:
	rm -rf *.o *.a cargador
	rm -rf src/*.o

dep:
	$(CC) $(CPPFLAGS) $(CFLAGS) -MM src/*.c

