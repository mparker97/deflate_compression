#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/globals.h"
#include "include/global_errors.h"
#include "include/aht.h"

void aht_init(struct aht* aht, int sz){
	struct aht_node* ahtn;
	aht->tree = calloc(sz * 2, sizeof(struct aht_node));
	if (!aht->tree){
		fail_out(E_MALLOC);
	}
	aht->score = 0;
	aht->sz = sz;
	aht->nyt = sz;
	
	ahtn = aht->tree + sz;
	ahtn->weight = 0;
	ahtn->depth = 0;
	ahtn->parent = ahtn->left = ahtn->right = ahtn->block_next = ahtn->block_prev = -1;
}

void aht_deinit(struct aht* aht){
	freec(aht->tree);
}

static struct aht_node* aht_get_block_leader(const struct aht* aht, struct aht_node* q){
	struct aht_node* n;
	while (q->block_next >= 0){ // find leader
		n = aht->tree + q->block_next;
		if (q->weight != n->weight || ((q->left < 0) ^ (n->left < 0))){ // different weight or different class (leaf/non-leaf)
			break;
		}
		q = n;
	}
	return q;
}

static void aht_cascade_update_depth(struct aht* aht, struct aht_node* ahtn, int d){
	//#ifdef _DEBUG
	if (ahtn->depth < 0){
		fprintf(stderr, "\033[1;31maht cycle detected at node %ld\033[0m\n", ahtn - aht->tree);
		aht_print(aht);
		exit(1);
	}
	//#endif
	
	if (ahtn->left >= 0){
		//#ifdef _DEBUG
		ahtn->depth = -d;
		//#endif
		aht_cascade_update_depth(aht, aht->tree + ahtn->left, d + 1);
		aht_cascade_update_depth(aht, aht->tree + ahtn->right, d + 1);
	}
	else{
		aht->score += (d - ahtn->depth) * ahtn->weight;
	}
	ahtn->depth = d;
}

static void aht_slide(struct aht* aht, struct aht_node* n, struct aht_node* b){
	// slide node p all the way to after node b
	// require 'p' to be subordinate to 'b'
	struct aht_node* p, *orig = n;
	short b_par, prev_par;
	b_par = b->parent; // save old parent of b
	// block pointers on p side
	if (n->block_prev >= 0){
		aht->tree[n->block_prev].block_next = n->block_next;
	}
	aht->tree[n->block_next].block_prev = n->block_prev;
	// parents
	prev_par = n->parent;
	p = aht->tree + n->parent;
	while (n != b){
		if (p->right == n - aht->tree){ // prioritize right because we are advancing child pointers to the right
			p->right = n->block_next;
		}
		else{
			p->left = n->block_next;
		}
		if (aht->tree[n->block_next].depth != p->depth + 1){
			aht_cascade_update_depth(aht, aht->tree + n->block_next, p->depth + 1);
		}
		// swap n->block_next's parent and prev_par
		p = aht->tree + aht->tree[n->block_next].parent; // old parent
		aht->tree[n->block_next].parent = prev_par;
		prev_par = p - aht->tree;
		
		n = aht->tree + n->block_next;
	}
	p = aht->tree + b_par; // old parent of b becomes orig's parent
	if (p->right == b - aht->tree){
		p->right = orig - aht->tree;
	}
	else{
		p->left = orig - aht->tree;
	}
	if (orig->depth != p->depth + 1){ // cascade update only if changing it
		aht_cascade_update_depth(aht, orig, p->depth + 1);
	}
	orig->parent = b_par;
	// block pointers on b side
	if (b->block_next >= 0){
		aht->tree[b->block_next].block_prev = orig - aht->tree;
	}
	orig->block_next = b->block_next;
	orig->block_prev = b - aht->tree;
	b->block_next = orig - aht->tree;
}

static struct aht_node* aht_sai(struct aht* aht, struct aht_node* p){
	// slide and increment
	struct aht_node* b, *orig;
	unsigned int wt = p->weight;
	short s = p->parent; // internal node updates to previous parent
	
	orig = p;
	b = aht_get_block_leader(aht, p);
	if (b->block_next >= 0){ // if this fails, p is the root
    b = aht->tree + b->block_next;
		if ((p->left < 0 && b->left >= 0 && b->weight == wt) || (p->left >= 0 && b->left < 0 && b->weight == wt + 1)){
			aht_slide(aht, p, aht_get_block_leader(aht, b)); // slide past leader of the next block
		}
		if (p->left < 0){ // leaf
			aht->score += p->depth;
			s = p->parent; // leaf node updates to new parent
		}
		p = aht->tree + s;
	}
	else{
		p = NULL;
	}
	orig->weight++; // increment weight at the end so that score is properly updated in aht_slide
	return p;
}

