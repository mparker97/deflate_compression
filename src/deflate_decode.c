// http://www.integpg.com/deflate-compression-algorithm/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "include/globals.h"
#include "include/deflate.h"
#include "include/errors.h"

#define DEFLATE_DECOMP_INIT_SZ (256 * sizeof(unsigned char))

struct deflate_decompr{ // amortized list
	unsigned char* d;
	size_t sz;
	size_t sliding_window; // obtained from header; used to verify dists aren't too large
};

struct code_len{
	short val;
	short len;
};

// See 3.2.7, HCLEN
const unsigned char D1_INIT_CODE_LENS[] = {16, 0, 17, 0, 18, 0, 0, 0, 8, 0, 7, 0, 9, 0, 6, 0, 10, 0, 5, 0, 11, 0, 4, 0, 12, 0, 3, 0, 13, 0, 2, 0, 14, 0, 1, 0, 15, 0};

int code_len_cmp(const void* a, const void* b){ // for qsort
	// sort by len, then by val
	int ret = ((struct code_len*)a)->len - ((struct code_len*)b)->len;
	if (ret == 0){
		ret = ((struct code_len*)a)->val - ((struct code_len*)b)->val;
	}
	return ret;
}

// Write a single character to the 'dec' amortized list
int decompr_write_char(struct deflate_decompr* dec, unsigned char c){
	int ret = 0;
	if (!(dec->sz & (dec->sz - 1))){
		if ((dec->d = realloc(dec->d, dec->sz << 1)) == NULL)
			fail_out(E_MALLOC);
	}
	dec->d[dec->sz++] = c;
fail:
	return ret;
}

// Write the character sequence 'str' of length 'len' to the 'dec' amortized list
int decompr_write_str(struct deflate_decompr* dec, const unsigned char* str, unsigned int len){
	int ret = 0;
	if ((dec->d = realloc(dec->d, 1ULL << __builtin_clzl(dec->sz - 1 + len))) == NULL)
		fail_out(E_MALLOC);
	// copy chars
	while (len-- > 0){
		dec->d[dec->sz++] = *(str++);
	}
fail:
	return ret;
}

// Compute the adler32 checksum of the memory segment at 'b' of length 'len'
unsigned int adler32(const unsigned char* b, size_t len){
	unsigned int s1 = 1, s2 = 0;
	size_t i;
	for (i = 0; i < len; i++){
		s1 = (s1 + b[i]) % 65521;
		s2 = (s2 + s1) % 65521;
	}
	return (s2 << 16) | s1;
}

// Look up the Huffman code value from the fixed literal/length Huffman tree
	// "Code" to "Lit Value" in 3.2.6 Table 1
int h_tree_lookup_fl(unsigned char** byte, int* bit){
	int b, adjust;
	b = reverse_bits(read_bits32(byte, bit, 7), 7);
	if (b < 24){ // lit values 256-279: codes 0000000 to 0010111
		adjust = 256;
	}
	else{
		b <<= 1;
		b |= read_bits32(byte, bit, 1);
		if (b < 192){ // lit values 0-143: codes 00110000 to 10111111
			adjust = -48;
		}
		else if (b < 200){ // lit values 280-287: codes 11000000 to 11000111
			adjust = 88;
		}
		else{ // lit values 144-255: codes 110010000 to 111111111
			b <<= 1;
			b |= read_bits32(byte, bit, 1);
			adjust -256;
		}
	}
	return b + adjust;
}

// Get the Huffman code from the fixed literal/length code value
	// "Lit Value" to "Code" in 3.2.6 Table 1
h_code code_fl(unsigned int v, int* nb){
	int adjust, len;
	if (v < 144){
		adjust = 48;
		len = 8;
	}
	else if (v < 256){
		adjust = 256;
		len = 9;
	}
	else if (v < 280){
		adjust = -256;
		len = 7;
	}
	else{
		adjust = -88;
		len = 8;
	}
	if (nb)
		*nb = len;
	return reverse_bits(v + adjust, len);
}

// Look up the Huffman code value from the fixed distance Huffman tree
	// "Code" to "Lit Value" in 3.2.6 final paragraph
