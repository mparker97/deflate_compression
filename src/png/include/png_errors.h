#ifndef PNG_ERRORS_H
#define PNG_ERRORS_H
#include <string.h>
#include "../../include/global_errors.h"

#define PNG_ERROR_MASK (2U << 24)
#define NUM_PNG_ERRORS 15
#define E_CHNAME PNG_ERROR_MASK + 1  // Improper chunk name
#define E_TXTRST PNG_ERROR_MASK + 2  // Text failed restriction test
#define E_CHDSZ  PNG_ERROR_MASK + 3  // Improper chunk data size
#define E_BTDPTH PNG_ERROR_MASK + 4  // Invalid bit depth
#define E_COLTYP PNG_ERROR_MASK + 5  // Invalid color type
#define E_CMPRMT PNG_ERROR_MASK + 6  // Invalid compression method
#define E_FILTMT PNG_ERROR_MASK + 7  // Invalid filter method
#define E_ITRLMT PNG_ERROR_MASK + 8  // Invalid interlace method
#define E_2EMPRF PNG_ERROR_MASK + 9  // Two embedded profiles
#define E_MULTNM PNG_ERROR_MASK + 10 // Multiple of the same name
#define E_FRQORD PNG_ERROR_MASK + 11 // Impropery ordered frequencies
#define E_CHMULT PNG_ERROR_MASK + 12 // Multiple of a given chunk type
#define E_CHORDR PNG_ERROR_MASK + 13 // Chunks out of order
#define E_CHFRAG PNG_ERROR_MASK + 14 // Fragmented chunk
#define E_NODAT  PNG_ERROR_MASK + 15 // No data chunk

const unsigned char png_errors[NUM_PNG_ERRORS + 1][ERROR_NAME_LEN + 1] = {
	[PNG_ERROR_MASK + E_CHNAME] = "E_CHNAME",
	[PNG_ERROR_MASK + E_TXTRST] = "E_TXTRST",
	[PNG_ERROR_MASK + E_CHDSZ ] = "E_CHDSZ ",
	[PNG_ERROR_MASK + E_BTDPTH] = "E_BTDPTH",
	[PNG_ERROR_MASK + E_COLTYP] = "E_COLTYP",
	[PNG_ERROR_MASK + E_CMPRMT] = "E_CMPRMT",
	[PNG_ERROR_MASK + E_FILTMT] = "E_FILTMT",
	[PNG_ERROR_MASK + E_ITRLMT] = "E_ITRLMT",
	[PNG_ERROR_MASK + E_2EMPRF] = "E_2EMPRF",
	[PNG_ERROR_MASK + E_MULTNM] = "E_MULTNM",
	[PNG_ERROR_MASK + E_FRQORD] = "E_FRQORD",
	[PNG_ERROR_MASK + E_CHMULT] = "E_CHMULT",
	[PNG_ERROR_MASK + E_CHORDR] = "E_CHORDR",
	[PNG_ERROR_MASK + E_CHFRAG] = "E_CHFRAG",
	[PNG_ERROR_MASK + E_NODAT ] = "E_NODAT "
	// TODO
};

#undef fail_out
#define fail_out(s, e) \
	do{ \
		if ((e) & ERROR_CLEAR_MASK == PNG_ERROR_MASK) \
			do_fail_out((s)->env, e, png_errors[e]); \
		else \
			do_fail_out((s)->env, e, global_errors[e]); \
	} while(0)

#endif