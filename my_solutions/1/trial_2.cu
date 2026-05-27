#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

/*
 *	TODO answered: yes, '-D' with nvcc works, e.g.
 *		nvcc -DNUM_THREADS=32 trial_2.cu -o trial_2
 *	The idiom below means "if not overridden on the command line, default to 8".
 *	(The previous '#if !defined(NUM_THREADS)' AFTER the '#define' was dead --
 *	by the time the check ran, the macro was already defined, so #error never
 *	fired. That's why nothing was "returning error".)
*/
#ifndef NUM_THREADS
	#define NUM_THREADS 8
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

	/*
	 *	mix this thread's index with cpu_base (the single CPU random,
	 *	broadcast to every thread by value) to get a per-thread GPU random.
	 *	cpu_base changes each run -> per-thread randoms change each run too.
	*/
	uint8_t my_random = mix(
		(uint32_t)i * 2654435761u ^ (uint32_t)cpu_base
	);
	d_gpu_rand[i] = my_random;
	// uint16_t prevents byte overflow on the sum (max 255 + 255 = 510)
	d_sums[i] = (uint16_t)cpu_base + (uint16_t)my_random;
}

int main () {
	srand(
		(unsigned)time(NULL)
	);

	// CPU side: ONE random number, broadcast to every GPU thread
	uint8_t cpu_number = (uint8_t)(
		(rand() % 255) + 1
	);
	printf("CPU base: %u\n", cpu_number);

	// allocate device/GPU buffers (outputs only -- cpu_number rides by value)
	uint8_t *d_gpu_rand;
	uint16_t *d_sums;

	cudaMalloc(&d_gpu_rand, NUM_THREADS * sizeof(uint8_t));
	cudaMalloc(&d_sums,     NUM_THREADS * sizeof(uint16_t));

	/*
	 *	function<<<numBlocks, threadsPerBlock>>> is the launch config
	 *	1 block * NUM_THREADS threads = NUM_THREADS GPU threads in parallel
	 *	output order not guaranteed as threads race
	*/
	hello_kernel<<<1, NUM_THREADS>>>(
		cpu_number,
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
		NUM_THREADS * sizeof(uint16_t),
		cudaMemcpyDeviceToHost
	);

	// print everything neatly out
	for (int i=0; i<NUM_THREADS; i++) {
		printf("Thread %d: CPU_base %3u + GPU_rand %3u = %u\n",
			i, cpu_number, h_gpu_rand[i], h_sums[i]);
	}

	// clean up device
	cudaFree(d_gpu_rand);
	cudaFree(d_sums);

	return 0;
}
