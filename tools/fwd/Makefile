CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Iinclude -Isrc -fPIC
# Feature / syscall switches, e.g.:  make DEFS="-DFWD_ENABLE_TUNTAP=0"
CFLAGS  += $(DEFS)
# QUIC control channel and tunnels: needs OpenSSL >= 3.5. Auto-detected; force with
# make QUIC=1 / make QUIC=0 (QUIC=0 keeps the library libc only).
QUIC    ?= $(shell pkg-config --atleast-version=3.5 openssl 2>/dev/null && echo 1 || echo 0)
ifeq ($(QUIC),1)
CFLAGS  += -DFWD_ENABLE_QUIC=1 $(shell pkg-config --cflags openssl 2>/dev/null)
LDLIBS  += $(or $(shell pkg-config --libs openssl 2>/dev/null),-lssl -lcrypto)
endif

SRC  := $(wildcard src/*.c)
OBJ  := $(SRC:src/%.c=build/%.o)

all: build/libfwd.a build/libfwd.so build/fwdd build/fwdctl

build/%.o: src/%.c include/*.h src/fwd_internal.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/libfwd.a: $(OBJ)
	$(AR) rcs $@ $^

build/libfwd.so: $(OBJ)
	$(CC) -shared -o $@ $^ $(LDLIBS)

build/fwdd: examples/fwdd.c build/libfwd.a
	$(CC) $(CFLAGS) -o $@ $< build/libfwd.a $(LDLIBS)

build/fwdctl: examples/fwdctl.c build/libfwd.a
	$(CC) $(CFLAGS) -o $@ $< build/libfwd.a $(LDLIBS)

build/test_fwd: tests/test_fwd.c build/libfwd.a
	$(CC) $(CFLAGS) -o $@ $< build/libfwd.a $(LDLIBS)

test: build/test_fwd
	./build/test_fwd

clean:
	rm -rf build

.PHONY: all test clean
