#include <stdio.h>
#include <stdlib.h>

#ifndef NUM_GB
#define NUM_GB 8
#endif

#define ALLOC_SIZE (4096*256)
#define PAGE_SIZE (4096)
#define N_ITERS (1024*(NUM_GB))

int main()
{
	char *test[N_ITERS] = {0};
	int i, j;
	for (i = 0; i < N_ITERS; i++)
		test [i]= malloc(ALLOC_SIZE);
	for (i = 0; i < N_ITERS; i++)
		for (j = 0; j < ALLOC_SIZE; j += PAGE_SIZE)
			test[i][j] = 2;

	for (i = 0; i < N_ITERS; i++) {
		for (j = 0; j < ALLOC_SIZE; j += PAGE_SIZE)
			if (test[i][j] != 2)
				printf("FAILURE at %d %d!\n", i, j);
		free(test[i]);
	}

	return 0;
}
