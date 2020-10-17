/* Adaptive Huffman Tree implementation based on Vitter's algorithm (http://www.ittc.ku.edu/~jsv/Papers/Vit87.jacmACMversion.pdf)

Create the Huffman tree with size (sz) equal to the number of symbols in the alphabet.
The tree is array-backed with a size of 2 * sz.
The first sz elements correspond to the symbols and are thus leaves.
Starting at element sz, the internal nodes are created to sequentially fill the back half of the array as needed.

Adaptive Huffman trees are rebalanced with each insert operation to ensure minimal prefix encoding at all times.

*/

#ifndef AHT_H
#define AHT_H

struct ah_tree_node{ // adaptive Huffman tree node
	unsigned int weight;
	unsigned short depth;
	// the following shorts are indices into the array holding the tree
	short parent;
	short left;
	short right;
	short block_next;
	short block_prev;
};

struct ah_tree{
	struct ah_tree_node* tree;
	// The first sz spots are dedicated to their respective symbols; the next sz spots are dedicated to non-leaf nodes
	unsigned int score;
	short sz;
	short nyt; // not yet transferred node index; also the last node added to the end of tree
};

void ah_tree_init(struct ah_tree* aht, short sz);
void ah_tree_deinit(struct ah_tree* aht);
void aht_insert(struct ah_tree* aht, short c);

#endif
