-include config.mak

.PHONY: all clean test

POLARSSL_DIR := ../polarssl-1.1.1
POLARSSL_INCDIR := $(POLARSSL_DIR)/include
CFLAGS += -Iinclude -I$(POLARSSL_INCDIR)

LIB += libprivcrypt.a

all: lib

clean:
	rm -f *.o $(LIB)

lib: $(LIB)

POLARSSL_LIBDIR := $(POLARSSL_DIR)/library
KEX_OBJS := null_kex.o test_kex.o polarssl_kex.o $(POLARSSL_LIBDIR)/libpolarssl.a
BACKEND_OBJS := kern_backend.o gpu_backend.o
FRONTEND_OBJS := gcrypt.o aesni.o aesni-intel_asm.o
$(LIB): $(FRONTEND_OBJS) priv_crypt.o util.o $(KEX_OBJS) $(BACKEND_OBJS)
	ar rcs $@ $^
