# Matrix Multiplication

**Difficulty:** Easy

Write a program that multiplies two matrices of 32-bit floating point numbers on a GPU. Given matrix `A` of dimensions M × N and matrix `B` of dimensions N × K, compute the product matrix `C`, which will have dimensions M × K. All matrices are stored in row-major format.

## Implementation Requirements

- Use only native features (external libraries are not permitted)
- The `solve` function signature must remain unchanged
- The final result must be stored in matrix `C`

## Examples

**Example 1:**

Input — Matrix `A` (2 × 2):

```
[1.0  2.0]
[3.0  4.0]
```

Matrix `B` (2 × 2):

```
[5.0  6.0]
[7.0  8.0]
```

Output — Matrix `C` (2 × 2):

```
[19.0  22.0]
[43.0  50.0]
```

**Example 2:**

Input — Matrix `A` (1 × 3):

```
[1.0  2.0  3.0]
```

Matrix `B` (3 × 1):

```
[4.0]
[5.0]
[6.0]
```

Output — Matrix `C` (1 × 1):

```
[32.0]
```

## Constraints

- 1 ≤ M, N, K ≤ 8192
- Performance is measured with M = 8192, N = 6144, K = 4096

## Initial File

```c
#include <cuda_runtime.h>

__global__ void matrix_multiplication_kernel(
	const float* A, 
	const float* B, 
	float* C, 
	int M, 
	int N,
	int K
) {}

// A, B, C are device pointers (i.e. pointers to memory on the GPU)
extern "C" void solve(
	const float* A, 
	const float* B, 
	float* C, 
	int M, 
	int N, 
	int K
) {
	dim3 threadsPerBlock(16, 16);
	dim3 blocksPerGrid((K + threadsPerBlock.x - 1) / threadsPerBlock.x, (M + threadsPerBlock.y - 1) / threadsPerBlock.y);

	matrix_multiplication_kernel<<<blocksPerGrid, threadsPerBlock>>>(A, B, C, M, N, K);
	cudaDeviceSynchronize();
}
```
