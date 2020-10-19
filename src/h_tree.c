#include <stdlib.h>
#include "include/globals.h"
#include "include/h_tree.h"
#include "include/global_errors.h"
#include "include/deflate_errors.h"
#include "include/aht.h"
#include "include/deflate.h"

#define H_TREE_REP(x) (-(x) - 1)

// Initialize the (dynamic) Huffman tree 'h'
void h_tree_init(struct h_tree_head* h, int sz){
	if ((h->tree = calloc(sz, sizeof(struct h_tree_node))) == NULL)
		fail_out(E_MALLOC);
	h->sz = 0;
}

// Deinitialize the Huffman tree 'h'
void h_tree_deinit(struct h_tree_head* h){
	freec(h->tree);
}

// Look up the Huffman code value from the Huffman tree 'h'
int h_tree_lookup(struct h_tree_head* h, unsigned char** byte, int* bit){
	int v;
	struct h_tree_node* t = h->tree;
	for (;;){
		if (read_bits32(byte, bit, 1) & H_CODE_1)
			v = t->left;
		else
			v = t->right;
		if (v < 0)
			return H_TREE_REP(v);
		else if (v == 0)
			break;
		t = h->tree + v;
	}
	fail_out(E_HUFINV);
	return 0;
}

// Adds the Huffman code 'c' of code length 'codelen' that encodes non-negative value 'val' to the Huffman tree 'h'
void h_tree_add(struct h_tree_head* h, h_code c, int codelen, int val){
	short i = 0;
	struct h_tree_node* t;
	short* v = &i; // point to zero to start
	if (c >= H_CODE_1 << codelen) // Huffman code is bigger than its codelen
		fail_out(E_HUFINV);
	for (; i < codelen; i++, c >>= 1){
		if (*v < 0){
			fail_out(E_HUFAMB);
		}
		else if (*v == 0){ // new node; point the branch to the next spot at the end of the array
			*v = h->sz++;
		}
		// follow node
		t = h->tree + *v;
		if (c & H_CODE_1)
			v = &t->right;
		else
			v = &t->left;
	}
	// check that leaf is empty or that the correct val is already there
	if (*v != 0 && *v != H_TREE_REP(val)){
		fail_out(E_HUFAMB);
	}
	*v = H_TREE_REP(val);
}

int h_tree_d_lens(struct htbq* htn, struct aht* aht0, struct aht* aht1, struct hlit_hdist_hclen* ldc){
	int i, j, h0, hlit, hdist, d;
	int bit_count = 5 + 5 + 4 + 4 * 3; // HLIT, HDIST, HCLEN, initial HCLEN codes
	for (i = 0; i < 19; i++){
		htn[i].val = i;
	}
	for (h0 = NUM_DIST_CODES - 1; h0 >= 1 && aht1->tree[h0].depth == 0; h0--); // get HDIST from this
	hdist = h0 + 1;
	for (h0 = NUM_LITLEN_CODES - 1; h0 >= 257 && aht0->tree[h0].depth == 0; h0--); // get HLIT from this
	hlit = ++h0;
	
	// leave h0 as hlit for the first run (lit/len); set to h_dist for the second run (dist)
	for (i = 0; i < h0; i++){
		d = aht0->tree[i].depth;
		for (j = i + 1;; j++){ // i is the RLE base; j is the RLE bound
			if (j == h0){
				if (h0 == hlit){
					// switch over to dist aht
					j = 0;
					i -= h0; // shift i down along with j
					h0 = hdist;
					aht0 = aht1;
				}
				else{
					goto finish_off;
				}
			}
			if (aht0->tree[j].depth != d){ // loop proceeds until the char changes or it runs to the end
finish_off:
				if (j - i >= 3 && d == 0){
					// 17, 18
					while (j - i >= 11){
						i += min(j - i, 138);
						htn[18].weight++;
						bit_count += 7;
					}
					if (j - i > 1){
						htn[17].weight++;
						bit_count += 3;
					}
					else if (j - i == 1){
						htn[0].weight++;
					}
				}
				else{
					htn[d].weight++;
					i++;
					// 16
					while (j - i >= 3){
						i += min(j - i, 6);
						htn[16].weight++;
						bit_count += 2;
					}
					for (; i < j; i++){
						htn[d].weight++;
					}
				}
				i = j - 1;
				break;
			}
		}
	}
	// HCLEN found by the last nonzero frequency code length
	for (i = 15, j = 15; i > 4 && htn[j].weight == 0; i--){
		j += (j < 8)? i - 1 : -(i - 1);
	}
	if (ldc){
		ldc->hlit = hlit - 257;
		ldc->hdist = hdist - 1;
		ldc->hclen = i - 4;
	}
	bit_count += i * 3; // remaining HCLEN codes, each 3 bits long
	return bit_count;
}

