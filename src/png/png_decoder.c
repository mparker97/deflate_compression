#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "../include/globals.h"
#include "include/png_decoder.h"
#include "include/png_errors.h"
#include "../include/crc.h"
#include "../include/deflate_ext.h"

#define ch_cts(x) (pd->ch_counts & (1ULL << (x))) // chunk counts

#define CH_LEN_LEN 4 // length of chunk length member
#define CH_CRC_LEN 4 // length of chunk crc member

#define seek_read_sz(ptr, sz) \\
	do{ \\
		lseek(pd->fd, pd->data, SEEK_SET); \\
		read(pd->fd, ptr, sz); \\
	} while (0)
#define seek_read(ptr) seek_read_sz(ptr, pd->len)

int (*ch_funcs[CH_NUM_TYPES + 1])(struct png_decoder*) = {
	[IHDR] = chunk_IHDR,
	[PLTE] = chunk_PLTE,
	[IDAT] = chunk_IDAT,
	[IEND] = chunk_IEND,
	[CHRM] = chunk_CHRM,
	[GAMA] = chunk_GAMA,
	[ICCP] = chunk_ICCP,
	[SBIT] = chunk_SBIT,
	[SRGB] = chunk_SRGB,
	[BKGD] = chunk_BKGD,
	[HIST] = chunk_HIST,
	[TRNS] = chunk_TRNS,
	[PHYS] = chunk_PHYS,
	[SPLT] = chunk_SPLT,
	[TIME] = chunk_TIME,
	[ITXT] = chunk_ITXT,
	[TEXT] = chunk_TEXT,
	[ZTXT] = chunk_ZTXT
};

void cleanup(struct png_decoder* pd){
	int i;
	if (pd->img){
		for (i = 0; i < pd->height; i++)
			if (pd->img[i])
				free(pd->img[i]);
		freec(pd->img);
	}
	if (pd->compr_dat.str)
		freec(pd->compr_dat.str);
	if (pd->decompr_dat.str)
		freec(pd->decompr_dat.str);
	if (pd->fd)
		closec(pd->fd);
	if (pd->ch_plte)
		freec(pd->ch_plte);
	if (pd->ch_iccp)
		freec(pd->ch_iccp);
	if (pd->ch_hist)
		freec(pd->ch_hist);
	if (pd->ch_splts){
		for (i = 0; i < pd->ch_splts_len; i++)
			if (pd->ch_splts[i].name)
				free(pd->ch_splts[i].name);
		freec(pd->ch_splts);
	}
	if (pd->ch_texts){
		for (i = 0; i < pd->ch_texts_len; i++)
			if (pd->ch_texts[i].kw)
				free(pd->ch_texts[i].kw);
		freec(pd->ch_texts);
	}
	if (pd->ch_ztxts){
		for (i = 0; i < pd->ch_ztxts_len; i++){
			if (pd->ch_ztxts[i].kw)
				free(pd->ch_ztxts[i].kw);
			if (pd->ch_ztxts[i].tx.str)
				free(pd->ch_ztxts[i].tx.str);
		}
		freec(pd->ch_ztxts);
	}
	if (pd->ch_itxts){
		for (i = 0; i < pd->ch_itxts_len; i++){
			if (pd->ch_itxts[i].free_tx && pd->ch_itxts[i].tx.str)
				free(pd->ch_itxts[i].tx.str);
			if (pd->ch_itxts[i].kw)
				free(pd->ch_itxts[i].kw);
		}
		freec(pd->ch_itxts);
	}
}

void dissect_error(int e, unsigned char* e1, unsigned char* e2){
	memcpy(e1, global_errors[e >> CH_NUM_TYPES_ORDER], ERROR_NAME_LEN);
	e1[ERROR_NAME_LEN] = '\0';
	memcpy(e2, chunk_types[e & ((1 << CH_NUM_TYPES_ORDER) - 1)], CH_NAME_LEN);
	e2[CH_NAME_LEN] = '\0';
}

void check_crc(struct png_decoder* pd){
	unsigned int ccrc; // calculated crc
	unsigned char* data;
	if ((data = malloc(pd->len + CH_CRC_LEN * 2)) == NULL)
		fail_out(E_MALLOC);
	seek_read_sz(data, pd->len + CH_CRC_LEN);
	ccrc = calc_crc(data, pd->len);
	free(data);
	if (ccrc != *(unsigned int*)(data + pd->len + CH_CRC_LEN))
		return 0;
	return 1;
}

void ch_failure(struct png_decoder* pd, int ch, int e){
	unsigned char e1[ERROR_NAME_LEN + 1], e2[CH_NAME_LEN + 1], n[CH_NAME_LEN + 1];
	if (e < 0)
		return; // no error
	memcpy(n, pd->name, CH_NAME_LEN);
	n[CH_NAME_LEN] = '\0';
	dissect_error(e, e1, e2);
	printf("Error %d (%s, %s) in chunk %s: ", e, e1, e2, n);
	if (pd->name[0] & 0x20){ // critical
		printf("(critical; terminating)\n");
		fail_out(E_INVAL);
	}
	else{
		printf("(ancillary; skipping)\n");
	}
}

