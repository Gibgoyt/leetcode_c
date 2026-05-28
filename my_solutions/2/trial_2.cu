#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/*
 *	compile-time matrix dimensions
 *	keet < 16 to better visually display & <256 GPU threads, thus less to coordinate
*/
#if !defined(M_DIM)
	#define M_DIM 4
#endif
#if !defined(N_DIM)
	#define N_DIM 4
#endif

/*
 *	flat vector kernel
 *
 *	treat MxN matrix as one big M*N long vector
 *	for element wise addition position in the matrix is irrelevant
 *	C[i] only depending on A[i] and B[i]
 *	so any consistent layout (i.e. like row-major, column-major, scrambled) works as long as A and B share the layout
*/
__global__ void add_flat(
	const float *A,
	const float *B,
	float *C,
	int total
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i < total) {
		C[i] = A[i] + B[i];
	}
}

/*
 *	2D grid kernel
 *
 *	GEMM style 2D indexing that will then be applicable to matrix multiplication
 *	
 *	convention (i.e. matching solution at https://github.com/rishisankar/leetgpu/blob/main/matrix_multiplication.cu):
 *		x dimension -> columns	(innermost, horizontal)
 *		y dimension -> rows	(outermost, vertical)
 *
 *	mapping rule for row major
 *		flat = row * N + col
*/
__global__ void add_2d (
	const float *A,
	const float *B,
	float *C,
	int M,
	int N
) {
	int col = blockIdx.x * blockDim.x + threadIdx.x;
	int row = blockIdx.y * blockDim.y + threadIdx.y;
	
	/*
	 *	bounds checking both dimenstions independently
	 *	with dim3(16, 16), launching 256 threads per block for 4x4 matrix, therefore the out-bounds must do nothing please
	*/
	if (
		row < M &&
		col < N
	) {
		int idx = row * N + col;
		C[idx] = A[idx] + B[idx];
	}
}

/*
 *	pretty print matrix
*/
static void print_matrix (
	const char *name,
	const float *H,
	int M,
	int N
) {
	printf("%s (%dx%d):\n", name, M, N);
	for (int r=0; r<M; r++) {
		printf("\t[)");
		for (int c=0; c<N; c++) {
			printf("%8.2f", H[r * N + c]);
			if (c + 1 < N) {
				printf("\t");
			}
		}
		printf("\t]\n");
	}
}