int h_tree_lookup_fd(unsigned char** byte, int* bit){
	return reverse_bits(read_bits32(byte, bit, 5), 5);
}

// Get the Huffman code from the fixed distance code value
	// "Lit Value" to "Code" in 3.2.6 final paragraph
h_code code_fd(unsigned int v){ // Number of bits ('nb') is always 5
	return reverse_bits(v, 5);
}

// Look up the Huffman code value from the Huffman tree 'h'
int _h_tree_lookup(const struct h_tree_head* h, unsigned char** byte, int* bit){
	if (h->sz == H_TREE_SZ_FL){ // fixed Huffman tree for literal/length
		return h_tree_lookup_fl(byte, bit);
	}
	else if (h->sz == H_TREE_SZ_FD){ // fixed Huffman tree for distance
		return h_tree_lookup_fd(byte, bit);
	}
	else{ // dynamic Huffman tree
		return h_tree_lookup(h, byte, bit);
	}
}

void form_h_tree(struct h_tree_head* h, struct code_len* cls){
	// 3.2.2 procedure
	int i, start, prev_len_count = 0;
	h_code code = 0;
	// cls is already sorted by len and has terminating entry (with len > MAX_CODE_LEN); skip over zeros (aren't to be represented in tree)
	for (i = 0; cls[i].len == 0; i++); // FUTURE: realloc away unused (zero len) entries?
	while (cls[i].len <= MAX_CODE_LEN){ // while not at last entry in 'cls' array
		start = i;
		code = (code + prev_len_count) << 1; // begin at the next length, incrementing the old prefix
		for (; cls[i].len == cls[start].len; i++){ // loop through the entries with this length
			// continue to increment the code being added (code + i - start)
			h_tree_add(h, code + i - start, cls[start].len, cls[i].val);
		}
		prev_len_count = i - start; // update the number of codes with the previous length
	}
}

// Create the dynamic Huffman tree for the code length alphabet
void form_d1(struct h_tree_head* h, unsigned char** byte, int* bit, int hclen){
	struct code_len cls[hclen + 1];
	int i;
	memcpy(cls, D1_INIT_CODE_LENS, (hclen + 1) * sizeof(struct code_len));
	h_tree_init(h, hclen);
	
	// read the code lengths for the code length alphabet; each is 3 bits and there are 'hclen' of them
	for (i = 0; i < hclen; i++){
		cls[i].len = read_bits32(byte, bit, 3);
	}
	cls[hclen].len = MAX_CODE_LEN + 1;
	qsort(cls, hclen, sizeof(struct code_len), code_len_cmp);
	form_h_tree(h, cls);
}

// Create the dynamic Huffman tree for the literal/length (h2) and distance (h3) alphabets
int form_d2(const struct h_tree_head* h1, struct h_tree_head* h2, struct h_tree_head* h3, unsigned char** byte, int* bit, int hlit, int hdist){
	int ret = 0, i, cl, top, val;
	struct code_len* cls;
	unsigned int b = 0; // number of code length entries in a row to set to this code length
	if ((cls = malloc((hlit + 1) * sizeof(struct code_len))) == NULL) // also sufficient for h3 since hlit > hdist, always
		fail_out(E_MALLOC);
	h_tree_init(h2, hlit);
	h_tree_init(h3, hdist);
	cl = -1;
	// 3.2.7 code length procedure
	top = hlit; // static upper bound for code length entries
form_d2_1:
	for (i = val = 0; i < top;){
		ret = _h_tree_lookup(h1, byte, bit); // read from the code length Huffman tree (0 - 18)
		if (ret < 16){ // < 16; literal code length
			cl = ret;
			b = 1;
		}
		else{
			if (ret == 16){ // == 16; copy this code length 3 - 6 times depending on next 2 bits
				if (cl < 0) // no current code length
					fail_out(E_HUFINV);
				b = read_bits32(byte, bit, 2) + 3;
			}
			else if (ret == 17){ // == 17; repeat code length 0 3 - 10 times depending on next 3 bits
				cl = 0;
				b = read_bits32(byte, bit, 3) + 3;
			}
			else if (ret == 18){ // == 18; repeat code length 0 11 - 138 times depending on next 7 bits
				cl = 0;
				b = read_bits32(byte, bit, 7) + 11;
			}
			else
				fail_out(E_HUFVAL);
		}
form_d2_2:
		for (; b > 0 && i < top; b--, val++, i++){ // set the code length to the 'b' consecutive entries
			cls[i].val = val;
			cls[i].len = cl;
		}
	}
	cls[top].len = MAX_CODE_LEN + 1; // indicates end of the array
	qsort(cls, top, sizeof(struct code_len), code_len_cmp);
	if (top == hlit){
		form_h_tree(h2, cls); // create the literal/length Huffman tree
		top = hdist;
		if (b == 0) // build h3 code length array; don't reset 'cl' since the current code length "carries over"
			goto form_d2_1;
		else{ // rle extends beyond hlit; reset but continue filling in with this code length
			i = val = 0;
			goto form_d2_2;
		}
	}
	form_h_tree(h3, cls); // create the distance Huffman tree
	free(cls);
}

