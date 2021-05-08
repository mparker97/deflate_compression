// Prints the binary representation of the input bytes; the optional argument specifies the number of bytes to be printed per line

#include <stdlib.h>
#include <stdio.h>

int main(int argc, char* argv[]){
	unsigned char buf[64];
	unsigned long c = 1;
	unsigned int chars_per_line = 8;
	int n, i, j;
	if (argc == 2){
		chars_per_line = atoi(argv[1]);
		if (chars_per_line == 0){
			printf("Invalid chars per line\n");
			exit(1);
		}
	}
	else if (argc != 1){
		printf("USAGE: %s [chars per line]\n", argv[0]);
		exit(1);
	}
	for (;;){
		n = fread(buf, sizeof(unsigned char), 64, stdin);
		if (n == 0){
			printf("\n");
			break;
		}
		for (i = 0; i < n; i++, c++){
			for (j = 0; j < 8; j++){
				printf("%d", (buf[i] & (1 << j))? 1 : 0);
			}
			printf(" ");
			if (!(c % chars_per_line)){
				printf("\n");
			}
		}
	}
}
