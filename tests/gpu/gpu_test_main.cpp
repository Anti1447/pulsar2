#include "TestFramework.h"
#include <cuda_runtime.h>
#include <cstdio>

// Shared main for the GPU suite: exits 0 with a SKIP line when no CUDA device is present (plan: tests skip cleanly).
int main() {
	int devices = 0;
	if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) {
		std::printf("SKIP: no CUDA device\n");
		return 0;
	}
	return RunAllTests();
}