int check_text_restrictions(unsigned char* s, unsigned int min_len, unsigned int max_len){
	int i;
	if (s[0] == ' ') // no leading spaces
		goto fail;
	for (i = 0; i <= max_len && s[i] != '\0'; i++){
		if (s[i] < 32 || (s[i] > 126 && s[i] < 161)) // no restricted characters
			goto fail;
		if (s[i] == ' ' && s[i + 1] == ' ') // no consecutive spaces
			goto fail;
	}
	if (i < min_len || i > max_len) // min_len to max_len characters
		goto fail;
	if (s[i - 1] == ' ') // no trailing spaces
		goto fail;
	return i;
fail:
	fail_out(E_TXTRST);
}

void chunk_IHDR(struct png_decoder* pd){
	if (pd->len != sizeof(pd->ch_ihdr))
		fail_out(E_CHDSZ);
	seek_read(&pd->ch_ihdr);
	if (!pd->ch_ihdr.width || !pd->ch_ihdr.height) // height and width must be nonzero
		fail_out(E_SZ);
	if (pd->ch_ihdr.bit_depth > 16 || !pd->ch_ihdr.bit_depth || (pd->ch_ihdr.bit_depth & (pd->ch_ihdr.bit_depth - 1))) // bit depth must be 1, 2, 4, 8, 16
		fail_out(E_BTDPTH);
	pd->sample_depth = pd->ch_ihdr.bit_depth;
	switch (pd->ch_ihdr.color_type){
		case 0: // bit depth must be 1, 2, 4, 8, 16
			goto ct_pass;
		case 2:
		case 4:
		case 6: // bit depth must be 8, 16
			if (!(pd->ch_ihdr.bit_depth & 0x18))
				break;
			goto ct_pass;
		case 3: // bit depth must be 1, 2, 4, 8
			if (pd->ch_ihdr.bit_depth == 16)
				break;
			pd->sample_depth = 8;
			goto ct_pass;
		default:
			fail_out(E_COLTYP);
	}
	fail_out(E_BTDPTH);
ct_pass:
	pd->px_depth = pd->ch_ihdr.bit_depth * PX_DEPTHS[pd->ch_ihdr.color_type];
	if (pd->ch_ihdr.compression_method) // compression method must be 0
		fail_out(E_CMPRMT);
	if (pd->ch_ihdr.filter_method) // filter method must be 0
		fail_out(E_FILTMT);
	if (pd->ch_ihdr.interlace_method > 1) // interlace method must be 0 or 1
		fail_out(E_ITRLMT);
}
void chunk_PLTE(struct png_decoder* pd){
	if (pd->ch_ihdr.color_type == 0 || pd->ch_ihdr.color_type == 4) // PLTE entry must NOT exist for these color types
		fail_out(E_EXIST);
	if (!pd->len || pd->len > 255 * 3 || pd->len % 3 || (pd->ch_ihdr.color_type == 3 && pd->len / 3 > (1 << pd->ch_ihdr.bit_depth)))
		fail_out(E_CHDSZ);
	if ((pd->ch_plte = malloc(pd->len)) == NULL)
		fail_out(E_MALLOC);
	seek_read(pd->ch_plte);
	pd->ch_plte_len = pd->len / 3;
}
void chunk_IDAT(struct png_decoder* pd){
	int ret = 0;
	unsigned char* d;
	if (pd->ch_ihdr.color_type == 3 && !ch_cts(PLTE)) // PLTE entry must exist for this color type
		fail_out(E_NEXIST | PLTE);
	if ((pd->compr_dat.str = realloc(pd->compr_dat.str, pd->compr_dat.len + pd->len)) == NULL)
		fail_out(E_MALLOC);
	seek_read(pd->compr_dat.str + pd->compr_dat.len);
	pd->compr_dat.len += pd->len;
	// decompression saved for later
}
void chunk_IEND(struct png_decoder* pd){
	if (pd->len != 0)
		fail_out(E_CHDSZ);
}
void chunk_CHRM(struct png_decoder* pd){
	if (ch_cts(SRGB) || ch_cts(ICCP))
		return; // SRGB and ICCP override CHRM
	if (pd->len != sizeof(pd->ch_chrm))
		fail_out(E_CHDSZ);
	seek_read_sz(&pd->ch_chrm, sizeof(pd->ch_chrm));
}
void chunk_GAMA(struct png_decoder* pd){
	if (ch_cts(SRGB) || ch_cts(ICCP))
		return; // SRGB and ICCP override GAMA
	if (pd->len != 4)
		fail_out(E_CHDSZ);
	seek_read_sz(&pd->gamma, sizeof(unsigned int));
}
void chunk_ICCP(struct png_decoder* pd){
	int ret, i;
	struct string_len c, d;
	unsigned char* iccp = NULL, * prof;
	if (ch_chrm(SRGB))
		fail_out(E_2EMPRF); // ICCP and SRGB not compatible
	if ((iccp = malloc(pd->len + 1)) == NULL)
		fail_out(E_MALLOC);
	seek_read(iccp);
	iccp[pd->len] = '\0';
	if ((i = check_text_restrictions(iccp, 1, 79)) < 0) // profile name must be 1-79 bytes
		goto fail;
	if (i == pd->len) // no '\0'
		goto fail;
	prof = iccp + i + 2;
	if (prof[-1] != 0){
		i = E_CMPRMT;
		goto fail;
	}
	c.str = prof;
	c.len = pd->len - (prof - iccp);
	if ((ret = deflate_decompress(&d, &c, 0)) < 0){
		i = ret;
		goto fail;
	}
	iccp = realloc(iccp, i + 1); // trim to just iccp // FUTURE: leave around for saving incase it doesn't change (avoid recompression)?
	pd->ch_iccp = iccp;
	pd->ch_iccp_prof.str = d.str;
	pd->ch_iccp_prof.len = d.len;
	return;
fail:
	free(iccp);
	fail_out(i);
}
void chunk_SBIT(struct png_decoder* pd){
	int i, depths;
	switch (pd->ch_ihdr.color_type){
		case 0:
			depths = 1;
			break;
		case 2:
		case 3:
			depths = 3;
			break;
		case 4:
			depths = 2;
			break;
		case 6:
			depths = 4;
			break;
	}
	if (pd->len != depths)
		fail_out(E_CHDSZ);
	seek_read_sz(pd->ch_sbit, depths);
	for (i = 0; i < depths; i++){
		if (!pd->ch_sbit[i] || pd->ch_sbit[i] > pd->sample_depth) // each entry must be nonzero and <= sample_depth
			fail_out(E_RANGE);
	}
}
void chunk_SRGB(struct png_decoder* pd){
	if (ch_chrm(ICCP))
		fail_out(E_2EMPRF); // ICCP and SRGB not compatible
	if (pd->len != 1)
		fail_out(E_CHDSZ);
	seek_read_sz(&pd->rendering_intent, 1);
	if (pd->rendering_intent > 3)
		fail_out(E_INVAL);
}
void chunk_BKGD(struct png_decoder* pd){
	int i;
	int ct;
	unsigned char buf[6];
	int lens[4] = {2, 0, 6, 1}; // background data lengths based on ct: 3 -> 1; 0, 4 -> 2; 2, 6 -> 6
	ct = lens[pd->ch_ihdr.color_type & 3];
	if (ct > 0){
		if (pd->len != ct)
			fail_out(E_CHDSZ);
		seek_read_sz(pd->ch_bkgd, ct);
	}
	switch (pd->ch_ihdr.color_type){
		case 3:
			if (!ch_cts(PLTE)) // PLTE entry must exist for this color type
				fail_out(E_NEXIST | PLTE);
			break;
		case 0:
		case 4:
			if (*(unsigned short*)(pd->ch_bkgd) >= (1 << pd->ch_ihdr.bit_depth))
				fail_out(E_RANGE);
			break;
		case 2:
		case 6:
			for (i = 0; i < 3; i++){
				if (((unsigned short*)(pd->ch_bkgd))[i] >= (1 << pd->ch_ihdr.bit_depth))
					fail_out(E_RANGE);
			}
			break;
	}
}
void chunk_HIST(struct png_decoder* pd){
	if (!ch_cts(PLTE)) // PLTE entry must exist
		fail_out(E_NEXIST | PLTE);
	if (pd->len / 2 != pd->ch_plte_len)
		fail_out(E_CHDSZ);
	if ((pd->ch_hist = malloc(pd->len)) == NULL)
		fail_out(E_MALLOC);
	seek_read(pd->ch_hist);
	// TODO (but not here): all nonzero entries in ch_hist MUST appear somewhere in image
}
void chunk_TRNS(struct png_decoder* pd){
	// ch_trns_palette points to data for CT 3, otherwise the pointer itself is filled with the data
	int i;
	lseek(pd->fd, pd->data, SEEK_SET);
	switch (pd->ch_ihdr.color_type){
		case 4:
		case 6:
			fail_out(E_EXIST);
			break;
		case 3:
			if (!ch_cts(PLTE)) // PLTE entry must exist for this color type
				fail_out(E_NEXIST | PLTE);
			if (pd->len > pd->ch_plte_len) // at most one entry per PLTE entry
				fail_out(E_CHDSZ);
			if ((pd->ch_trns_palette = malloc(pd->len)) == NULL)
				fail_out(E_MALLOC);
			pd->ch_trns_palette_len = pd->len;
			read(pd->fd, pd->ch_trns_palette, pd->len);
			break;
		case 0:
			if (pd->len != 2)
				fail_out(E_CHDSZ);
			read(pd->fd, &pd->ch_trns_palette, 2);
			if ((unsigned short)(pd->ch_trns_palette) >= (1 << pd->ch_ihdr.bit_depth))
				fail_out(E_RANGE);
			break;
		case 2:
			if (pd->len != 6)
				fail_out(E_CHDSZ);
			read(pd->fd, &pd->ch_trns_palette, 6);
			for (i = 0; i < 3; i++){
				if (((unsigned short*)&pd->ch_trns_palette)[i] >= (1 << pd->ch_ihdr.bit_depth))
					fail_out(E_RANGE);
			}
			break;
	}
}
void chunk_PHYS(struct png_decoder* pd){
	int ret = 0;
	if (pd->len != 9)
		fail_out(E_CHDSZ | PHYS);
	lseek(pd->fd, pd->data, SEEK_SET);
	read(pd->fd, &pd->ch_phys_px_x, 4); // TODO: make these three fields a struct and do one read
	read(pd->fd, &pd->ch_phys_px_y, 4);
	read(pd->fd, &pd->ch_phys_unit, 1);
	if (pd->ch_phys_unit > 1)
		fail_out(E_INVAL | PHYS);
fail:
	return ret;
}
int chunk_SPLT(struct png_decoder* pd){
	int sz, i, j;
	unsigned char* csn = NULL, * csd;
	unsigned short prev_freq = (unsigned short)(-1);
	sz = min(pd->len, 81U);
	if ((i = a_list_add(&pd->ch_splts, &pd->ch_splts_len, sizeof(*pd->ch_splts))))
		fail_out(i);
	if ((csn = malloc(pd->len + 1)) == NULL){
		i = E_MALLOC;
		goto fail_nofree;
	}
	seek_read_sz(csn, sz);
	csn[pd->len] = '\0';
	if ((i = check_text_restrictions(csn, 1, 79)) < 0) // palette name must be 1-79 bytes
		goto fail;
	if (i == sz){ // no '\0'
		i = E_NONULL;
		goto fail;
	}
	for (j = 0; j < pd->ch_splts_len; j++) // no repeated palette names among SPLT chunks
		if (pd->ch_splts[j].name && !strcmp(csn, pd->ch_splts[j].name)){
			i = E_MULTNM;
			goto fail;
		}
	if (i + 2 > pd->len){ // no sample depth
		i = CHDSZ;
		goto fail;
	}
	csd = ((csn + 1) & 1ULL) + i + 2; // round up to short alignment
	pd->data += (off_t)csd - (off_t)csn;
	sz = pd->len - i - 2;
	seek_read_sz(csd, sz);
	if (csn[i + 1] == 8){
		if (!(sz % sizeof(struct ch_splt_data_8))){
			i = E_CHDSZ;
			goto fail;
		}
		for (j = 0; j < sz / sizeof(struct ch_splt_data_8); j++){
			if (((struct ch_splt_data_8*)csd)[j].freq > prev_freq){
				i = E_FRQORD;
				goto fail; // FUTURE: too harsh; re-sort instead?
			}
			prev_freq = ((struct ch_splt_data_8*)csd)[j].freq;
		}
	}
	else if (csn[i + 1] == 16){
		if (!(sz % sizeof(struct ch_splt_data_16))){
			i = E_CHDSZ;
			goto fail;
		}
		for (j = 0; j < sz / sizeof(struct ch_splt_data_16); j++){
			if (((struct ch_splt_data_16*)csd)[j].freq > prev_freq){
				i = E_FRQORD; // FUTURE: too harsh; re-sort instead?
				goto fail;
			}
			prev_freq = ((struct ch_splt_data_16*)csd)[j].freq;
		}
	}
	else{
		i = E_BTDPTH;
		goto fail;
	}
	pd->ch_splts[pd->ch_splts_len - 1].name = csn;
	pd->ch_splts[pd->ch_splts_len - 1].csd8 = csd;
	return;
fail:
	free(csn);
fail_nofree:
	pd->ch_splts_len--;
	fail_out(i);
}
void chunk_TIME(struct png_decoder* pd){
	// FUTURE: more advanced time checks (maybe in coordination w/ encoder)?
	if (pd->len != sizeof(pd->ch_time))
		fail_out(E_CHDSZ);
	seek_read_sz(&pd->ch_time, sizeof(pd->ch_time));
	if (
		pd->ch_time.year > 9999
		|| !pd->ch_time.month || pd->ch_time.month > 12
		|| !pd->ch_time.day || pd->ch_time.day > 31
		|| pd->ch_time.hour > 23
		|| pd->ch_time.minute > 59
		|| pd->ch_time.second > 60
	)
		fail_out(E_RANGE);
}
void chunk_ITXT(struct png_decoder* pd){
	int i, rm;
	struct string_len c, d;
	unsigned char* kw = NULL, * lt, * tkw, * tx;
	if ((i = a_list_add(&pd->ch_itxts, &pd->ch_itxts_len, sizeof(*pd->ch_itxts)))){
		fail_out(i); // didn't increment
	}
	if ((kw = malloc(pd->len + 1)) == NULL){
		i = E_MALLOC;
		goto fail_nofree;
	}
	seek_read(kw);
	kw[pd->len] = '\0';
	rm = pd->len;
	
	if ((i = check_text_restrictions(kw, 1, 79)) < 0) // keyword must be 1-79 bytes
		goto fail;
	if (i == pd->len){ // no '\0'
		i = E_NONULL;
		goto fail;
	}
	lt = kw + i + 3;
	rm -= i + 3;
	if (rm <= 0){
		i = E_CHDSZ;
		goto fail;
	}
	
	if ((i = check_text_restrictions(lt, 0, rm)) < 0)
		goto fail;
	if (i == rm){ // no '\0'
		i = E_NONULL;
		goto fail;
	}
	tkw = lt + i + 1;
	rm -= i + 1;
	if (rm <= 0){
		i = E_CHDSZ;
		goto fail;
	}
	
	if ((i = check_text_restrictions(tkw, 0, rm)) < 0)
		goto fail;
	if (i == rm){ // no '\0'
		i = E_NONULL;
		goto fail;
	}
	tx = tkw + i + 1;
	rm -= i + 1;
	if (rm < 0){
		i = E_CHDSZ;
		goto fail;
	}
	
	if (lt[-2] == 1){
		if (lt[-1] != 0){
			i = E_CMPRMT;
			goto fail;
		}
		c.str = tx;
		c.len = rm;
		if ((i = deflate_decompress(&d, &c, DEFLATE_NULLTERM)) < 0)
			goto fail;
		kw = realloc(kw, tx - kw); // trim to just keyword // FUTURE: leave around for saving incase it doesn't change (avoid recompression)?
		pd->ch_itxts[pd->ch_itxts_len - 1].free_tx = 1;
		pd->ch_itxts[pd->ch_itxts_len - 1].tx.str = d.str;
		pd->ch_itxts[pd->ch_itxts_len - 1].tx.len = d.len;
	}
	else if (lt[-2] != 0)
		fail_out(E_INVAL);
	else{
		pd->ch_itxts[pd->ch_itxts_len - 1].tx.str = tx;
		pd->ch_itxts[pd->ch_itxts_len - 1].tx.len = rm;
	}
	pd->ch_itxts[pd->ch_itxts_len - 1].kw = kw;
	pd->ch_itxts[pd->ch_itxts_len - 1].lt = lt;
	pd->ch_itxts[pd->ch_itxts_len - 1].tkw = tkw;
	return;
fail:
	free(kw);
fail_nofree:
	pd->ch_itxts_len--; // undo a_list_add
	fail_out(i);
}
void chunk_TEXT(struct png_decoder* pd){
	int i, rm;
	unsigned char* kw, * tx;
	if (((i = a_list_add(&pd->ch_texts, &pd->ch_texts_len, sizeof(*pd->ch_texts)))){
		fail_out(i); // didn't increment
	}
	if ((kw = malloc(pd->len + 1)) == NULL){
		i = E_MALLOC;
		goto fail_nofree;
	}
	seek_read(kw);
	kw[pd->len] = '\0';
	if ((i = check_text_restrictions(kw, 1, 79)) < 0) // keyword must be 1-79 bytes
		goto fail;
	tx = kw + i + 1;
	if (i == pd->len){ // no '\0'
		i = E_NONULL;
		goto fail;
	}
	rm = pd->len - i - 1;
	if ((i = check_text_restrictions(tx, 0, rm)) != rm)
		goto fail;
	pd->ch_texts[pd->ch_texts_len - 1].kw = kw;
	pd->ch_texts[pd->ch_texts_len - 1].tx.str = tx;
	pd->ch_texts[pd->ch_texts_len - 1].tx.len = rm;
	return;
fail:
	free(kw);
fail_nofree:
	pd->ch_texts_len--; // undo a_list_add
	fail_out(i);
}
void chunk_ZTXT(struct png_decoder* pd){
	int ret, i;
	struct string_len c, d;
	unsigned char* kw, * tx;
	if ((i = a_list_add(&pd->ch_ztxts, &pd->ch_ztxts_len, sizeof(*pd->ch_ztxts)))){
		fail_out(i);
	}
	if ((kw = malloc(pd->len + 1)) == NULL){
		i = E_MALLOC;
		goto fail_nofree;
	}
	seek_read(kw);
	kw[pd->len] = '\0';
	if ((i = check_text_restrictions(kw, 1, 79)) < 0) // keyword must be 1-79 bytes
		goto fail;
	if (i == pd->len){ // no '\0'
		i = E_NONULL;
		goto fail;
	}
	tx = kw + i + 2;
	if (tx[-1] != 0){
		i = E_CMPRMT;
		goto fail;
	}
	c.str = tx;
	c.len = pd->len - (tx - kw);
	if ((ret = deflate_decompress(&d, &c, DEFLATE_NULLTERM)) < 0){
		i = ret;
		goto fail;
	}
	kw = realloc(kw, i + 1); // trim to just keyword // FUTURE: leave around for saving incase it doesn't change (avoid recompression)?
	pd->ch_ztxts[pd->ch_ztxts_len - 1].kw = kw;
	pd->ch_ztxts[pd->ch_ztxts_len - 1].tx.str = d.str;
	pd->ch_ztxts[pd->ch_ztxts_len - 1].tx.len = d.len;
	return;
fail:
	freec(kw);
fail_nofree:
	pd->ch_ztxts_len--; // undo a_list_add
	fail_out(i);
}

void ch_inc(struct png_decoder* pd, int w){
	if (!(pd->name[1] & 0x20))
		pd->ch_counts |= (1ULL << w);
}

void ch_mult_ok(struct png_decoder* pd, int w){
	if (!(pd->name[1] & 0x20) && w != IDAT && w != SPLT && w != ITXT && w != TEXT && w != ZTXT && ch_cts(w)) // Multiple chunks of type when forbidden
		fail_out(E_CHMULT);
}

void ch_order_ok(struct png_decoder* pd, int w){
	if (
		(w == IHDR && pd->prev_ch < 0) // IHDR must be first
		|| ((w == PLTE || (w >= CHRM && w <= SPLT)) && ch_cts(IDAT)) // These before IDAT
		|| ((w >= CHRM && w <= SRGB) && ch_cts(PLTE)) // These before PLTE
		|| ((w >= BKGD && w <= TRNS) && !ch_cts(PLTE)) // These after PLTE
		|| (w == IDAT && pd->prev_ch != IDAT && ch_cts(IDAT)) // IDATs are consecutive
		//|| ch_cts(IEND) // IEND must be last // don't need since IEND stops iteration
		)
		fail_out(E_CHORDR | w);
}

int do_chunk(struct png_decoder* pd){
	int ret = 0, i;
	if ((i = get_ch_index(pd->name)) < 0){ // chunk type not supported
		ret = -i;
		i = 0;
		goto fail;
	}
	if (!fail_checkpoint()){
		// TODO: if not already OR'ed
		ret |= i;
		goto fail;
	}
	pd->prev_ch = i;
	if (pd->name[2] & 0x20)
		fail_out(E_RESERV); // reserved bit must not be set
	ch_order_ok(i);
	ch_mult_ok(pd, i);
	if (!check_crc(pd))
		fail_out(E_CRC);
	ch_funcs[i](pd); // ch type already embedded
	ch_inc(pd->name, i);
	ret = i;
	fail_uncheckpoint();
	goto pass;
fail:
	ch_failure(pd, i, ret); // if not critical, keep going
pass:
	return ret;
}

int next_chunk(struct png_decoder* pd, off_t* off){
	int ret = 0;
	if (*off + CH_LEN_LEN + CH_NAME_LEN + CH_CRC_LEN >= pd->f_sz)
		fail_out(E_CHFRAG);
	lseek(pd->fd, *off, SEEK_SET);
	read(pd->fd, &pd->len, CH_LEN_LEN);
	if (*off + pd->len + CH_LEN_LEN + CH_NAME_LEN + CH_CRC_LEN > pd->f_sz)
		fail_out(E_CHFRAG);
	read(pd->fd, &pd->name, CH_NAME_LEN);
	pd->data = *off + CH_LEN_LEN + CH_NAME_LEN;
	*off += pd->len + CH_LEN_LEN + CH_NAME_LEN + CH_CRC_LEN;
	return ret;
}

int iter_chunks(struct png_decoder* pd){
	int ret = 0;
	off_t off = PNG_HEAD_LEN;
	while ((ret = next_chunk(pd, &off)) >= 0){
		if ((ret = do_chunk(pd)) < 0)
			goto fail;
		if (ret == IEND)
			goto out_loop;
	}
	goto fail;
out_loop:
	if (!ch_cts(IDAT))
		fail_out(E_NODAT);
	if ((ret = deflate_decompress(&pd->decompr_dat, &pd->compr_dat, 0)) < 0)
		fail_out(-ret | IDAT);
fail:
	return ret;
}

int malloc_img(struct png_decoder* pd){
	int ret = 0;
	unsigned int i;
	if ((pd->img = calloc(pd->ch_ihdr.height, sizeof(unsigned char*))) == NULL)
		fail_out(E_MALLOC);
	for (i = 0; i < pd->ch_ihdr.height; i++)
		if ((pd->img[i] = malloc(((size_t)(pd->ch_ihdr.width) * pd->px_depth + 7) / 8)) == NULL)
			fail_out(E_MALLOC);
	}
fail:
	return ret;
}