static void aht_swap(struct aht* aht, struct aht_node* a, struct aht_node* b){ // require 'a' to be subordinate to 'b' (which implies a->block_next >= 0 and b->block_prev >= 0)
	/*
	swap 'a' and 'b'
	adj = true:
		list before: ... <-> x <-> a <-> b <-> z <-> ...
		list after:  ... <-> x <-> b <-> a <-> z <-> ...
	adj = false:
		list before: ... <-> x <-> a <-> [...y...] <-> b <-> z <-> ...
		list after:  ... <-> x <-> b <-> [...y...] <-> a <-> z <-> ...
	*/
	struct aht_node* p;
	int adj;
	short t;
	adj = a->block_next == b - aht->tree;
	// forward arrows
	t = a->block_next;
	a->block_next = b->block_next; // ----------------------------- ...     x     b     [...y...]     a  -> z     ...
	if (a->block_prev >= 0)
		aht->tree[a->block_prev].block_next = b - aht->tree; // --- ...     x  -> b     [...y...]     a  -> z     ...
	if (adj)
		b->block_next = a - aht->tree; // ----------|adj|---------- ...     x  -> b         ->        a  -> z     ...
	else{
		b->block_next = t; // ------------------------------------- ...     x  -> b  -> [...y...]     a  -> z     ...
		aht->tree[b->block_prev].block_next = a - aht->tree; // --- ...     x  -> b  -> [...y...]  -> a  -> z     ...
	}
	// backward arrows
	aht->tree[a->block_next].block_prev = a - aht->tree; // ------- ...     x  -> b  -> [...y...]  -> a <-> z     ...
	t = a->block_prev;
	if (adj)
		a->block_prev = b - aht->tree; // ----------|adj|---------- ...     x  -> b        <->        a <-> z     ...
	else
		a->block_prev = b->block_prev; // ------------------------- ...     x  -> b  -> [...y...] <-> a <-> z     ...
	b->block_prev = t; // ----------------------------------------- ...     x <-> b  -> [...y...] <-> a <-> z     ...
	if (!adj)
		aht->tree[b->block_next].block_prev = b - aht->tree; // --- ...     x <-> b <-> [...y...] <-> a <-> z     ...
	
	// parents
	p = aht->tree + a->parent;
	if (a->parent == b->parent){ // same parent, they are left/right children so just switch them, and no need to switch parents
		t = p->left;
		p->left = p->right;
		p->right = t;
	}
	else{
		if (p->right == a - aht->tree){ // a is right child; make right child b
			p->right = b - aht->tree;
		}
		else{ // a is left child; make left child b
			p->left = b - aht->tree;
		}
		p = aht->tree + b->parent;
		if (p->right == b - aht->tree){ // b is right child; make right child a
			p->right = a - aht->tree;
		}
		else{ // b is left child; make left child a
			p->left = a - aht->tree;
		}
		// swap parents
		t = a->parent;
		a->parent = b->parent;
		b->parent = t;
	}
	
	// depths
	if (a->depth != b->depth){
		aht->score += (a->depth - b->depth) * (b->weight - a->weight);
		t = a->depth;
		a->depth = b->depth;
		b->depth = t;
	}
}

static void aht_interchange_leaf(struct aht* aht, struct aht_node* q){
	struct aht_node* n = aht_get_block_leader(aht, q);
	if (n != q){ // interchange only if different
		aht_swap(aht, q, n);
	}
}

static short aht_sibling(const struct aht* aht, const struct aht_node* ahtn){
	struct aht_node* p;
	short ret;
	if (ahtn->parent < 0){ // no parent means no sibling
		ret = -1;
	}
	else{
		p = aht->tree + ahtn->parent;
		if (aht->tree + p->left == ahtn){ // this is left child; return right child
			ret = p->right;
		}
		else{ // this is right child; return left child
			ret = p->left;
		}
	}
	return ret;
}