// Read the compressed data and decompress it using the Huffman trees
int do_decompress(struct deflate_decompr* dec, const struct h_tree_head* h2, const struct h_tree_head* h3, unsigned char** byte, int* bit){
	// continue 3.2.3 procedure after compression mode resolved
	int ret = 0, len, dist;
	for (;;){
		ret = _h_tree_lookup(h2, byte, bit);
		if (ret < 0)
			goto fail;
		if (ret == 256) // end of block symbol
			break;
		if (ret < 256) // literal byte
			if ((ret = decompr_write_char(dec, (unsigned char)ret)) < 0)
				goto fail; // really pass
		else if (ret < 286){ // len/dist pairs
			// "Code" and "Extra Bits" to "Length" in 3.2.5 Table 1
			if (ret < 265)
				len = ret - 254; // no extra bits
			else if (ret < 285){
				len = (ret - 261) / 4;
				len = (1 << len + 2) + 3 // starting length of this extra bits group
					+ ((ret - 261) % 4) * (1 << len) // offset to starting length of x in this group
					+ read_bits32(byte, bit, len); // offset into range of lengths of this x given by bits
			}
			else if (ret == 285)
				len = 258; // no extra bits
			else
				fail_out(E_HUFINV);
			// "Code" and "Extra Bits" to "Distance" in 3.2.5 Table 2
			ret = _h_tree_lookup(h3, byte, bit);
			if (ret < 0)
				goto fail;
			if (ret < 4)
				dist = ret + 1; // no extra bits
			else if (ret < 30){
				dist = (ret - 2) / 2;
				dist = (1 << dist + 1) + 1 // starting distance of this extra bits group
					 + (ret % 2) * (1 << dist) // offset to starting distance of x in this group
					 + read_bits32(byte, bit, dist); // offset into range of distances of this x given by bits
			}
			else
				fail_out(E_HUFINV);
			if (dist > dec->sz || dist > dec->sliding_window)
				fail_out(E_HUFDIS);
			if ((ret = decompr_write_str(dec, dec->d + dec->sz - dist, len)) < 0)
				goto fail;
		}
		else
			fail_out(E_HUFVAL);
	}
fail:
	return ret;
}