int pass(struct png_decoder* pd; struct img_pass* ip){
	int ret = 0;
	unsigned char start;
	is->pass++;
	if (pd->ch_ihdr.interlace_method == 1){ // Adam7
		/*
		1 6 4 6 2 6 4 6
		7 7 7 7 7 7 7 7
		5 6 5 6 5 6 5 6
		7 7 7 7 7 7 7 7
		3 6 4 6 3 6 4 6
		7 7 7 7 7 7 7 7
		5 6 5 6 5 6 5 6
		7 7 7 7 7 7 7 7
	   */
		if (is->pass == 1){
			is->i_step = is->j_step = 8;
			is->i_start = 0 = is->j_start = 0;
		}
		else if (is->pass < 8){
			start = 1 << ((7 - is->pass) / 2);
			is->j_step = 1 << ((8 - is->pass) / 2);
			is->i_step = start * 2;
			if (is->pass & 1){
				is->i_start = start;
				is->j_start = 0;
			}
			else{
				is->i_start = 0;
				is->j_start = start;
			}
		}
		else {
			ret = 1;
		}
		/*
		switch (is->pass){
			case 1:
				is->i_start = 0; is->j_start = 0; is->i_step = 8; is->j_step = 8;
				break;
			case 2:
				is->i_start = 0; is->j_start = 4; is->j_step = 8; is->i_step = 8;
				break;
			case 3:
				is->i_start = 4; is->j_start = 0; is->j_step = 4; is->i_step = 8;
				break;
			case 4:
				is->i_start = 0; is->j_start = 2; is->j_step = 4; is->i_step = 4;
				break;
			case 5:
				is->i_start = 2; is->j_start = 0; is->j_step = 2; is->i_step = 4;
				break;
			case 6:
				is->i_start = 0; is->j_start = 1; is->j_step = 2; is->i_step = 2;
				break;
			case 7:
				is->i_start = 1; is->j_start = 0; is->j_step = 1; is->i_step = 2;
				break;
			default:
				ret = 1;
		}
		*/
	}
	else{
		if (is->pass > 1)
			ret = 1;
		else{
			is->start = 0;
			is->j_step = is->i_step = 1;
		}
	}
fail:
	return ret;
}

