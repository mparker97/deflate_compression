#ifndef GLOBAL_ERRORS_H
#define GLOBAL_ERRORS_H
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
//#include "globals.h"

#define ERROR_CLEAR_MASK (127U << 24)

#define ERROR_NAME_LEN 8 // length of error names (without \0)
#define NUM_GLOBAL_ERRORS 10 // length of the following errors list // TODO
#define E_LEN    1  // Improper length
#define E_MALLOC 2  // Malloc failed
#define E_CRC    3  // CRC checksum failed
#define E_SZ     4  // Improper size
#define E_EXIST  5  // Exists
#define E_NEXIST 6  // Doesn't exist
#define E_NONULL 7  // Not NULL
#define E_RANGE  8  // Out of range
#define E_INVAL  9  // Invalid
#define E_RESERV 10 // Reserved

const static unsigned char global_errors[NUM_GLOBAL_ERRORS + 1][ERROR_NAME_LEN + 1] = {
	[E_LEN   ] = "E_LEN   ",
	[E_MALLOC] = "E_MALLOC",
	[E_CRC   ] = "E_CRC   ",
	[E_SZ    ] = "E_SZ    ",
	[E_EXIST ] = "E_EXIST ",
	[E_NEXIST] = "E_NEXIST",
	[E_NONULL] = "E_NONULL",
	[E_RANGE ] = "E_RANGE ",
	[E_INVAL ] = "E_INVAL ",
	[E_RESERV] = "E_RESERV",
	// TODO
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

static void print_error(const unsigned char* m, char* file, int line){
	fprintf(stderr, "FAIL_OUT ENCOUNTERED ERROR %s (%s:%d)\n", m, file, line);
}

#define MAX_CHECKPOINTS 10
extern int checkpoint_stack;
extern jmp_buf checkpoints[MAX_CHECKPOINTS];
static int fail_checkpoint(){
	if (++checkpoint_stack == MAX_CHECKPOINTS){
		fprintf(stderr, "Checkpoint stack full\n");
		exit(1);
	}
	return setjmp(checkpoints[checkpoint_stack]);
}

static void fail_uncheckpoint(){
	checkpoint_stack--;
}

#ifdef _DEBUG
#define do_fail_out(e, m) do {print_error(m, __FILE__, __LINE__); longjmp(checkpoints[checkpoint_stack], e);} while (0)
#else
#define do_fail_out(e, m) longjmp(checkpoints[checkpoint_stack], e)
#endif

#define fail_out(e) do_fail_out(e, global_errors[e])

#pragma GCC diagnostic pop

#endif