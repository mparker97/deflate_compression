#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <wait.h>
#include <signal.h>
#include "../src/include/globals.h"
#include "../src/include/global_errors.h"
#include "../src/include/deflate_ext.h"
#include "../src/include/h_tree.h"

#define _TEST_CHECK_LLD // Not going to work

const static int SLIDING_WINDOW = 1 << 15;

void do_write(unsigned char* buf, int len, int dist){
	static int pos = 0;
	int cp, i;
	if (dist == 0){ // lit
		buf[pos] = len;
		printf("%c", len);
		pos = (pos + 1) % SLIDING_WINDOW;
	}
	else{ // len/dist
		cp = pos - dist;
		if (cp < 0)
			cp += SLIDING_WINDOW;
		for (i = 0; i < len; i++){
			buf[pos] = buf[cp];
			printf("%c", buf[cp]);
			pos = (pos + 1) % SLIDING_WINDOW;
			cp = (cp + 1) % SLIDING_WINDOW;
		}
	}
}

static void do_parent(int pid, int p){
	char* st, *n;
	FILE* f;
	char buf[128];
	int parse[5];
	int i, len;
	unsigned char* out_buf = malloc(SLIDING_WINDOW * sizeof(unsigned char));
	if (!out_buf){
		goto fail;
	}
	f = fdopen(p, "r");
	if (!f){
		goto fail;
	}
	while (fgets(buf, 128, f)){
		if (buf[0] == '\n')
			break;
		st = buf;
		for (i = 0; i < 5; i++){
			n = strtok(st, " ,");
			if (!n){
				goto fail;
			}
			parse[i] = atoi(n);
			st = NULL;
		}
		do_write(out_buf, parse[1], parse[2]);
	}
	fclose(f);
	return;
fail:
	close(p);
	kill(pid, SIGKILL);
}

static void do_test(deflate_compr_t* com, struct h_tree_builder* htb){
	int f;
	int p[2];
	
	if (pipe(p) < 0){
		fail_out(E_PIPE);
	}
	f = fork();
	if (f < 0){
		fail_out(E_FORK);
	}
	if (f){
		close(p[1]);
		do_parent(f, p[0]);
		wait(NULL);
	}
	else{
		close(p[0]);
		dup2(p[1], STDOUT_FILENO);
		if (fail_checkpoint()){
			process_loop(com, htb);
		}
		printf("\n"); // to terminate parent read loop
		fail_uncheckpoint();
		close(p[1]);
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
	deflate_compr_init(com, argv[1], SLIDING_WINDOW);
	h_tree_builder_init(&htb, 19);
	do_test(com, &htb);
fail:
	fail_uncheckpoint();
	deflate_compr_deinit(com);
	htb_deinit(&htb);
	free(com);
}
