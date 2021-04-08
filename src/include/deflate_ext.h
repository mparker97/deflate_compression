#ifndef DEFLATE_EXT_H
#define DEFLATE_EXT_H

#include <stdio.h>
#include "globals.h"
#define DEFLATE_NULLTERM 1

typedef unsigned short swi; // sliding window index

typedef struct deflate_compr deflate_compr_t;
SPAWNABLE_HEADER(deflate_compr_t);

void deflate_compr_init(deflate_compr_t* com, int fd_in, int fd_out, int fd_stats, swi sw);
void deflate_compr_deinit(deflate_compr_t* com);

int deflate_decompress(struct string_len* decompr_dat, struct string_len* compr_dat, int ops);
int deflate_compress(int fd_in, int fd_out, int fd_stats, swi sw, int ops);

struct compress_stats{
	int bytes; // number of bytes processed
	int tree_bits; // number of bits in trees
	int ll_bits; // number of bits in lit/len compressed data
	int d_bits; // number of bits in dist compressed data
	int ll; // lit character or length
	int d; // 0 or distance
	
	// check d to see whether this is a literal (d == 0) or len/dist pair (d != 0)
	
	// bits from compressed data only = ll_bits + d_bits
	// rate = (double)(tree_bits + ll_bits + d_bits) / bytes
};

#endif
