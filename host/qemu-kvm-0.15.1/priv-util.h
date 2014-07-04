#ifndef PRIV_UTIL_H
#define PRIV_UTIL_H

#include <priv_crypt.h>

int priv_easy_create(struct priv_cctx **cctx_store, int dir_in);
void print_hex(uint8_t *buf, size_t len);
/* A safe strncpy that ensures the destination is always
   NUL-terminated if n > 0 (so that some room is available in dest) */
void sstrncpy(char *dest, const char *src, size_t n);

#endif
