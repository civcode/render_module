#include "jpeg_encode.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <jpeglib.h>

/* Error unwinding stays entirely in C, never across C++ destructors. Mutable
   compressor/output state is on the heap so longjmp doesn't invalidate it. */
struct ErrorState {
    struct jpeg_error_mgr base;
    jmp_buf jump;
};
struct Encoder {
    struct jpeg_compress_struct compressor;
    struct ErrorState error;
    unsigned char* data;
    unsigned long size;
    unsigned char* row;
    struct jpeg_destination_mgr destination;
    size_t capacity, limit;
};
static void failed(j_common_ptr info) {
    struct ErrorState* error = (struct ErrorState*)info->err;
    longjmp(error->jump, 1);
}
/* Unlike jpeg_mem_dest(), publish every realloc immediately to our owned state.
   Its stock destination publishes only at finish, unsafe for fatal-error cleanup
   after buffer growth. The public destination-manager API also lets us cap it. */
static void init_destination(j_compress_ptr info) {
    struct Encoder* e = (struct Encoder*)info->client_data;
    e->destination.next_output_byte = e->data;
    e->destination.free_in_buffer = e->capacity;
}
static boolean grow_destination(j_compress_ptr info) {
    struct Encoder* e = (struct Encoder*)info->client_data;
    unsigned char* next;
    size_t capacity;
    if (e->capacity == e->limit) longjmp(e->error.jump, 1);
    capacity = e->capacity*2;
    if (capacity > e->limit) capacity = e->limit;
    next = (unsigned char*)realloc(e->data, capacity);
    if (!next) longjmp(e->error.jump, 1);
    e->data = next;
    e->destination.next_output_byte = next + e->capacity;
    e->destination.free_in_buffer = capacity - e->capacity;
    e->capacity = capacity;
    return TRUE;
}
static void finish_destination(j_compress_ptr info) {
    struct Encoder* e = (struct Encoder*)info->client_data;
    e->size = (unsigned long)(e->capacity - e->destination.free_in_buffer);
}
int rm_encode_jpeg(const unsigned char* rgba, int w, int h, int quality,
                   unsigned long max_bytes, unsigned char** data, unsigned long* size) {
    struct Encoder* e;
    if (!data || !size) return 0;
    *data = NULL; *size = 0;
    if (!rgba || w < 1 || h < 1 || w > 2048 || h > 2048 || quality < 1 || quality > 100 ||
        max_bytes < 1 || max_bytes > 16UL*1024*1024) return 0;
    e = (struct Encoder*)calloc(1, sizeof(*e));
    if (!e) return 0;
    e->compressor.err = jpeg_std_error(&e->error.base);
    e->error.base.error_exit = failed;
    if (setjmp(e->error.jump)) {
        jpeg_destroy_compress(&e->compressor);
        free(e->row); free(e->data); free(e); return 0;
    }
    jpeg_create_compress(&e->compressor);
    e->limit = max_bytes; e->capacity = max_bytes < 4096 ? max_bytes : 4096;
    e->data = (unsigned char*)malloc(e->capacity);
    if (!e->data) longjmp(e->error.jump, 1);
    e->compressor.client_data = e;
    e->destination.init_destination = init_destination;
    e->destination.empty_output_buffer = grow_destination;
    e->destination.term_destination = finish_destination;
    e->compressor.dest = &e->destination;
    e->compressor.image_width = (JDIMENSION)w;
    e->compressor.image_height = (JDIMENSION)h;
    e->compressor.input_components = 3;
    e->compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&e->compressor);
    jpeg_set_quality(&e->compressor, quality, TRUE);
    e->row = (unsigned char*)malloc((size_t)w*3);
    if (!e->row) longjmp(e->error.jump, 1);
    jpeg_start_compress(&e->compressor, TRUE);
    while (e->compressor.next_scanline < e->compressor.image_height) {
        size_t x;
        const unsigned char* source = rgba + (size_t)e->compressor.next_scanline*w*4;
        for (x = 0; x < (size_t)w; ++x) {
            e->row[x*3] = source[x*4]; e->row[x*3+1] = source[x*4+1]; e->row[x*3+2] = source[x*4+2];
        }
        jpeg_write_scanlines(&e->compressor, &e->row, 1);
    }
    jpeg_finish_compress(&e->compressor);
    jpeg_destroy_compress(&e->compressor);
    *data = e->data; *size = e->size;
    free(e->row); free(e); return 1;
}