// Decompress a deflate block into dec.d starting at bit *bit of *byte and not exceeding rm bytes
int deflate_block(struct deflate_decompr* dec, unsigned char** byte, int* bit, size_t rm){
	// continuing 3.2.3 procedure at line 2
	int ret = 0, i, bfinal, btype;
	unsigned char* orig_byte; // save the original byte pointer
	unsigned short len, nlen; // length, 1's complement length
	unsigned int hlit, hdist, hclen;
	struct h_tree_head h1, h2, h3;
	orig_byte = *byte;
	if (read_bits32(byte, bit, 1)) // BFINAL means this is the last block
		bfinal = 1;
	btype = read_bits32(byte, bit, 2);
	switch (btype){ // BTYPE is the type of the block
		case 0: // uncompressed
			byte_roundup(*byte, *bit);
			if (byte + 4 - orig_byte < rm) // check if block is big enough
				fail_out(E_ZBSZ);
			memcpy(&len, byte[0], 2);
			memcpy(&nlen, byte[2], 2);
			*byte += 4;
			if (len != ~nlen) // check ones' complement
				fail_out(E_ZNLEN);
			decompr_write_str(dec, *byte, len)
			*byte += len;
			break;
		case 2: // dynamic Huffman codes
			i = read_bits32(byte, bit, 14);
			hlit = MASK(i, 0, 5) + 257;
			hdist = MASK(i, 5, 10) + 1;
			hclen = MASK(i, 10, 14) + 4;
			if (hlit > 286)
				fail_out(E_ZINV); // others fine due to capping at #bit max
			form_d1(&h1, byte, bit, hclen);
			form_d2(&h1, &h2, &h3, byte, bit, hlit, hdist);
			goto decomp;
		case 1: // fixed Huffman codes
			h1.tree = NULL;
			h2.tree = NULL;
			h2.sz = H_TREE_SZ_FL;
			h3.tree = NULL;
			h3.sz = H_TREE_SZ_FD;
decomp:
			if ((ret = do_decompress(dec, &h2, &h3, byte, bit)) < 0)
				goto fail;
			break;
		default:
			fail_out(E_ZBTYPE); // 3 is reserved
	}
	if (bfinal)
		ret = bfinal; // bfinal means ret must be 1 to terminate the deflate_block loop in the caller
fail:
	h_tree_deinit(&h1);
	h_tree_deinit(&h2);
	h_tree_deinit(&h3);
	return ret;
}

void deflate_decompress_header(struct deflate_decompr* dec, unsigned char** _byte, unsigned char* cap){
	unsigned char cinfo;
	unsigned char* byte = *_byte;
	if (cap - byte < 2) // need room for at least CMF byte and FLG byte
		fail_out(E_ZHEAD);
	if ((((unsigned short)byte[0] << 8) | (unsigned short)byte[1]) % 31) // need CMF*256 + FLG to be a multiple of 31
		fail_out(E_ZFCHCK);
	if (byte[0] & 0xf != 8) // need compression method (cm) of 8 (deflate)
		fail_out(E_ZCMPMT);
	cinfo = (byte[0] >> 4) & 0x0f;
	// cinfo is log2(sliding window) - 8
	if (cinfo > 7) // need sliding window <= 32768
		fail_out(E_ZSLWIN);
	dec->sliding_window = 1ULL << (cinfo + 8);
	if (byte[1] & 0x20){ // preset dictionary
		// FUTURE: I don't have any knowledge of existing dictionaries, and I REALLY shouldn't need to for png files...
		fail_out(E_ZPDICT);
		// otherwise, *_byte += 6 + extra bytes;
	}
	// FLEVEL not needed
	*_byte += 2;
}

// Decompresses the data from 'compr_dat' into 'decompr_dat' with options 'ops'
int deflate_decompress(struct string_len* decompr_dat, struct string_len* compr_dat, int ops){
	int ret = 0;
	struct deflate_decompr dec;
	unsigned char* byte, * cap;
	unsigned int a32;
	int bit = 0;
	decompr_dat->str = NULL; // poison values if error
	decompr_dat->len = 0;
	
	if (compr_dat->len == 0) // no data, skip
		goto fail;
	if ((dec.d = malloc(DEFLATE_DECOMP_INIT_SZ)) == NULL)
		fail_out(E_MALLOC);
	dec.sz = DEFLATE_DECOMP_INIT_SZ;
	byte = compr_dat->str;
	cap = byte + compr_dat->len - sizeof(unsigned int); // take off adler32
	// header
	deflate_decompress_header(&dec, &byte, cap);
	// blocks
	// 3.2.3 procedure
	while (byte < cap){
		ret = deflate_block(&dec, &byte, &bit, cap - byte)
		if (ret < 0)
			goto fail;
		else if (ret == 1)
			break;
	}
	// footer
	if (ops | DEFLATE_NULLTERM && dec.d[dec.sz - 1] != 0) // write \0 if options say so
		decompr_write_char(&dec, 0);
	realloc(dec.d, dec.sz); // shouldn't fail because reducing size
	memcpy(&a32, cap, sizeof(unsigned int));
	if (adler32(dec.d, dec.sz) != a32)
		fail_out(E_ZADL32);
	decompr_dat->str = dec.d;
	decompr_dat->len = dec.sz;
fail:
	return ret;
}
