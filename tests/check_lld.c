#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <wait.h>
#include "../src/include/globals.h"
#include "../src/include/global_errors.h"
#include "../src/include/deflate_ext.h"
#include "../src/include/h_tree.h"

#define _TEST_CHECK_LLD

static void do_test(deflate_compr_t* com, struct h_tree_builder* htb){
	int f = fork();
	int p[2];
	char buf[128];
	
	if (pipe(p) < 0){
		fail_out(E_PIPE);
	}
	if (f < 0){
		fail_out(E_FORK);
	}
	if (f){
		close(pipe[1]);
		while (read(pipe[0], buf, 64) > 1){
			// TODO: parse the 5 numbers
		}
		wait(NULL);
	}
	else{
		close(pipe[0]);
		dup2(pipe[1], STDOUT_FILENO);
		if (fail_checkpoint()){
			process_loop(com, htb);
		}
		printf("\n"); // to terminate parent read loop
		fail_uncheckpoint();
		exit(0);
	}
}

int main(int argc, char* argv[]){
	deflate_compr_t* com = NULL;
	struct h_tree_builder htb;
	if (argc != 2){
		fprintf(stderr, "USAGE: %s FILE\n", argv[0]);
		exit(1);
	}
	if (!fail_checkpoint()){
		goto fail;
	}
	com = spawn_deflate_compr_t();
	deflate_compr_init(com, argv[1], 1 << 15);
	h_tree_builder_init(&htb, 19);
	do_test(com, &htb);
fail:
	fail_uncheckpoint();
	deflate_compr_deinit(com);
	htb_deinit(&htb);
	free(com);
}
