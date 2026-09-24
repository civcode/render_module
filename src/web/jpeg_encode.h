#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Top-origin RGBA -> RGB JPEG. max_bytes: 1..16 MiB. Caller frees *data.
   Failure (including output cap) returns 0 with *data=NULL, *size=0. */
int rm_encode_jpeg(const unsigned char* rgba, int width, int height, int quality,
                   unsigned long max_bytes, unsigned char** data, unsigned long* size);
#ifdef __cplusplus
}
#endif
