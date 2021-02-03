#ifndef DEFLATE_EXT_H
#define DEFLATE_EXT_H

#include <stdio.h>
#include "globals.h"
#define DEFLATE_NULLTERM 1

typedef unsigned short swi; // sliding window index

typedef struct deflate_compr deflate_compr_t;
SPAWNABLE_HEADER(deflate_compr_t);

void deflate_compr_init(deflate_compr_t* com, const char* fn, swi sw);
void deflate_compr_deinit(deflate_compr_t* com);

int deflate_decompress(struct string_len* decompr_dat, struct string_len* compr_dat, int ops);
int deflate_compress(struct string_len* compr_dat, struct string_len* decompr_dat, int ops);

#endif