void aht_insert(struct aht* aht, int c){
	struct aht_node* q;
	struct aht_node* l2i = NULL; // leaf to increment
	q = aht->tree + c;
	if (q->weight == 0){ // nyt
		q = aht->tree + aht->nyt; // now the new internal 0 node
		// spawn char node off of nyt's right
		q->right = q->block_prev = c; // prev is right child
		l2i = aht->tree + c; // right child of q
		
		l2i->weight = 0;
		l2i->depth = q->depth + 1;
		l2i->parent = aht->nyt;
		l2i->left = l2i->right = -1;
		l2i->block_next = aht->nyt++; // next is parent (former nyt)
		l2i->block_prev = aht->nyt; // prev is (left) sibling (new nyt)
		// spawn new nyt off of nyt's left
		q->left = aht->nyt;
		aht->tree[aht->nyt].weight = 0;
		aht->tree[aht->nyt].depth = q->depth + 1;
		aht->tree[aht->nyt].parent = aht->nyt - 1;
		aht->tree[aht->nyt].left = aht->tree[aht->nyt].right = -1;
		aht->tree[aht->nyt].block_next = c; // next is (right) sibling
		aht->tree[aht->nyt].block_prev = -1; // prev is none
	}
	else{
		aht_interchange_leaf(aht, q); // interchange q with its leader; fine since at this point q's block is all leaves
		if (aht_sibling(aht, q) == aht->nyt){
			l2i = q;
			q = aht->tree + q->parent;
		}
	}
	while (q){
		q = aht_sai(aht, q);
	}
	if (l2i){
		aht_sai(aht, l2i);
	}
}

void aht_print_helper(const struct aht* aht, struct aht_node* ahtn, char* bm, int d){
	unsigned char buf[d + 1];
	
	if (bm[ahtn - aht->tree])
		return;
	bm[ahtn - aht->tree] = 1;
	
	memset(buf, '\t', d);
	buf[d] = 0;
	fprintf(stderr, "%s", buf);
	if (ahtn->left < 0)
		fprintf(stderr, "\033[1;32m");
	else
		fprintf(stderr, "\033[1;33m");
	fprintf(stderr, "----ID: %ld\033[0m", ahtn - aht->tree);
	if (ahtn - aht->tree <= 255)
		fprintf(stderr, " (%c)", (int)(ahtn - aht->tree));
	fprintf(stderr, "\n");
	fprintf(stderr, "%sweight: %d\n", buf, ahtn->weight);
	fprintf(stderr, "%sdepth: %d", buf, ahtn->depth);
	if (ahtn->depth != d)
		fprintf(stderr, "\033[0;31m (!%d)\033[0m", d);
	fprintf(stderr, "\n");
	if (ahtn->parent >= 0)
		fprintf(stderr, "%sparent: %d\n", buf, ahtn->parent);
	if (ahtn->left >= 0)
		fprintf(stderr, "%sleft: %d\n", buf, ahtn->left);
	if (ahtn->right >= 0)
		fprintf(stderr, "%sright: %d\n", buf, ahtn->right);
	if (ahtn->block_prev >= 0)
		fprintf(stderr, "%sprev: %d\n", buf, ahtn->block_prev);
	if (ahtn->block_next >= 0)
		fprintf(stderr, "%snext: %d\n", buf, ahtn->block_next);
	
	if (ahtn->left >= 0)
		aht_print_helper(aht, aht->tree + ahtn->left, bm, d + 1);
	if (ahtn->right >= 0)
		aht_print_helper(aht, aht->tree + ahtn->right, bm, d + 1);
}

void aht_print(const struct aht* aht){
	char* bm = calloc(aht->sz * 2, sizeof(char));
	if (!bm)
		fail_out(E_MALLOC);
	
	fprintf(stderr, "sz: %d\nnyt: %d\n", aht->sz, aht->nyt);
	fprintf(stderr, "\033[1;31mSCORE: %d\033[0m\n", aht->score);
	aht_print_helper(aht, aht->tree + aht->sz, bm, 0);
	free(bm);
}

static unsigned int aht_check_score_helper(const struct aht* aht, struct aht_node* ahtn){
	if (ahtn->left < 0){
		return ahtn->depth * ahtn->weight;
	}
	else{
		return aht_check_score_helper(aht, aht->tree + ahtn->left)
			+ aht_check_score_helper(aht, aht->tree + ahtn->right);
	}
}
int aht_check_score(const struct aht* aht){
	unsigned int s = aht_check_score_helper(aht, aht->tree + aht->sz);
	if (s == aht->score){
		fprintf(stderr, "PASS (%d)\n", aht->score);
		return 1;
	}
	else{
		fprintf(stderr, "FAIL (expected %u, got %u)\n", aht->score, s);
		return 0;
	}
}
