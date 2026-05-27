/*
 *	no CUDA, this is CPU only
 *
 *	the goal is for me to first have a rock solid grasp of matrices in C before continuing
 *	patterning up on my first principles
 *	before we even touch on cuda with __global__
 *	the flattening convention, the index formula, triple nested GEMM (general matrix multiply) loop
 *
 *	*.cu only so that ./compile.sh will work
*/

#include <stdio.h>

/*
 *	§1 major row flattening
 *
 *	C got not runtime-sized 2D array type like C++ std::vector
 *	VLAs (i.e. variable length array) exist but stack only + not supported by MSVC but fuck windows
 *	anything sized at runtime lives on the heap, with malloc() handing us a 1D float*
 *	contiguous run of float without any idea of rows baked in
 *
 *	adding the notion ourselves
 *	"A buffer of length rows*cols stores the matrix row by row:  row 0 first, then row 1, then row 2, ..."
 *
 *	this is row major layout, fortran and BLAS (mostly AMD) uses column major whereas LeetGPU specifies row major
 *	hence row major for this example/trial
 *
 *	contiguous memory is cache friendly on the CPU + foreshadowing
 *	coalescing friendly on the GPU
 *	warps love it when 32 consecutive threads touch 32 consecutive floats
 *
 *	§2 the index formula
 *
 *	 For a rows x cols matrix laid out row-major, element (r, c) is at flat index:
 *	 	r * cols + c
 *	 Sanity-check for a 3x4 matrix:
 *	 	row 0 -> indices 0, 1, 2, 3
 *	 	row 1 -> indices 4, 5, 6, 7
 *	 	row 2 -> indices 8, 9,10,11
*/

/*
 *	Fill a rows x cols matrix with mat[r*cols + c] = r*cols + c
 *	The value at each cell equals its flat index, so any indexing bug becomes immediately obvious when we print.
*/
void fill_matrix (
	float *mat,
	int rows,
	int cols
) {
	for (int r=0; r<rows; r++) {
		for (int c=0; c<cols; c++) {
			mat[r * cols + c] = (float)(r * cols + c);
		}
	}
}

/*
 *	Pretty-print a rows x cols matrix, one row per line
*/
void print_matrix (
	const float *mat,
	int rows,
	int cols
) {
	for (int r=0; r<rows; r++) {
		for (int c=0; c<cols; c++) {
			printf("%6.1f", mat[r * cols + c]);
		}
		printf("\n");
	}
}

/*
 *	Triple nested GEMM loop (GEneral Matrix Multiplication)
 *
 *	Dimensions:
 *		A is M x N
 *		B is N x K
 *		C is M x K
 *
 *	For every output cell (m, k):
 *		C[m,k] = sum over i in [0, N) of  A[m,i] * B[i,k]
 *
 *	Two rules:
 *		A[m,i]  ->  A[m*N + i]   (A has N columns
 *		B[i,k]  ->  B[i*K + k]   (B has K columns)
 *
 *	Complexity: O(M*N*K). For LeetGPU's perf case (M=8192, N=6144, K=4096) that's ~206 billion multiply-adds on one
 *	CPU core -- which is exactly why we want the GPU.
*/

void gemm_cpu (
	const float *A,
	const float *B,
	float *C,
	int M,
	int N,
	int K
) {
	for (int m=0; m<M; m++) {
		for (int k=0; k<K; k++) {
			float sum = 0.0f;
			for (int i=0; i<N; i++) {
				sum += A[m * N +i] * B[i * K + k];
			}
			C[m * K + k] = sum;
		}
	}
}

int main (
	void
) {
	printf("=== Demo 1: 3x4 matrix, row-major fill ===\n");
	{
		int M = 3;
		int N = 4;
		float *mat = (float*)malloc(M * N * sizeof(float));
		if (!mat) {
			fprintf(stderr, "malloc\n");
			exit(1);
		}
		fill_matrix(mat, M, N);
		print_matrix(mat, M, N);
		free(mat);
	}

	printf("\n=== Demo 2: one element of C by hand ===\n");
	{
		float A[] = {1.0f, 2.0f, 3.0f, 4.0f};   /* 2x2 row-major */
		float B[] = {5.0f, 6.0f, 7.0f, 8.0f};   /* 2x2 row-major */
		int N_inner = 2;   /* A's cols == B's rows */
		int K_cols  = 2;   /* B's cols == C's cols */
		int m = 1, k = 0;
		float sum = 0.0f;
		for (int i = 0; i < N_inner; ++i) {
		    sum += A[m * N_inner + i] * B[i * K_cols + k];
		}
		printf("  C[%d,%d] = %.1f   (expected 43.0)\n", m, k, sum);
	}

	printf("\n=== Demo 3: full CPU GEMM, 2x2 * 2x2 ===\n");
	{
		int M = 2, N = 2, K = 2;
		float A[] = {1.0f, 2.0f, 3.0f, 4.0f};
		float B[] = {5.0f, 6.0f, 7.0f, 8.0f};
		float* C = (float*)malloc(M * K * sizeof(float));
		if (!C) {
		    perror("malloc");
		    return 1;
		}
		gemm_cpu(A, B, C, M, N, K);
		printf("  C =\n");
		print_matrix(C, M, K);
		printf("  expected:\n");
		printf("    19.0   22.0\n");
		printf("    43.0   50.0\n");
		/* Demo 5 -- §7: free heap allocations. */
		free(C);
	}

	return 0;
}
