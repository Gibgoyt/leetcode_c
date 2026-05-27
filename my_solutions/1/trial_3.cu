#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/*
 *	this is the kernel that we send over to the GPU
 *	'__global__' recognized by nvcc means it runs of the GPU and called from the CPU
 *	entry point from host to device (e.g. device being Nvidia 2080 Super)
 *
 *	every thread we launch will run vector_add() in parallel
 *	each thread doing exactly 1 element of the output
 *		C[i] = A[i] + B[i]
 *	there is no for loop, the loop is unrolled into parallel threads
*/
__global__ void vector_add(
	const float* A, 
	const float* B, 
	float* C, int N
) {
	/*
	 *	figure out which element of the index THE CURRENT thread owns
	 *	CUDA giving every thread built-in variables
	 *		threadIdx.x = my index within the block	(i.e. blockDim.x - 1)
	 *		blockIdx.x  = which block I am in	(i.e. gridDimx.x - 1)
	 *		blockDim.x  = how many threads per block(i.e. hardcoded to 256 at main())
	 *
	 *	example:
	 *		blockDim.x = 4
	 *			block 0: threads handle i = 0,1,2,3
	 *			block 1: threads handle i = 4,5,6,7
	 *			block 2: threads handle i = 8,9,10,11
	 *			...
	*/
	int i = blockIdx.x * blockDim.x + threadIdx.x;

	/*
	 *	bounds check (CRITICAL)
	 *
	 *	blocksPerGrid is computed with a ceiling division (on the host side)
	 *	for N=3, threadsPerBlock=256, 256 threads are still being launched
	 *	threads 3..255 therefore have no work to do
	 *	hence do not let them write C[3]...C[255] as memory beyond C's \0 will be corrupted
	*/
	if (i < N) {
		C[i] = A[i] + B[i];
	}
}

// A, B, C are device pointers (i.e. pointers to memory on the GPU)
extern "C" void solve(
	const float* A, 
	const float* B, 
	float* C, 
	int N
) {
	int threadsPerBlock = 256;
	int blocksPerGrid = (N + threadsPerBlock - 1) / threadsPerBlock;

	vector_add<<<blocksPerGrid, threadsPerBlock>>>(A, B, C, N);
	cudaDeviceSynchronize();
}

static void run_test (
	const char *name,
	const float *h_A,
	const float *h_B,
	const float *h_expected,
	int N
) {
	size_t bytes = N * sizeof(float);

	/*
	 *	allocate the GPU's buffers
	*/
	float *d_A;
	float *d_B;
	float *d_C;

	cudaMalloc(&d_A, bytes);
	cudaMalloc(&d_B, bytes);
	cudaMalloc(&d_C, bytes);

	/*
	 *	copy inputs from CPU's RAM to GPU's VRAM
	*/
	cudaMemcpy(d_A, h_A, bytes, cudaMemcpyHostToDevice);
	cudaMemcpy(d_B, h_B, bytes, cudaMemcpyHostToDevice);

	/*
	 *	solve(), which runs GPU kernel
	*/
	solve(d_A, d_B, d_C, N);

	/*
	 *	copy results back to CPU so that we can read it
	*/
	float *h_C = (float*)malloc(bytes);
	cudaMemcpy(h_C, d_C, bytes, cudaMemcpyDeviceToHost);

	/*
	 *	verify correctness
	*/
	int first_bad = -1;
	for (int i = 0; i < N; i++) {
		if (fabsf(h_C[i] - h_expected[i]) > 1e-5f) {
			first_bad = i;
			break;
		}
	}
	if (first_bad < 0) {
		printf("[PASS] %s (N=%d)\n", name, N);
	} else {
		printf("[FAIL] %s (N=%d) at i=%d: got %f, expected %f\n",
			   name, N, first_bad, h_C[first_bad], h_expected[first_bad]);
	}

	/*
	 *	free everything
	*/
	free(h_C);
	cudaFree(d_A);
	cudaFree(d_B);
	cudaFree(d_C);
}

int main(void) {
	// ---- Example 1 from Question.md ----
	// A = [1,2,3,4], B = [5,6,7,8], expected C = [6,8,10,12]
	float A1[] = {1.0f, 2.0f, 3.0f, 4.0f};
	float B1[] = {5.0f, 6.0f, 7.0f, 8.0f};
	float E1[] = {6.0f, 8.0f, 10.0f, 12.0f};
	run_test("Example 1", A1, B1, E1, 4);
	// ---- Example 2 from Question.md ----
	// A = [1.5, 1.5, 1.5], B = [2.3, 2.3, 2.3], expected C = [3.8, 3.8, 3.8]
	// This one stresses our float tolerance because 1.5 + 2.3 isn't exact.
	float A2[] = {1.5f, 1.5f, 1.5f};
	float B2[] = {2.3f, 2.3f, 2.3f};
	float E2[] = {3.8f, 3.8f, 3.8f};
	run_test("Example 2", A2, B2, E2, 3);
	// ---- Synthetic stress test: N = 1,000,000 ----
	// Two reasons to include this:
	//   (a) Tiny N=3,4 examples won't expose performance / memory issues.
	//   (b) 1,000,000 is NOT a multiple of 256, so the final block has
	//       idle threads -- this PROVES our `if (i < N)` bounds check
	//       is actually working.
	int N = 1000000;
	float* A3 = (float*)malloc(N * sizeof(float));
	float* B3 = (float*)malloc(N * sizeof(float));
	float* E3 = (float*)malloc(N * sizeof(float));
	for (int i = 0; i < N; i++) {
		A3[i] = (float)i * 0.5f;
		B3[i] = (float)i * 0.25f;
		E3[i] = A3[i] + B3[i];   // compute expected on CPU for comparison
	}
	run_test("Synthetic 1M", A3, B3, E3, N);
	free(A3); free(B3); free(E3);
	return 0;
}