unsigned char Paeth(unsigned char a, unsigned char b, unsigned char c){
	unsigned int p, pa, pb, pc;
	unsigned char ret = a;
	p = (unsigned int)a + b - c;
	pa = (p > a)? p - a : (unsigned int)a - p;
	pa = (p > b)? p - b : (unsigned int)b - p;
	pa = (p > c)? p - c : (unsigned int)c - p;
	if (pb < pa){
		ret = b;
	}
	if (pc < pa && pc < pb){
		ret = c;
	}
	return ret;
}

void defilter(unsigned char* l0, unsigned char* l1, size_t sz){
	// l0 = defiltered previous line, l1 = filtered current line
	// l1 starts with 0 and then sz bytes of defiltered previous line
	// l0 starts with filter byte and then sz bytes to be defiltered
	// overwrite l0 starting at byte 1 with defiltered l1
	// store defiltered byte at that position in prev, if algo needs it
	int i, p;
	unsigned char prev, temp;
	switch (*l1){
		case FILTER_NONE:
			memcpy(l0 + 1, l1 + 1, sz);
			break;
		case FILTER_SUB:
			for (i = 1; i < sz + 1; i++){
				l0[i] = l1[i] + l0[i - 1];
			}
			break;
		case FILTER_UP:
			for (i = 0; i < sz + 1; i++){
				l0[i] += l1[i];
			}
			break;
		case FILTER_AVERAGE:
			for (i = 0; i < sz; i++){
				l0[i] = l1[i] + ((unsigned int)(l0[i - 1]) + l0[i]) / 2;
			}
			break;
		case FILTER_PAETH:
			prev = 0;
			for (i = 0; i < sz; i++){
				temp = l0[i];
				l0[i] = l1[i] + Paeth(l0[i - 1], l0[i], prev);
				prev = temp;
			}
			break;
		default:
			fail_out(E_DEFLTR);
	}
}

