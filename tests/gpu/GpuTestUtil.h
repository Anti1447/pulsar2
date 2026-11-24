#pragma once
#include "TestFramework.h"
#include <cuda_runtime.h>
#include <rocketsim_cuda/sim_context.cuh>
#include <vector>

namespace gputest {
	inline void CudaOk(cudaError_t e, const char* what) {
		if (e != cudaSuccess) {
			std::ostringstream s;
			s << "CUDA error in " << what << ": " << cudaGetErrorString(e);
			throw TestFailure{ s.str() };
		}
	}
	inline void Sync() { CudaOk(cudaDeviceSynchronize(), "cudaDeviceSynchronize"); }

	inline std::vector<rocketsim_cuda::CarStatePOD> DownloadCars(rocketsim_cuda::SimContext& ctx) {
		Sync();
		std::vector<rocketsim_cuda::CarStatePOD> v(ctx.GetTotalCars());
		ctx.CopyCarStateToHost(v.data(), 0, (uint32_t)v.size());
		Sync();
		return v;
	}
	inline std::vector<rocketsim_cuda::BallStatePOD> DownloadBalls(rocketsim_cuda::SimContext& ctx) {
		Sync();
		std::vector<rocketsim_cuda::BallStatePOD> v(ctx.GetNumEnvs());
		ctx.CopyBallStateToHost(v.data(), 0, (uint32_t)v.size());
		Sync();
		return v;
	}
}
