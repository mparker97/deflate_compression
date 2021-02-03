// http://www.integpg.com/deflate-compression-algorithm/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "include/globals.h"
#include "include/deflate.h"
#include "include/deflate_ext.h"
#include "include/aht.h"
#include "include/deflate_errors.h"
#include "include/h_tree.h"

#define DUP_HT_SZ 1024

/*
The data space 'd' points to a region kept at a size of 2 * 'sliding_window' + 2.
	The justification behind the size has two factors.
		First, there are two sliding windows in order to search amongst the previous sliding window for potential dup strings.
		Rather than reading in a new character each iteration, an entire new sliding window is read in once the current one
			is exhausted. The two sliding windows are rotated; as the current one is being processed, the old one is being
			overwritten one char at a time since its chars are out of range (sliding_window) for dup strings.
		Second, the hash function uses the current char plus the two subsequent chars. Thus, in fact 'sliding_window' + 2 chars
			must be read in so that these are present. There are 2 extra spaces ('A', 'B') after the current sliding window
			for spillover of these two additionaly read chars.
			
		d                                 e
		+=================================+=================================+===+===+
		|      former sliding window      |      current sliding window     | A | B |
		+=================================+=================================+===+===+
		
		|-------- sliding_window ---------|-------- sliding_window ---------|-1-|-1-|

The two regions can be represented by pointers 'd' and 'e'. The 'e' window is current, and the 'd' window is former.
'sliding_window' + 2 bytes of data are initially read into 'e'. Each char of the current sliding window is copied to its
	relative position in the former sliding window after it is processed. When the current sliding window is fully processed,
	'A' and 'B' are copied into the first two bytes of 'e', and then the next 'sliding_window' bytes are copied in,
	filling up the remaining window and the two spillover bytes once again. At this point, the result is that
	the entire region, from d to B, is a contiguous segment of the source file.

The hash table, 'dup_ht', has DUP_HT_SZ struct dup_hash_entry elements, which are each heads of hash chains.
	Each contains 'ptr', an index into 'dup_entries', and 'len', the length of the hash chain.

'dup_entries' is a space of 'sliding_window' shorts; each is an index again into 'dup_entries', thus forming a hash chain.
	There is no special value for the end of the chain since this cannot be updated to the previous entry in the chain
		indefinitely without making it doubly linked, so the chain stops according to the 'len' field of the 'dup_ht' entry.

The hash table is updated with each char in the current sliding window read. Only the most recent 'sliding_window'
	chars are kept; the newest char overwrites the oldest char's spot in 'dup_entries'.
	The hash at the old char is calculated in order to decrement its hash chain 'len' by 1 (effectively trimming it off).
	The hash at the new char is calculated in order to increment its hash chain 'len' by 1 and also prepend that dup entry to
		the hash chain. This ensures every char being replaced is the last entry in its hash chain and can thus be trimmed easily.

The hash function operates on the current and subsequent two chars. When searching through possible dup strings, simply
	call check_dup_str on the current char* and the char* obtained by
		- adding the index of the element in the hash chain to e, if the dup entry came from the current sliding window
			(its index is less than the current char's index)
		- subtracting the index of the element in the hash chain from d + the current char's index, if the dup entry
			came from the former sliding window (its index is greater than or equal to the current char's index).
	The maximum dup length is 258; if the current string manages to successfully match a dup string up to the end of the sliding
		window but has not reached 258 chars in length, 258 bytes from the next sliding window are read in ahead to continue
		checking. The boolean 'read_ahead' is set to indicate this, so that the next 'sliding_window' read-in can start ahead by
		those 258 bytes.
*/

struct dup_hash_entry{
	swi ptr;
	swi len;
};

struct deflate_compr{ // typedef in include/deflate_ext.h
	struct aht ll_aht, d_aht;
	FILE* f;
	unsigned char* d, *e;
	swi* dup_entries;
	struct dup_hash_entry* dup_ht;
	unsigned char* bound;
	int sliding_window;
	unsigned char read_ahead; // bool
	unsigned char done;
};

SPAWNABLE(deflate_compr_t);

void deflate_compr_init(deflate_compr_t* com, const char* file_name, swi sliding_window_sz){
	if (!(com->d = malloc(com->sliding_window * 2 + 2))){
		fail_out(E_MALLOC);
	}
	aht_init(&com->ll_aht, NUM_LITLEN_CODES);
	aht_init(&com->d_aht, NUM_DIST_CODES);
	if (!(com->dup_entries = malloc(com->sliding_window * sizeof(swi)))){
		fail_out(E_MALLOC);
	}
	if (!(com->dup_ht = calloc(DUP_HT_SZ, sizeof(struct dup_hash_entry)))){
		fail_out(E_MALLOC);
	}
	com->f = fopen(file_name, "r");
	if (com->f == NULL){
		fail_out(E_NEXIST);
	}
	com->sliding_window = sliding_window_sz;
	com->e = com->d + com->sliding_window;
	com->read_ahead = 0;
	com->done = 0;
}

