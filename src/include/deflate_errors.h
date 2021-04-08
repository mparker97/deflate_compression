// This wraps the global_errors.h error system, adding new errors specialized for deflate operations

#ifndef DEFLATE_ERRORS_H
#define DEFLATE_ERRORS_H
#include "global_errors.h"

#define DEFLATE_ERROR_MASK (1U << 24)
#define NUM_DEFLATE_ERRORS 14
#define E_HUFAMB DEFLATE_ERROR_MASK + 1  // ambiguous Huffman code
#define E_HUFINV DEFLATE_ERROR_MASK + 2  // invalid Huffman code (input)
#define E_HUFVAL DEFLATE_ERROR_MASK + 3  // invalid Huffman value (output)
#define E_HUFDIS DEFLATE_ERROR_MASK + 4  // invalid distance (e.g. beyond sliding window)
#define E_ZADL32 DEFLATE_ERROR_MASK + 5  // invalid adler32 checksum
#define E_ZHEAD  DEFLATE_ERROR_MASK + 6  // invalid header
#define E_ZFCHCK DEFLATE_ERROR_MASK + 7  // invalid FCHECK
#define E_ZCMPMT DEFLATE_ERROR_MASK + 8  // invalid compression method
#define E_ZSLWIN DEFLATE_ERROR_MASK + 9  // invalid sliding window
#define E_ZPDICT DEFLATE_ERROR_MASK + 10 // invalid preset dictionary
#define E_ZBSZ   DEFLATE_ERROR_MASK + 11 // invalid compression block size
#define E_ZNLEN  DEFLATE_ERROR_MASK + 12 // block length one's complement mismatch
#define E_ZINV   DEFLATE_ERROR_MASK + 13 // invalid compression metadata
#define E_ZBTYPE DEFLATE_ERROR_MASK + 14 // invalid compression block type


const static unsigned char deflate_errors[NUM_DEFLATE_ERRORS + 1][ERROR_NAME_LEN + 1] = {
	[DEFLATE_ERROR_MASK - E_HUFAMB] = "E_HUFAMB",
	[DEFLATE_ERROR_MASK - E_HUFINV] = "E_HUFINV",
	[DEFLATE_ERROR_MASK - E_HUFVAL] = "E_HUFVAL",
	[DEFLATE_ERROR_MASK - E_HUFDIS] = "E_HUFDIS",
	[DEFLATE_ERROR_MASK - E_ZADL32] = "E_ZADL32",
	[DEFLATE_ERROR_MASK - E_ZHEAD ] = "E_ZHEAD ",
	[DEFLATE_ERROR_MASK - E_ZFCHCK] = "E_ZFCHCK",
	[DEFLATE_ERROR_MASK - E_ZCMPMT] = "E_ZCMPMT",
	[DEFLATE_ERROR_MASK - E_ZSLWIN] = "E_ZSLWIN",
	[DEFLATE_ERROR_MASK - E_ZPDICT] = "E_ZPDICT",
	[DEFLATE_ERROR_MASK - E_ZBSZ  ] = "E_ZBSZ  ",
	[DEFLATE_ERROR_MASK - E_ZNLEN ] = "E_ZNLEN ",
	[DEFLATE_ERROR_MASK - E_ZINV  ] = "E_ZINV  ",
	[DEFLATE_ERROR_MASK - E_ZBTYPE] = "E_ZBTYPE"
	// TODO
};

#undef fail_out
#define fail_out(e) \
	do{ \
		if (((e) & ERROR_CLEAR_MASK) == DEFLATE_ERROR_MASK) \
			do_fail_out(e, deflate_errors[e]); \
		else \
			do_fail_out(e, global_errors[e]); \
	} while(0)

#endif