void h_tree_builder_init(struct h_tree_builder* htb, int sz){
	h_tree_init(&htb->head, sz);
	htb->q = calloc(sz, sizeof(struct htbq));
	if (!htb->q){
		fail_out(E_MALLOC);
	}
	htb->weights = calloc((sz + 1), sizeof(int));
	if (!htb->weights){
		fail_out(E_MALLOC);
	}
	htb->cap = sz;
	htb->h0 = -1;
	htb->h1 = htb->t1 = 0;
}

void h_tree_builder_deinit(struct h_tree_builder* htb){
	h_tree_deinit(&htb->head);
	freec(htb->q);
	freec(htb->weights);
}

void h_tree_builder_reset(struct h_tree_builder* htb){
	memset(htb->weights, 0, (htb->cap + 1) * sizeof(unsigned int));
	memset(htb->q, 0, htb->cap * sizeof(struct htbq));
	htb->h0 = -1;
	htb->h1 = htb->t1 = 0;
}

static int htbq_comp(const void* a, const void* b){
	// sort by weight, then by val
	int ret = (int)((struct htbq*)a)->weight - ((struct htbq*)b)->weight;
	if (ret == 0){
		ret = (int)((struct htbq*)a)->val - ((struct htbq*)b)->val;
	}
	return ret;
}

static inline unsigned int h_tree_builder_peek0(struct h_tree_builder* htb){
	if (htb->h0 < htb->cap){
		return htb->q[htb->h0].weight;
	}
	else{
		return (unsigned int)-1;
	}
}

static inline int h_tree_builder_pop0(struct h_tree_builder* htb){
	htb->weights[htb->t1] += htb->q[htb->h0].weight;
	return htb->h0++;
}

static inline unsigned int h_tree_builder_peek1(struct h_tree_builder* htb){
	if (htb->h1 >= 0 && htb->h1 < htb->t1){
		return htb->weights[htb->h1];
	}
	else{
		return (unsigned int)-1;
	}
}

static inline int h_tree_builder_pop1(struct h_tree_builder* htb){
	htb->weights[htb->t1] += htb->weights[htb->h1];
	return htb->h1++;
}

static inline void h_tree_builder_push(struct h_tree_builder* htb, int l, int r){
	htb->head.tree[htb->t1].left = l;
	htb->head.tree[htb->t1].right = r;
	htb->head.sz++;
	htb->t1++;
}

void h_tree_builder_build(struct h_tree_builder* htb){
	unsigned int p0, p1;
	int i0, i1;
	qsort(htb->q, htb->cap, sizeof(struct htbq), htbq_comp);
	for (htb->h0 = 0; htb->q[htb->h0].weight == 0; htb->h0++);
	for (;;){
		p0 = h_tree_builder_peek0(htb);
		p1 = h_tree_builder_peek1(htb);
		if (p0 < p1){ // take leaf
			i0 = H_TREE_REP(h_tree_builder_pop0(htb));
			p0 = h_tree_builder_peek0(htb);
			if (p0 < p1){ // take leaf
				i1 = H_TREE_REP(h_tree_builder_pop0(htb));
			}
			else{
				if (p1 == (unsigned int)-1){ // just root and one leaf
					// TODO what should I do here?
				}
				else{ // take non-leaf
					i1 = h_tree_builder_pop1(htb);
				}
			}
		}
		else{ // take non-leaf
			i0 = h_tree_builder_pop1(htb);
			p1 = h_tree_builder_peek1(htb);
			if (p0 < p1){ // take leaf
				i1 = H_TREE_REP(h_tree_builder_pop0(htb));
			}
			else{
				if (p1 == (unsigned int)-1){ // only that non-leaf remains; it is the root
					break;
				}
				i1 = h_tree_builder_pop1(htb); // take non-leaf
			}
		}
		h_tree_builder_push(htb, i0, i1);
	}
}

static unsigned int h_tree_builder_score_helper(struct h_tree_builder* htb, struct h_tree_node* htn, int depth){
	unsigned int ret = 0;
	if (htn->left < 0){
		ret += htb->q[H_TREE_REP(htn->left)].weight * depth;
	}
	else{
		ret += h_tree_builder_score_helper(htb, htb->head.tree + htn->left, depth + 1);
	}
	if (htn->right < 0){
		ret += htb->q[H_TREE_REP(htn->right)].weight * depth;
	}
	else{
		ret += h_tree_builder_score_helper(htb, htb->head.tree + htn->right, depth + 1);
	}
	return ret;
}

unsigned int h_tree_builder_score(struct h_tree_builder* htb){
	return h_tree_builder_score_helper(htb, htb->head.tree + htb->t1 - 1, 1);
}
