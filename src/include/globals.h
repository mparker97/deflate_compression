#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "global_errors.h"

#define min(a, b) (((a) < (b))? (a) : (b))
#define closec(x) do {close(x); (x) = 0;} while (0)
#define freec(x) do {free(x); (x) = NULL;} while (0)

#define bit_inc(byte, bit, n) do{(byte) += ((bit) + (n)) / 8; (bit) = ((bit) + (n)) % 8;} while (0)
#define byte_inc(byte, bit, n) do{(byte) += (n); (bit) = 0;} while (0)
#define byte_roundup(byte, bit) if (bit) do{(byte)++; (bit) = 0;} while (0)
#define MASK(val, a, b) ((*(1ULL << (b)) - 1) & (val)) >> (a))

struct string_len{
	// character array with length, so that \0 can be a member without termination
	unsigned char* str;
	size_t len;
};

// Read 'len' bits (up to 4 bytes) from 'byte' starting at bit 'bit'
unsigned int _bits32(unsigned char* byte, int bit, int len){
	// must have len + bit <= 32
	unsigned int ret = 0;
	memcpy(&ret, byte, min((bit + len + 7) / 8, sizeof(unsigned int)));
	return (ret >> bit) & ((1U << len) - 1);
}

// Read 'len' bits (up to 4 bytes) from '*byte' starting at bit '*bit', and advance '*byte' and '*bit" accordingly
static inline unsigned int read_bits32(unsigned char** byte, int* bit, int len){
	unsigned int ret = _bits32(*byte, *bit, len);
	bit_inc(*byte, *bit, len);
	return ret;
}

// Add space of size 'elm_sz' to amortized list '*list' with length '*len'
// 'elm_sz' should be the same for each call using the same list
// Automatically double the list's space if its capacity is reached
int a_list_add(void** list, unsigned int* len, size_t elm_sz){
	if (!(*len & (*len - 1)))
		if ((*list = realloc(*list, *len * elm_sz << 1)) == NULL)
			return E_MALLOC;
	memset(*list + *len, 0, elm_sz); // clear new elm
	(*len)++;
	return 0;
}

int reverse_bits(int x, int len){
	int f;
	for (f = 0; len > 0; len--){
		f |= (x & 1);
		f <<= 1;
		x >>= 1;
	}
	return f;
}

#endif
