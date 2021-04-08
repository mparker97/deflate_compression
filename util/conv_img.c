// Reads in lines of human-readable RGB triples separated by spaces and prints out the ascii character for each contiguously

#include <stdio.h>
#include <stdlib.h>

int main(){
	char buf[64];
	int i, j;
	char* c;
	while (fgets(buf, 64, stdin)){
		for (i = 0; i < 3; i++){
			c = strtok(buf, " \t");
			j = atoi(c);
			printf("%c", j);
		}
	}
	printf("\n");
	return 0;
}