void deflate_compr_deinit(deflate_compr_t* com){
	free(com->d);
	free(com->ll_aht.tree);
	free(com->d_aht.tree);
	free(com->dup_entries);
	free(com->dup_ht);
	fclose(com->f);
}

// Hash table uses a hash function based on the first three characters of the dup string
short dup_hash(const unsigned char* p){
	// Interleave bits of p[0], p[1], and p[2]
	// Thanks for your code https://stackoverflow.com/a/1024889
	int x, y, z;
	x = p[0];
	x = (x | (x << 16)) & 0x000000FF;
	x = (x | (x << 8)) & 0x0000F00F;
	x = (x | (x << 4)) & 0x000C30C3;
	x = (x | (x << 2)) & 0x00249249;
	y = p[1];
	y = (y | (y << 16)) & 0x000000FF;
	y = (y | (y << 8)) & 0x0000F00F;
	y = (y | (y << 4)) & 0x000C30C3;
	y = (y | (y << 2)) & 0x00249249;
	z = p[2];
	z = (z | (z << 16)) & 0x000000FF;
	z = (z | (z << 8)) & 0x0000F00F;
	z = (z | (z << 4)) & 0x000C30C3;
	z = (z | (z << 2)) & 0x00249249;
	return (x | (y << 1) | (z << 2)) % DUP_HT_SZ;
}

// TODO: figure out bound for reading in and stuff

void fetch(deflate_compr_t* com, unsigned char* p, swi len){
	int ret = fread(p, sizeof(unsigned char), len, com->f); // TODO: get bytes here
	com->bound = p + ret;
	if (!ret){
		com->done = 1;
	}
}

void fetch_sliding_window(deflate_compr_t* com){
	if (com->read_ahead){
		fetch(com, com->e + MAXLEN, com->sliding_window + 2 - MAXLEN);
	}
	else{
		fetch(com, com->e + 2, com->sliding_window);
	}
}

void fetch_ahead(deflate_compr_t* com){
	fetch(com, com->e + 2, MAXLEN - 2);
	com->read_ahead = 1;
	//com->e[0] = com->e[com->sliding_window];
	//com->e[1] = com->e[com->sliding_window + 1];
}

// Returns the common subsequence length of the current position 'str' and the duplicate entry 'dup'
int check_dup_str(deflate_compr_t* com, const unsigned char* str, const unsigned char* dup){
	int ret = 0;
	while (str < com->bound && *(str++) == *(dup++)){
		if (++ret == MAXLEN)// || str > com->bound)
			break;
		if (str > com->e + 2 + com->sliding_window){
			if (!com->read_ahead){
				fetch_ahead(com);
				//if (com->done){ // nothing fetched; done
				//	break;
				//}
			}
			str = com->e + 2;
		}
	}
	return ret;
}

int get_len_code(int x, int* peb, int* pebits){
	// "Length" to "Code", "Extra Bits", and offset in 3.2.5 Table 1
	int eb = 0;
	if (x < 11){
		x += 254;
	}
	else if (x == 258){
		x = 285;
	}
	else{
		eb = 28 - __builtin_clz(x - 3);
		if (pebits)
			*pebits = (x - 3) & ((1 << eb) - 1);
		x = 261 + eb * 4 + (x - 3) / (1 << (eb + 1));
	}
	if (peb)
		*peb = eb;
	return x;
}

int get_dist_code(int x, int* peb, int* pebits){
	// "Distance" to "Code", "Extra Bits", and offset in 3.2.5 Table 2
	int eb = 0;
	if (x < 5){
		x--;
	}
	else{
		eb = 31 - __builtin_clz(x - 1);
		if (pebits)
			*pebits = (x - 1) & ((1 << (eb - 1)) - 1);
		x = eb * 2 + (((x - 1) >> (eb - 1)) & 1);
	}
	if (peb)
		*peb = eb - 1;
	return x;
}

