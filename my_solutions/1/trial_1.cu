#include <stdio.h>

__global__ void hello_kernel () {
	printf("Hello from GPU, I am thread %d from block %d.\n", threadIdx.x, blockIdx.x);
}

int main () {
	hello_kernel<<<2, 4>>>();
	cudaDeviceSynchronize();
	return 0;
}