int main (
	void
) {
	const int M = M_DIM;
	const int N = N_DIM;
	const int total = M * N;
	const size_t bytes = (size_t)total * sizeof(float);

	/*
	 *	host side
	 *	allocate 3 flat row major buffers + expected
	 *	host setup identical for 2d_add() and flat_add()
	 *	2d_add() 2D-ness only exists device side
	*/
	float *h_A = (float*)malloc(bytes);
	float *h_B = (float*)malloc(bytes);
	float *h_C = (float*)malloc(bytes);
	float *h_expected = (float*)malloc(bytes);

	/*
	 *	fill with a recognizable pattern so bugs stand out visually
	*/
	for (int r=0; r<M; r++) {
		for (int c=0; c<N; c++) {
			int i = r * N + c;
			h_A[i] = (float)(r * 10 + c);
			h_B[i] = (float)(c * 10 + r);
			h_expected[i] = h_A[i] + h_B[i];
		}
	}

	print_matrix("A", h_A, M, N);
	print_matrix("B", h_B, M, N);
	print_matrix("expected C", h_expected, M, N);

	/*
	 *	device buffers
	 *	manual + verbose error checking after each CUDA call, no macros
	*/
	
	float *d_A = NULL;
	float *d_B = NULL;
	float *d_C = NULL;

	cudaError_t cudaRc;

	cudaRc = cudaMalloc(&d_A, bytes);
	if (cudaRc != cudaSuccess) {
		fprintf(stderr, "cudaMalloc() failed allocating d_A: %s\n", cudaGetErrorString(cudaRc));
		exit(1);
	}

	cudaRc = cudaMalloc(&d_B, bytes);
	if (cudaRc != cudaSuccess) {
		fprintf(stderr, "cudaMalloc() failed allocating d_B: %s\n", cudaGetErrorString(cudaRc));
		exit(1);
	}

	cudaRc = cudaMalloc(&d_C, bytes);
	if (cudaRc != cudaSuccess) {
		fprintf(stderr, "cudaMalloc() failed allocating d_C: %s\n", cudaGetErrorString(cudaRc));
		exit(1);
	}

	cudaRc = cudaMemcpy(d_A, h_A, bytes, cudaMemcpyHostToDevice);
	if (cudaRc != cudaSuccess) {
		fprintf(stderr, "cudaMemcpy() h_A->d_A failed: %s\n", cudaGetErrorString(cudaRc));
		exit(1);
	}

	cudaRc = cudaMemcpy(d_B, h_B, bytes, cudaMemcpyHostToDevice);
	if (cudaRc != cudaSuccess) {
		fprintf(stderr, "cudaMemcpy() h_B->d_B failed: %s\n", cudaGetErrorString(cudaRc));
		exit(1);
	}

	/*
	 *	flat_add() launch
	*/
	{
		int threadsPerBlock = 256;
		int blocksPerGrid = (total + threadsPerBlock - 1) / threadsPerBlock;

		add_flat<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, total);
		
		/*
		 *	kernel launches are async do not return cudaError_t
		 *	to catch launch config errors check with cudaGetLastError() immediately after
		*/
		cudaRc = cudaGetLastError();
		if (cudaRc != cudaSuccess) {
			fprintf(stderr, "add_flat() launch failed: %s\n", cudaGetErrorString(cudaRc));
			exit(1);
		}

		/*
		 *	to catch runtime kernel errors (e.g. oob memory) wait for it to finish and check sync's return code
		*/
		cudaRc = cudaDeviceSynchronize();
		if (cudaRc != cudaSuccess) {
			fprintf(stderr, "add_flat() sync failed: %s\n", cudaGetErrorString(cudaRc));
			exit(1);
		}

		cudaRc = cudaMemcpy(h_C, d_C, bytes, cudaMemcpyDeviceToHost);
		if (cudaRc != cudaSuccess) {
			fprintf(stderr, "cudaMemcpy() d_C->h_C at flat_add() failed: %s\n", cudaGetErrorString(cudaRc));
		}

		print_matrix("C with flat_add()", h_C, M, N);
	}

	/*
	 *	wipe d_C so 2d_add() can recompute it, and a bug at 2d_add() will therefore not be masked
	*/
	cudaRc = cudaMemset(d_C, 0, bytes);
	if (cudaRc != cudaSuccess) {
		fprintf(stderr, "cudaMemset() failed: %s\n", cudaGetErrorString(cudaRc));
		exit(1);
	}

	/*
	 * 2d_add() kernel launch
	*/
	{
		dim3 threadsPerBlock(16, 16);
		dim3 blocksPerGrid(
			(N + threadsPerBlock.x - 1) / threadsPerBlock.x,	// x covers N columns
			(M + threadsPerBlock.y - 1) / threadsPerBlock.y		// y covers M rows
		);
		
		add_2d<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, M, N);

		cudaRc = cudaGetLastError();
		if (cudaRc != cudaSuccess) {
			fprintf(stderr, "add_2d() luanch failed: %s\n", cudaGetErrorString(cudaRc));
			exit(1);
		}

		cudaRc = cudaDeviceSynchronize();
		if (cudaRc != cudaSuccess) {
			fprintf(stderr, "add_2d() sync failed: %s\n", cudaGetErrorString(cudaRc));
			exit(1);
		}	

		cudaRc = cudaMemcpy(h_C, d_C, bytes, cudaMemcpyDeviceToHost);
		if (cudaRc != cudaSuccess) {
			fprintf(stderr, "cudaMemcpy() d_C->h_C 2d_add() failed: %s\n", cudaGetErrorString(cudaRc));
			exit(1);
		}

		print_matrix("C 2d_add()", h_C, M, N);
	}	
	int first_bad = -1;
	for (int i = 0; i < total; i++) {
		if (fabsf(h_C[i] - h_expected[i]) > 1e-5f) {
			first_bad = i;
			break;
		}
	}
	if (first_bad < 0) {
		printf("[PASS] %dx%d matrix addition (alpha and beta agree with CPU)\n", M, N);
	} else {
		printf("[FAIL] at flat=%d (row=%d col=%d): got %f, expected %f\n",
			first_bad, first_bad / N, first_bad % N,
			h_C[first_bad], h_expected[first_bad]);
	}
	free(h_A); free(h_B); free(h_C); free(h_expected);
	cudaFree(d_A); cudaFree(d_B); cudaFree(d_C);
	return 0;
}