int px_write(struct png_decoder* pd, unsigned char* px, unsigned int i, unsigned int j){ // TODO: move up
	size_t n_px;
	if (pd->px_depth < 8){
		n_px = (size_t)j * pd->px_depth;
		pd->img[i][n_px / 8] |= *px << (n_px % 8)
	}
	else
		memcpy(&pd->img[i][j * pd->px_depth / 8], px, pd->px_depth / 8);
}

int fill_img(struct png_decoder* pd){
	int ret = 0;
	unsigned int i, j, k;
	unsigned char* byte;
	unsigned char* stage = NULL, * temp;
	size_t sz;
	struct img_pass ip;
	int bit;
	unsigned char px[8];
	unsigned int px_mask;
	sz = ((size_t)(pd->ch_ihdr.width) * pd->px_depth + 7) / 8 + 1; // maximum number of bytes in a scanline
	if ((stage = malloc(sz)) == NULL)
		fail_out(E_MALLOC);
	byte = pd->decompr_dat.str;
	ip.pass = 0;
	px_mask = (1 << pd->px_depth) - 1;
	while ((ret = pass(pd, &ip)) < 1){
		bit = 8;
		sz = ((size_t)(pd->ch_ihdr.width - ip->j_start) / ip->j_step * pd->px_depth + 7) / 8; // number of bytes in this scanline
		memset(stage, 0, sz + 1);
		for (i = ip->i_start; i < pd->ch_ihdr.height; i += ip->i_step){
			ret = defilter(stage, byte, sz);
			temp = byte;
			byte = stage + 1;
			for (j = ip->j_start; j < pd->ch_ihdr.width; j += ip->j_step){
				//memset(px, 0, 8);
				if (pd->ch_ihdr.bit_depth == 8){
					memcpy(px, byte, pd->px_depth / 8);
					byte += pd->px_depth / 8;
				}
				else if (pd->ch_ihdr.bit_depth == 16){ // read msb
					for (k = 0; k < pd->px_depth / 8; k += 2){
						px[k] = byte[k + 1];
						px[k + 1] = byte[k];
					}
					byte += k;
				}
				else{ // spec ensures non-byte-integral px_depth is < 1 byte and a power of two bits
					*px = (*byte >> (bit - pd->px_depth)) & px_mask; // read msb
					bit -= pd->px_depth;
					if (bit == 0){
						byte++;
						bit = 8;
					}
				}
				px_write(pd, px, i, j);
			}
			byte = temp + sz + 1;
		}
	}
fail:
	if (stage)
		free(stage);
	return ret;
}

