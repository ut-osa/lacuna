#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

void print_hex(size_t len, uint8_t *buf)
{
   int i;
   for (i = 0; i < len; i++) {
      if (i > 0 && !(i % 16))
         printf("\n");
      printf("%02x ", buf[i]);
   }
   printf("\n");
}
