// http://www.integpg.com/deflate-compression-algorithm/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "include/globals.h"
#include "include/deflate.h"
#include "include/aht.h"
#include "include/deflate_errors.h"
#include "include/deflate.h"
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
'sliding_window' + 2 bytes of data are initially read into 'd'. Each char of the current sliding window is copied to its
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

typedef unsigned short swi; // sliding window index
typedef unsigned short zdist; // distance
typedef unsigned char zlen; // length

struct dup_hash_entry{
	swi ptr;
	swi len;
};

struct deflate_compr{
	struct aht ll_aht, d_aht;
	
	unsigned char* d, *e;
	swi* dup_entries;
	struct dup_hash_entry* dup_ht;
	unsigned char* bound;
	int sliding_window;
	unsigned char read_ahead; // bool
	unsigned char done;
};

static FILE* f;

void deflate_compr_init(struct deflate_compr* com){
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
	com->e = com->d + com->sliding_window;
	com->read_ahead = 0;
	com->done = 0;
}

void deflate_compr_deinit(struct deflate_compr* com){
	free(com->d);
	free(com->ll_aht.tree);
	free(com->d_aht.tree);
	free(com->dup_entries);
	free(com->dup_ht);
}

// Hash table uses a hash function based on the first three characters of the dup string
short dup_hash(unsigned char* p){
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

void fetch(struct deflate_compr* com, unsigned char* p, swi len){
	int ret = fread(p, sizeof(unsigned char), len, f); // TODO: get bytes here
	com->bound = p + ret;
	if (!ret){
		com->done = 1;
	}
}

void fetch_sliding_window(struct deflate_compr* com){
	if (com->read_ahead){
		fetch(com, com->e + MAXLEN, com->sliding_window + 2 - MAXLEN);
	}
	else{
		fetch(com, com->e + 2, com->sliding_window);
	}
}

void fetch_ahead(struct deflate_compr* com){
	fetch(com, com->e + 2, MAXLEN - 2);
	com->read_ahead = 1;
	com->e[0] = com->e[com->sliding_window];
	com->e[1] = com->e[com->sliding_window + 1];
}

// Returns the common subsequence length of the current position 'str' and the duplicate entry 'dup'
int check_dup_str(struct deflate_compr* com, unsigned char* str, unsigned char* dup){
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

void process_loop(struct deflate_compr* com, struct h_tree_builder* htb){
	int i, j, k, t;
	int c; // offset of dup, taken from com->d
	
	struct dup_hash_entry* dh;
	swi hash_new;
	int dup_carry_over;
	
	int max_len;
	int max_idx;
	int first_window = 1;
	int sc, sc2;
	
	fetch(com, com->e, 2);
	for (i = k = 0;;){
		fetch_sliding_window(com); // read next sliding window into 'e' + 2
		if (com->done) // finished
			break;
		com->read_ahead = 0;
		for (; i < com->bound - com->e;){ // for each character in sliding window
			hash_new = dup_hash(com->e + i);
			dh = com->dup_ht + hash_new;
			hash_new = dh->ptr; // hash_new now maintains the hash chain element index
			max_len = 2; // need at least 3 to make len/dist worth it
			max_idx = -1;
			for (j = 0; j < dh->len; j++){ // loop through hash chain
				if (hash_new < i){ // element is within this sliding window
					c = hash_new + com->sliding_window;
				}
				else{ // element is within previous sliding window
					c = hash_new;
				}
				// check for dup string and save if it's the longest
				t = check_dup_str(com, com->e + i, com->d + c);
				if (t > max_len){
					max_len = t;
					max_idx = c;
				}
				hash_new = com->dup_entries[hash_new]; // proceed to next hash element
			}
			
			printf("%d bytes processed. ", k + 1);
			dup_carry_over = 0;
			if (max_len < 3){
				j = i + 1;
				printf("Literal %c (%d)\n", com->e[i], com->e[i]);
				// TODO: write literal
				aht_insert(&com->ll_aht, com->e[i]);
			}
			else{
				max_idx = com->e + i - (com->d + max_idx);
				printf("Len: %d, dist: %d\n", max_len, max_idx);
				aht_insert(&com->ll_aht, get_len_code(max_len, NULL, NULL));
				aht_insert(&com->d_aht, get_dist_code(max_idx, NULL, NULL));
				// TODO: write len/dist pair
				
				if (i + max_len > com->sliding_window + 2){ // prevent overflow
					dup_carry_over = i + max_len - com->sliding_window;
					j = com->sliding_window + 2; // copy from i up to sliding_window + 2, then copy from com->e + 2 up to dup_carry_over
				}
				else{
					j = i + max_len;
				}
			}
			
			h_tree_builder_reset(htb);
			sc = h_tree_d_lens(htb->q, &com->ll_aht, &com->d_aht, NULL);
			h_tree_builder_build(htb);
			sc2 = h_tree_builder_score(htb);
			//printf("Overhead score (codes + bits): %d + %d = %d\n", sc2, sc, sc + sc2);
			printf("%d, %d, %d, %d, %f\n", sc2, sc, com->ll_aht.score, com->d_aht.score, (double)(com->ll_aht.score + com->d_aht.score + sc + sc2) / (k + 1));
			
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
			if (dup_carry_over > 0){
				i = 2;
				j = dup_carry_over;
				dup_carry_over = -1;
				goto repeat_copy;
			}
			else if (dup_carry_over < 0){ // did a wrap around, break out to move to the next sliding window
				break;
			}
			
		}
		if (!com->read_ahead){ // copy spillover chars if haven't already
			com->e[0] = com->e[com->sliding_window];
			com->e[1] = com->e[com->sliding_window + 1];
			i = 0;
		}
		first_window = 0;
	}
}

int main(int argc, char* argv[]){
	struct deflate_compr com;
	struct h_tree_builder htb;
	if (argc != 2){
		fprintf(stderr, "USAGE: %s FILE\n", argv[0]);
		exit(1);
	}
	f = fopen(argv[1], "r");
	if (f == NULL){
		fprintf(stderr, "Failed to open file\n");
		exit(1);
	}
	com.sliding_window = 1 << 15;
	deflate_compr_init(&com);
	h_tree_builder_init(&htb, 19);
	printf("codes, ebits, ll_aht, d_aht, ratio\n");
	process_loop(&com, &htb);
	fclose(f);
}

/*
int deflate_compress(struct string_len* compr_dat, struct string_len* decompr_dat, int ops){
	struct deflate_compr com;
	// TODO: get sliding window
	com.sliding_window = sliding_window;
	deflate_compr_init(&com);
	
}
*/