void png_decoder_init(struct png_decoder* pd){
	pd->img = NULL;
	pd->compr_dat.str = NULL;
	pd->compr_dat.len = 0;
	pd->decompr_dat.str = NULL;
	pd->decompr_dat.len = 0;
	pd->gamma = 45455;
	pd->ch_chrm.wx = 31270;
	pd->ch_chrm.wy = 32900;
	pd->ch_chrm.rx = 64000;
	pd->ch_chrm.ry = 33000;
	pd->ch_chrm.gx = 30000;
	pd->ch_chrm.gy = 60000;
	pd->ch_chrm.bx = 15000;
	pd->ch_chrm.by = 6000;
	memset(&pd->ch_bkgd, 0, 3 * sizeof(unsigned short));
	pd->ch_phys_px_x = pd->ch_phys_px_y = 1;
	pd->ch_phys_unit = 0;
	memset(&pd->ch_sbit, 0, 4 * sizeof(unsigned char));
	pd->ch_counts = 0;
	pd->prev_ch = -1;
	pd->ch_plte_len = 0;
	memset(&pd->ch_time, 0, sizeof(pd->ch_time));
	pd->ch_texts_len = pd->ch_ztxts_len = pd->ch_itxts_len = pd->ch_splts_len = 0;
	pd->ch_trns_palette = NULL;
	pd->ch_trns_palette_len = 0;
}

int png_decode(unsigned char* file_name){
	struct png_decoder pd;
	struct stat st;
	unsigned char buf[64];
	if (fail_checkpoint() < 0){
		perror("Failed\n"); // TODO
		cleanup(&pd);
		return 0;
	}
	if ((pd.fd = open(file_name, O_RDWR)) < 0){
		perror("Failed to open file %s\n", file_name);
		fail_out(E_NEXIST);
	}
	if (fstat(pd.fd, &st) < 0){
		perror("fstat failed\n");
		fail_out(E_SZ);
	}
	if ((pd.f_sz = st.st_size) < PNG_HEAD_LEN){
		perror("file too small\n");
		fail_out(E_SZ);
	}
	read(file_name, buf, PNG_HEAD_LEN);
	if (strncmp(buf, PNG_HEAD, PNG_HEAD_LEN)){
		perror("invalid file header\n");
		fail_out(E_INVHDR);
	}
	png_decoder_init(&pd);
	iter_chunks(&pd);
	malloc_img(&pd);
	fill_img(&pd);
	// TODO: image in pd->img
	// TODO: keep some heap mem if pass
	cleanup(&pd);
	fail_uncheckpoint();
	return 1;
}
