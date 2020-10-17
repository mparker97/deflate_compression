#ifndef H_TREE_H
#define H_TREE_H

typedef unsigned int h_code;
#define H_CODE_1 (h_code)1
#define MAX_CODE_LEN (sizeof(h_code) * 8)

#define H_TREE_SZ_FL -1
#define H_TREE_SZ_FD -2

/* h tree (Huffman tree)
	h_tree_head has the array 'tree' to contain the array-based binary tree; its size is 'sz'
	h_tree_node contains 'left' and 'right' members, which are indices into the 'tree' array of the head for non-leaves
	The tree is traversed using a Huffman code:
		A '1' in the Huffman code takes the left branch, while a '0' takes the right branch
	All data is at the leaves. Leaves are not represented physically in the tree, but their Huffman code lookup values (val)
		are held in the parent's respective branch as -val - 1. Therefore, a negative left/right member is the end of the path,
		which works for nonegative Huffman values, and a 0 left/right member means the path is currently incomplete.
*/

struct h_tree_node{ // members are indices into the 'tree' member of h_tree_head
	short left;
	short right;
};
struct h_tree_head{
	struct h_tree_node* tree;
	int sz; // sz of H_TREE_SZ_FL means fixed Huffman tree for literal/length, H_TREE_SZ_FD means fixed Huffman tree for distance, else dynamic Huffman tree
};

struct htbq{
	unsigned short val;
	unsigned short weight;
};
struct h_tree_builder{
	struct h_tree_head head;
	struct htbq* q;
	unsigned int* weights;
	short h0, h1, t1; // 0 is queue of leaves; 1 is tree of non-leaves
};

struct hlit_hdist_hclen{
	short hlit, hdist, hclen;
};

void h_tree_init(struct h_tree_head* h, short sz);
void h_tree_deinit(struct h_tree_head* h);
int h_tree_lookup(struct h_tree_head* h, unsigned char** byte, int* byte);
void h_tree_add(struct h_tree_head* h, h_code c, int codelen, int val);
void h_tree_builder_init(struct h_tree_builder* htb, short sz);
void h_tree_builder_deinit(struct h_tree_builder* htb);
void h_tree_builder_reset(struct h_tree_builder* htb);
void h_tree_builder_build(struct h_tree_builder* htb);
unsigned int h_tree_builder_score(struct h_tree_builder* htb);
int h_tree_d_lens(struct h_tree_head* ht, struct aht* aht, struct aht* aht2, struct hlit_hdist_hclen* ldc)

#endif
