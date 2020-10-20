#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(){
	char buf[64];
	int i, j;
	char* c, *n;
	while (fgets(buf, 64, stdin)){
		n = buf;
		for (i = 0; i < 3; i++){
			c = strtok(n, " ");
			j = atoi(c);
			printf("%c", j);
			n = NULL;
		}
	}
	printf("\n");
	return 0;
}
