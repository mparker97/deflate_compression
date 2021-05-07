My deflate compression implementation in C.
# Deflate
The deflate method of compression is specified in rfc1950 and rfc1951 (see the "docs" directory). Data is compressed into two different types: literal bytes and length/distance pairs, which instruct the decoder to rewind by 'length' bytes and from there copy 'distance' bytes to the front. Both of these are compressed using Huffman encoding. The compressed file is broken into chunks that are each either entirely literal bytes or a mixture of literal bytes and length/distance pairs. For the latter, either a standard Huffman encoding is used, or a custom Huffman encoding is created through a Huffman tree of code lengths at the beginning of the chunk. This tree is itself compressed with run-length encoding and yet another Huffman tree of code lengths before it. Given the code lengths, the Huffman codes are determined through simple binary counting.
Compression is done using a "sliding window" of the most recently processed bytes to search for length/distance pairs.
# My Implementation
I use a sliding window size of 32768 bytes, which is the typical choice for deflate. This allows for a max distance of 32768 bytes and a max length of 258 bytes. To quickly identify potential length/distance pairs, I use a hash function on a triple of adjacent bytes. Thus, potential matches, which must match at least the first three bytes, exist in a hash chain of sequences to be searched, and the longest match is to be taken.

+---------------------------------------+---------------------------------------+---+

|------------------ A ------------------|------------------ B ------------------| C |

+---------------------------------------+---------------------------------------+---+

My design uses two adjacent sliding windows (A and B). Two spillover bytes (C) exist after window B so that the hash function can operate on the the last byte of B. Bytes are read into B and C in a batch. As B is processed byte by byte, the respective byte in A is overwritten and the hash chain is incrementally updated with the hash of this newest byte, and the least recent byte of the sliding window is removed. The appropriate hash chain is searched in order to find a potential length/distance pair. This match must be long enough to save space when compared to just a sequence of literal bytes; typically a length of at least three at least breaks even. If a long enough match is found, its length and distance from the current position is recorded, and the processing advances by that length. Otherwise, a literal byte is recorded, and the processing advances by 1. When B is entirely processed, C is moved to the beginning of B, and the remainder of B and C is overwritten by reading in new bytes. If a potential match exceeds the bytes presently read into B and C, 258 bytes of the next read-in commences early, allowing for the maximum length match to continue checking these bytes. The rest of the next read-in begins where it left off here. In either case, the result is that the bytes previously occupying B now reside in A, and the region spanned by A, B, and C consists of consecutive bytes.
