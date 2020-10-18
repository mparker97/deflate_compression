#ifndef PNG_DECODER_H
#define PNG_DECODER_H

#include "../include/globals.h"
#include "include/png_errors.h"

#define CH_NAME_LEN 4 // length of chunk name member
#define CH_NUM_TYPES 18 // length of following list of chunk types
#define CH_NUM_TYPES_ORDER 5 // ceil(log2(CH_NUM_TYPES - 1))
#define IHDR 1
#define PLTE 2
#define IDAT 3
#define IEND 4
#define CHRM 5
#define GAMA 6
#define ICCP 7
#define SBIT 8
#define SRGB 9
#define BKGD 10
#define HIST 11
#define TRNS 12
#define PHYS 13
#define SPLT 14
#define TIME 15
#define ITXT 16
#define TEXT 17
#define ZTXT 18

const unsigned char chunk_types[CH_NUM_TYPES + 1][CH_NAME_LEN] = {
	[0] = "NONE",
	// Critical (except PLTE), must appear in this order
	[IHDR] = "IHDR", // Header
	[PLTE] = "PLTE", // Palette
	[IDAT] = "IDAT", // Data
	[IEND] = "IEND", // End
	// Ancillary, before PLTE and IDAT
	[CHRM] = "CHRM", // Chromaticy
	[GAMA] = "GAMA", // Gamma
	[ICCP] = "ICCP", // Embedded ICC profile
	[SBIT] = "SBIT", // Significant bits
	[SRGB] = "SRGB", // Standard RGB
	// Ancillary, after PLTE, before IDAT
	[BKGD] = "BKGD", // Background color
	[HIST] = "HIST", // Palette histogram
	[TRNS] = "TRNS", // Transparency
	// Ancillary, before IDAT
	[PHYS] = "PHYS", // Physical pixel dimensions
	[SPLT] = "SPLT", // Suggested palette
	[TIME] = "TIME", // Image last-modification time
	[ITXT] = "ITXT", // International textual data
	[TEXT] = "TEXT", // Textual data
	[ZTXT] = "ZTXT"  // Compressed textual data
	// idat, splt, itxt, text, and ztxt can have multiple
};

// filter modes
#define FILTER_NONE 0
#define FILTER_SUB 1
#define FILTER_UP 2
#define FILTER_AVERAGE 3
#define FILTER_PAETH 4

const unsigned char* PNG_HEAD = "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a";
#define PNG_HEAD_LEN 8
const unsigned char* PX_DEPTHS = "\x01\x00\x03\x01\x02\x00\x04";

struct rgb8{
	unsigned char r;
	unsigned char g;
	unsigned char b;
};
struct rgba8{
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
};
struct rgb16{
	unsigned short r;
	unsigned short g;
	unsigned short b;
};
struct rgba16{
	unsigned short r;
	unsigned short g;
	unsigned short b;
	unsigned short a;
};
struct ch_splt_data_8{
	struct rgba8 rgba;
	unsigned short freq;
};
struct ch_splt_data_16{
	struct rgba16 rgba;
	unsigned short freq;
};
struct img_pass{
	unsigned char pass; // pass number through img
	unsigned char i_start; // starting index for i
	unsigned char j_start; // starting index for j
	unsigned char i_step; // stride between pixels for i
	unsigned char j_step; // stride between pixels for j
};

