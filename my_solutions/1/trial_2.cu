#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#define NUM_THREADS 8	// TODO!!: better to -D with nvcc??
#if !defined(NUM_THREADS)
	// TODO!!: why the fuck returning error???
	#error "bitch"
#endif

/*
 *	'__device__' only callable from GPU code
 *	tiny non-crypto mixer so each thread random uint8_t
*/
__device__ uint8_t mix (
	uint32_t seed
) {
	seed = seed * 1103515245u + 12345u;
	return (uint8_t)(
		(
			((seed >> 16) & 0xFFu) | 
			1u
		)
	);
}

/*
 * 	'___global___' making GPU function callable from CPU
 * 	since we use nvcc to compile it???
 * 	this is a "kernel"
 * 	nvcc splits the *.cu file into host on the CPU and device on the GPU code
 * 	this qualifier tells nvcc which side the code must sit on
*/
__global__ void hello_kernel (
	uint8_t cpu_base,
	const uint8_t *d_cpu_rand,
	uint8_t *d_gpu_rand,
	uint16_t *d_sums
) {
	int i = threadIdx.x;
	/*
	 *	every thread runs the same code, each seeing different values of local thread
	 *	threadIdx and blockIdx which is how parallelism works...
	 *	thread is the position within the block
	 *	and the block of GPU that it belongs to
	*/
	printf("Hello from GPU, I am thread i=%d from block %d.\n", i, blockIdx.x);

	// mix thread index with CPU per thread random to get GPU random
	uint8_t my_random = mix(
		(uint32_t)i * 2654435761u ^ d_cpu_rand[i]
	);
	d_gpu_rand[i] = my_random;
	// uint16_t prevent byte overflow on the sum
	d_sums[i] = (uint16_t)cpu_base + (uint16_t)my_random;
}

int main () {
	srand(
		(unsigned)time(NULL)
	);

	// CPU side (base value + per thread randoms)
	uint8_t cpu_number = (uint8_t)(
		(rand() % 255) + 1
	);
	printf("CPU base: %u\n", cpu_number);

	uint8_t h_cpu_rand[NUM_THREADS];
	for (int i=0; i < NUM_THREADS; i++) {
		h_cpu_rand[i] = (uint8_t)(
			(rand() % 255) + 1
		);
	}

	// allocate device/GPU buffers
	uint8_t *d_cpu_rand;
	uint8_t *d_gpu_rand;
	uint16_t *d_sums;

	cudaMalloc(&d_cpu_rand, NUM_THREADS * sizeof(uint8_t));
	cudaMalloc(&d_gpu_rand, NUM_THREADS * sizeof(uint8_t));

	cudaMemcpy(
		d_cpu_rand,
		h_cpu_rand,
		NUM_THREADS * sizeof(uint8_t),
		cudaMemcpyHostToDevice
	);

	/*
	 *	function<<<numBlocks, threadsPerBlock>>> is the launch config
	 *	2 blocks * 4 threads per block = 8 GPU threads running the hello_kernel() function in parallel
	 *	output order not gauranteed as threads race
	*/
	hello_kernel<<<1, NUM_THREADS>>>(
		cpu_number,
		d_cpu_rand,
		d_gpu_rand,
		d_sums
	);

	/*
	 *	nvcc kernel launches are async
	 *	CPU keeps running while GPU works
	 *	without the sync main() might return before device buffers are flushed, and not stdout output will be seen
	*/
	cudaDeviceSynchronize();

	// pull results back from GPU
	
	uint8_t h_gpu_rand[NUM_THREADS];
	uint16_t h_sums[NUM_THREADS];

	cudaMemcpy(
		h_gpu_rand,
		d_gpu_rand,
		NUM_THREADS * sizeof(uint8_t),
		cudaMemcpyDeviceToHost
	);
	cudaMemcpy(
		h_sums,
		d_sums,
		NUM_THREADS * sizeof(uint8_t),
		cudaMemcpyDeviceToHost
	);

	// print everything neatly out
	for (int i=0; i<NUM_THREADS; i++) {
		printf("Thread %d, CPU_rand %3u, + GPU_rand %3u = %u\n", i, h_cpu_rand[i], h_gpu_rand[i], h_sums[i]);
	}

	printf("CPU base %u was passed by value to every thread not shown above\n", cpu_number);

	// clean up device
	cudaFree(d_cpu_rand);
	cudaFree(d_gpu_rand);
	cudaFree(d_sums);
	
	return 0;
}
