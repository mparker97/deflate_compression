#ifndef DEFLATE_EXT_H
#define DEFLATE_EXT_H

#include "globals.h"
#define DEFLATE_NULLTERM 1

int deflate_decompress(struct string_len* decompr_dat, struct string_len* compr_dat, int ops);
int deflate_compress(struct string_len* compr_dat, struct string_len* decompr_dat, int ops);

#endif