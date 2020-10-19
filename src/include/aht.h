/* Adaptive Huffman Tree implementation based on Vitter's algorithm (http://www.ittc.ku.edu/~jsv/Papers/Vit87.jacmACMversion.pdf)

Create the Huffman tree with size (sz) equal to the number of symbols in the alphabet.
The tree is array-backed with a size of 2 * sz.
The first sz elements correspond to the symbols and are thus leaves.
Starting at element sz, the internal nodes are created to sequentially fill the back half of the array as needed.

Adaptive Huffman trees are rebalanced with each insert operation to ensure minimal prefix encoding at all times.

*/

#ifndef AHT_H
#define AHT_H

struct aht_node{ // adaptive Huffman tree node
	unsigned int weight;
	unsigned short depth;
	// the following shorts are indices into the array holding the tree
	short parent;
	short left;
	short right;
	short block_next;
	short block_prev;
};

struct aht{
	struct aht_node* tree;
	// The first sz spots are dedicated to their respective symbols; the next sz spots are dedicated to non-leaf nodes
	unsigned int score;
	int sz;
	int nyt; // not yet transferred node index; also the last node added to the end of tree
};

void aht_init(struct aht* aht, int sz);
void aht_deinit(struct aht* aht);
void aht_insert(struct aht* aht, int c);

#endif
