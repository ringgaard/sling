#include <math.h>

#include "base/clock.h"
#include "base/init.h"
#include "base/logging.h"
#include "base/macros.h"
#include "myelin/cuda/cuda.h"

using namespace sling;
using namespace sling::myelin;

const char *ptx_code = R"(

.version 4.3
.target sm_21
.address_size 64

.visible .entry vectoradd(
  .param .u64 param_0,
  .param .u64 param_1,
  .param .u64 param_2,
  .param .u32 param_3
) {
  .reg .pred   %p<2>;
  .reg .f32    %f<4>;
  .reg .b32    %r<6>;
  .reg .b64    %rd<11>;

  ld.param.u64   %rd1, [param_0];
  ld.param.u64   %rd2, [param_1];
  ld.param.u64   %rd3, [param_2];
  ld.param.u32   %r2, [param_3];

  mov.u32   %r3, %ntid.x;
  mov.u32   %r4, %ctaid.x;
  mov.u32   %r5, %tid.x;

  mad.lo.s32   %r1, %r4, %r3, %r5;
  setp.ge.s32  %p1, %r1, %r2;
  @%p1 bra   L0;

  cvta.to.global.u64   %rd4, %rd1;
  mul.wide.s32   %rd5, %r1, 4;
  add.s64   %rd6, %rd4, %rd5;

  cvta.to.global.u64   %rd7, %rd2;
  add.s64   %rd8, %rd7, %rd5;

  ld.global.f32   %f1, [%rd8];
  ld.global.f32   %f2, [%rd6];
  add.f32   %f3, %f2, %f1;

  cvta.to.global.u64   %rd9, %rd3;
  add.s64   %rd10, %rd9, %rd5;
  st.global.f32   [%rd10], %f3;

L0:
  ret;
}

)";

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  LOG(INFO) << "Initialize CUDA";
  CUDADevice device(0);
  LOG(INFO) << "CUDA device: " << device.ToString();

  LOG(INFO) << "Load CUDA module";
  CUDAModule module(ptx_code);

  LOG(INFO) << "Get function";
  CUDAFunction vectoradd(module, "vectoradd");

  LOG(INFO) << "max_threads_per_block=" << vectoradd.max_threads_per_block();
  LOG(INFO) << "shared_size=" << vectoradd.shared_size();
  LOG(INFO) << "const_size=" << vectoradd.const_size();
  LOG(INFO) << "local_size=" << vectoradd.local_size();
  LOG(INFO) << "num_regs=" << vectoradd.num_regs();
  LOG(INFO) << "ptx_version=" << vectoradd.ptx_version();
  LOG(INFO) << "binary_version=" << vectoradd.binary_version();

  // Allocate host vectors.
  int num_elements = 50000;
  size_t size = num_elements * sizeof(float);
  float *h_a = (float *) CHECK_NOTNULL(malloc(size));
  float *h_b = (float *) CHECK_NOTNULL(malloc(size));
  float *h_c = (float *) CHECK_NOTNULL(malloc(size));

  // Allocate the device input and output vectors.
  CUdeviceptr d_a, d_b, d_c;
  CHECK_CUDA(cuMemAlloc(&d_a, size));
  CHECK_CUDA(cuMemAlloc(&d_b, size));
  CHECK_CUDA(cuMemAlloc(&d_c, size));

  int threads_per_block = 256;
  int blocks_per_grid =
      (num_elements + threads_per_block - 1) / threads_per_block;
  LOG(INFO) << "CUDA kernel launch with " << blocks_per_grid << " blocks of "
            << threads_per_block << " threads";

  // Initialize the host input vectors.
  for (int i = 0; i < num_elements; ++i) {
    h_a[i] = rand() / (float) RAND_MAX;
    h_a[i] = rand() / (float) RAND_MAX;
  }

  int iterations = 10000;
  Clock clock;
  clock.start();

  for (int run = 0; run < iterations; ++run) {
    // Copy the host input vectors A and B in host memory to the device input
    // vectors in device memory.
    CHECK_CUDA(cuMemcpyHtoD(d_a, h_a, size));
    CHECK_CUDA(cuMemcpyHtoD(d_b, h_b, size));

    // Launch the vectoradd CUDA kernel.
    void *args[4] = {&d_a, &d_b, &d_c, &num_elements};
    CHECK_CUDA(cuLaunchKernel(vectoradd.handle(),
                              blocks_per_grid, 1, 1,
                              threads_per_block, 1, 1,
                              0, 0, args, nullptr));

    // Copy the device result vector in device memory to the host result vector
    // in host memory.
    CHECK_CUDA(cuMemcpyDtoH(h_c, d_c, size));
  }

  clock.stop();
  float ops = num_elements * iterations;
  LOG(INFO) << clock.cycles() / iterations << " cycles, "
            << clock.us() / iterations << " us "
            << (ops / clock.secs() / 1e9) << " GFLOPS";

  // Verify that the result vector is correct.
  for (int i = 0; i < num_elements; ++i) {
    if (fabs(h_a[i] + h_b[i] - h_c[i]) > 1e-5) {
      LOG(FATAL) << "Result verification failed at element " << i;
    }
  }

  // Free device global memory.
  CHECK_CUDA(cuMemFree(d_a));
  CHECK_CUDA(cuMemFree(d_b));
  CHECK_CUDA(cuMemFree(d_c));

  // Free host memory
  free(h_a);
  free(h_b);
  free(h_c);

  LOG(INFO) << "Done";
  return 0;
}

