#include <stdio.h>

/*
 * 	'___global___' making GPU function callable from CPU
 * 	since we use nvcc to compile it???
 * 	this is a "kernel"
 * 	nvcc splits the *.cu file into host on the CPU and device on the GPU code
 * 	this qualifier tells nvcc which side the code must sit on
*/
__global__ void hello_kernel () {
	/*
	 *	every thread runs the same code, each seeing different values of local thread
	 *	threadIdx and blockIdx which is how parallelism works...
	 *	thread is the position within the block
	 *	and the block of GPU that it belongs to
	*/
	printf("Hello from GPU, I am thread %d from block %d.\n", threadIdx.x, blockIdx.x);
}

int main () {
	/*
	 *	function<<<numBlocks, threadsPerBlock>>> is the launch config
	 *	2 blocks * 4 threads per block = 8 GPU threads running the hello_kernel() function in parallel
	 *	output order not gauranteed as threads race
	*/
	hello_kernel<<<2, 4>>>();

	/*
	 *	nvcc kernel launches are async
	 *	CPU keeps running while GPU works
	 *	without the sync main() might return before device buffers are flushed, and not stdout output will be seen
	*/
	cudaDeviceSynchronize();

	return 0;
}
