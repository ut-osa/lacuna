
#pragma once


//Isolated definition
#ifndef __VECTOR_TYPES_H__
typedef struct{
    unsigned char x, y, z, w;
} uchar4;
#endif

extern "C" void LoadBMPFile(uchar4 **dst, int *width, int *height, const char *name);