void process_loop(deflate_compr_t* com, struct h_tree_builder* htb){
	int i, j, k, t; // i and j are loop iterators, k is the total processed byte count, t is a scratch variable
	int c; // offset of dup, taken from com->d
	
	struct dup_hash_entry* dh;
	swi hash;
	int dup_carry_over; // number of characters wrapping around to the next sliding window, which must be copied separately
	
	int max_len; // maximum dup match length found from the hash chain
	int max_idx; // maximum dup match index found from the hash chain
	int first_window = 1; // bool to treat com->d as invalid for the first sliding window
	#ifdef _TEST_CHECK_LLD
	int sc, sc2; // scores; could do without
	#endif
	
	// insert end of block token (256) into ll_aht immediately, since it will always be there once
	aht_insert(&com->ll_aht, 256);
	k = 1;
	
	fetch(com, com->e, 2);
	for (i = 0;;){
		fetch_sliding_window(com); // read next sliding window into 'e' + 2
		if (com->done) // finished
			break;
		com->read_ahead = 0;
		for (; i < com->bound - com->e;){ // for each character in sliding window
			hash = dup_hash(com->e + i);
			dh = com->dup_ht + hash;
			hash = dh->ptr; // hash now maintains the hash chain element index
			max_len = 2; // need at least 3 to make len/dist worth it
			max_idx = -1;
			for (j = 0; j < dh->len; j++){ // loop through hash chain
				if (hash < i){ // element is within this sliding window
					c = hash + com->sliding_window;
				}
				else{ // element is within previous sliding window
					c = hash;
				}
				// check for dup string and save if it's the longest
				t = check_dup_str(com, com->e + i, com->d + c);
				if (t > max_len){
					max_len = t;
					max_idx = c;
				}
				hash = com->dup_entries[hash]; // proceed to next hash element
			}
			
			//printf("%d bytes processed. ", k + 1);
			dup_carry_over = 0;
			if (max_len < 3){
				j = i + 1;
				//printf("Literal %c (%d)\n", com->e[i], com->e[i]);
				// TODO: write literal
				aht_insert(&com->ll_aht, com->e[i]);
				max_len = 1;
			}
			else{
				max_idx = com->e + i - (com->d + max_idx); // now distance
				//printf("Len: %d, dist: %d\n", max_len, max_idx);
				
				aht_insert(&com->ll_aht, get_len_code(max_len, NULL, NULL));
				aht_insert(&com->d_aht, get_dist_code(max_idx, NULL, NULL));
				// TODO: write len/dist pair
				
				if (i + max_len > com->sliding_window){ // prevent overflow
					dup_carry_over = i + max_len - com->sliding_window;
					j = com->sliding_window; // copy from i up to sliding_window, then copy from com->e up to dup_carry_over
				}
				else{
					j = i + max_len;
				}
			}
			
			h_tree_builder_reset(htb);
			#ifdef _TEST_CHECK_LLD
			sc = h_tree_d_lens(htb->q, &com->ll_aht, &com->d_aht, NULL);
			#endif
			h_tree_builder_build(htb);
			#ifdef _TEST_CHECK_LLD
			sc += h_tree_builder_score(htb);
			//printf("%d, %d, %d, %d, %f\n", k + max_len, sc, com->ll_aht.score, com->d_aht.score, (double)(com->ll_aht.score + com->d_aht.score + sc) / (k + max_len));
			//printf("%d, %d\n", k + max_len, com->ll_aht.score + com->d_aht.score + sc);
			// number of bytes processed, lit/len, 0/dist, number of bits in Huffman trees, number of bits from compressed data
			if (max_len < 3)
				printf("%d, %d, %d, %d, %d\n", k + max_len, com->e[i], 0, sc, com->ll_aht.score + com->d_aht.score);
			else
				printf("%d, %d, %d, %d, %d\n", k + max_len, max_len, max_idx, sc, com->ll_aht.score + com->d_aht.score);
			#endif
			
			
			// update sliding window structures
repeat_copy:
			for (; i < j; i++, k++){
				// append to new chain
				dh = com->dup_ht + dup_hash(com->e + i);
				com->dup_entries[i] = dh->ptr;
				dh->ptr = i;
				dh->len++;

				// decrement old chain length
				if (!first_window){
					com->dup_ht[dup_hash(com->d + i)].len--;
				}
				com->d[i] = com->e[i]; // copy char to old sliding window
			}
			if (dup_carry_over > 0){ // dup match goes beyond sliding window; copy chars into beginning of next sliding window
				i = 0;
				j = dup_carry_over;
				dup_carry_over = -1; // ensures control flow will fall into the "else if" next time
				com->e[0] = com->e[com->sliding_window];
				com->e[1] = com->e[com->sliding_window + 1];
				// if the string breached past sliding window + 1, fetch_ahead had brought in the chars past e + 1
				goto repeat_copy;
			}
			else if (dup_carry_over < 0){ // did a wrap around, break out to move to the next sliding window
				break;
			}
			
		}
		if (!com->read_ahead){ // did not breach next sliding window; reset i to the beginning for next sliding window
			com->e[0] = com->e[com->sliding_window];
			com->e[1] = com->e[com->sliding_window + 1];
			i = 0;
		}
		first_window = 0;
	}
}

int main(){ // dummy main that will 100% segfault
	deflate_compr_init(NULL, NULL, 1);
	process_loop(NULL, NULL);
	return 0;
}

/*
int deflate_compress(struct string_len* compr_dat, struct string_len* decompr_dat, int ops){
	deflate_compr_t com;
	// TODO: get sliding window
	com.sliding_window = sliding_window;
	deflate_compr_init(&com);
	
}
*/
