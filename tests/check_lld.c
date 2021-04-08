/* Checks the process of encoding of the file by capturing the lit/len/dist pairs as they are produced
	and consulting its own sliding window record to print what this encoding would give upon decoding
	Thus, it verifies the encoding as it is happening by immediately decoding each token emitted to see
	if it matches the input.
	The input file, as of now, is not programmatically verified; this just prints out what the decoding is
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <wait.h>
#include <signal.h>
#include "../src/include/globals.h"
#include "../src/include/global_errors.h"
#include "../src/include/deflate_ext.h"
#include "../src/include/h_tree.h"

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

static void do_parent(int pid, int f){
	struct compress_stats cs;
	unsigned char* out_buf = malloc(SLIDING_WINDOW * sizeof(unsigned char));
	if (!out_buf){
		goto fail;
	}
	while (read(f, &cs, sizeof(cs)) > 1){
		do_write(out_buf, cs.ll, cs.dist);
	}
	return;
fail:
	close(f);
	kill(pid, SIGKILL);
}

static void do_test(deflate_compr_t* com, struct h_tree_builder* htb, int fd_in){
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
		deflate_compress(fd_in, p[1], -1, SLIDING_WINDOW, 0) // TODO: wrong
		write(p[1], &f, 1); // single arbitrary byte to terminate parent read loop
		close(p[1]);
		exit(0);
	}
}

int main(int argc, char* argv[]){
	int fd_in;
	if (argc != 2){
		fprintf(stderr, "USAGE: %s FILE\n", argv[0]);
		exit(1);
	}
	fd_in = open(argv[1], O_RDONLY);
	if (fd_in < 0){
		fprintf(stderr, "%s: no such file\n", argv[1]);
		exit(1);
	}
	do_test(com, &htb, fd_in);
	close(fd_in);
}
