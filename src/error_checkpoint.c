#include <setjmp.h>
#include "include/global_errors.h"
int checkpoint_stack = 0;
jmp_buf checkpoints[MAX_CHECKPOINTS];