struct png_decoder{
	unsigned char** img;
	struct string_len compr_dat, decompr_dat;
	// TODO: 24
	struct{
		unsigned int width; // width of image in pixels
		unsigned int height; // height of image in pixels
		unsigned char bit_depth; // number of bits to specify a sample (color, palette index): 1, 2, 4, 8, 16
		unsigned char color_type; // interpretation of image data: sums of 1 (uses palette), 2 (uses color), and 4 (uses alpha channel). Possible values:
			// 0 (grayscale;                    bit_depth = 1, 2, 4, 8, 16)
			// 2 (RGB triple;                   bit_depth = 8, 16)
			// 3 (palette index;                bit_depth = 1, 2, 4, 8)
			// 4 (grayscale followed by alpha;  bit_depth = 8, 16)
			// 5 (RGB triple followed by alpha; bit_depth = 8, 16)
		unsigned char compression_method; // 0
		unsigned char filter_method; // 0
		unsigned char interlace_method; // 0 (None) or 1 (Adam7)
	} ch_ihdr;
	unsigned char sample_depth; // = (ch_ihdr.bit_depth != 3)? ch_idr.bit_depth : 8
	unsigned char px_depth; // number of bits per pixel in img; based on bit_depth and color_type; currently max 64
	unsigned char rendering_intent; // sRGB chunk, 0 = Perceptual (photographs); 1 = Relative colorimetric (logos); 2 = Saturation (charts/graphs); 3 = Absolute colorimetric (proofs/previews)
	// 16
	struct rgb8* ch_plte;
	unsigned int ch_plte_len; // number of entries in the palette
	unsigned int gamma;
	// 16
	unsigned char* ch_trns_palette;
	unsigned int ch_trns_palette_len; // <= ch_plte_len
	unsigned int TODO_PAD0; // TODO
	// 16
	struct{
		unsigned int wx;
		unsigned int wy;
		unsigned int rx;
		unsigned int ry;
		unsigned int gx;
		unsigned int gy;
		unsigned int bx;
		unsigned int by;
	} ch_chrm;
	// 32
	struct{ // freeing kw frees all
		unsigned char* kw; // keyword
		struct string_len tx; // text
	}* ch_texts;
	struct{
		unsigned char* kw; // keyword
		struct string_len tx; // text
	}* ch_ztxts;
	// 16
	struct{ // freeing kw frees all (except potentially tx; see free_tx field)
		unsigned char* kw; // keyword
		unsigned char* lt; // language tag
		unsigned char* tkw; // translated keyword
		struct string_len tx; // text
		int free_tx; // 1 if tx should be freed separately
	}* ch_itxts;
	unsigned int ch_texts_len; // length of ch_texts array
	unsigned int ch_ztxts_len; // length of ch_ztxts array
	// 16
	unsigned int ch_itxts_len; // length of ch_itxts array
	unsigned char ch_sbit[4];
	unsigned short ch_bkgd[3];
	unsigned char ch_phys_unit;
	// 15 + 1
	unsigned char* ch_iccp;
	struct string_len ch_iccp_prof;
	// TODO: 24
	unsigned short* ch_hist;
	unsigned int ch_phys_px_x;
	unsigned int ch_phys_px_y;
	// 16
	struct{ // freeing name frees everything
		unsigned char* name;
		union{ // use ((unsigned char*)csd8)[-1] to choose which: 8 -> csd8; 16 -> csd16
			struct ch_splt_data_8* csd8;
			struct ch_splt_data_16* csd16;
		};
	}* ch_splts;
	unsigned int ch_splts_len; // length of ch_splts array
	// 12 + 4
	struct{
		unsigned short year; // 4 digit year
		unsigned char month; // 1-12
		unsigned char day; // 1-31
		unsigned char hour; // 0-23
		unsigned char minute; // 0-59
		unsigned char second; // 0-60 (leap second)
	} ch_time;
	unsigned long ch_counts;
	// 15 + 1
	int prev_ch;
	int fd;
	off_t f_sz;
	// 16
	unsigned int len; // updated to the chunk length for each chunk
	unsigned char name[CH_NAME_LEN]; // updated to the chunk name for each chunk
	off_t data; // updated to the chunk file offset for each chunk
	// 16
};

// Get the chunk's index (constant number) from its name
int get_ch_index(unsigned char* name){
	int ret = 0, i;
	unsigned char nname[CH_NAME_LEN];
	for (i = 0; i < CH_NAME_LEN; i++)
		nname[i] = name[i] & ~0x20; // make uppercase
	for (i = 1; i <= CH_NUM_TYPES; i++){ // search through chunk names
		if (!strncmp(nname, chunk_types[i], CH_NAME_LEN)){
			return i;
		}
	}
	return -E_CHNAME; // must be < 0 to distinguish from chunk index number
}

#endif