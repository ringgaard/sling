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
  .param .u64 A,
  .param .u64 B,
  .param .u64 C,
  .param .u32 N
) {
  .reg .pred   outside;
  .reg .b64    a, b, c;
  .reg .b32    n;
  .reg .b32    blkdim, blkidx, thridx, idx;
  .reg .b64    ofs, aptr, bptr, cptr;
  .reg .f32    aval, bval, cval;

  ld.param.u64   a, [A];
  ld.param.u64   b, [B];
  ld.param.u64   c, [C];
  ld.param.u32   n, [N];

  mov.u32   blkdim, %ntid.x;
  mov.u32   thridx, %ctaid.x;
  mov.u32   blkidx, %tid.x;

  mad.lo.s32   idx, thridx, blkdim, blkidx;
  setp.ge.s32  outside, idx, n;
  @outside bra   done;

  mul.wide.s32   ofs, idx, 4;
  add.s64   aptr, a, ofs;
  add.s64   bptr, b, ofs;
  add.s64   cptr, c, ofs;

  ld.global.f32   aval, [aptr];
  ld.global.f32   bval, [bptr];
  add.f32   cval, bval, aval;
  st.global.f32   [cptr], cval;

done:
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
  int num_elements = 500000;
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

  CHECK_CUDA(cuMemcpyHtoD(d_a, h_a, size));
  CHECK_CUDA(cuMemcpyHtoD(d_b, h_b, size));

  int iterations = 1000;
  float ops = num_elements;
  ops *= iterations;

  // Run tests.
  bool profile = true;
  bool test1 = true;
  bool test2 = false;
  bool test3 = false;
  bool test4 = false;

  // TEST 1: sync copy on each iteration.
  if (test1) {
    if (profile) CHECK_CUDA(cuProfilerStart());
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
    if (profile) CHECK_CUDA(cuProfilerStop());
    LOG(INFO) << "sync: " << clock.cycles() / iterations << " cycles, "
              << clock.us() / iterations << " us "
              << (ops / clock.ns()) << " GFLOPS";
  }

  // TEST 2: no copy
  if (test2) {
    if (profile) CHECK_CUDA(cuProfilerStart());
    Clock clock;
    clock.start();
    for (int run = 0; run < iterations; ++run) {
      // Launch the vectoradd CUDA kernel.
      void *args[4] = {&d_a, &d_b, &d_c, &num_elements};
      CHECK_CUDA(cuLaunchKernel(vectoradd.handle(),
                                blocks_per_grid, 1, 1,
                                threads_per_block, 1, 1,
                                0, 0, args, nullptr));
    }
    clock.stop();
    if (profile) CHECK_CUDA(cuProfilerStop());
    LOG(INFO) << "nocopy: " << clock.cycles() / iterations << " cycles, "
              << clock.us() / iterations << " us "
              << (ops / clock.ns()) << " GFLOPS";
  }

  // TEST 3: async
  if (test3) {
    CUstream stream;
    CHECK_CUDA(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING));
    if (profile) CHECK_CUDA(cuProfilerStart());
    Clock clock;
    clock.start();
    for (int run = 0; run < iterations; ++run) {
      // Launch the vectoradd CUDA kernel.
      void *args[4] = {&d_a, &d_b, &d_c, &num_elements};
      CHECK_CUDA(cuLaunchKernel(vectoradd.handle(),
                                blocks_per_grid, 1, 1,
                                threads_per_block, 1, 1,
                                1024, stream, args, nullptr));
    }
    CHECK_CUDA(cuStreamSynchronize(stream));
    clock.stop();
    if (profile) CHECK_CUDA(cuProfilerStop());
    CHECK_CUDA(cuStreamDestroy(stream));
    LOG(INFO) << "sync: " << clock.cycles() / iterations << " cycles, "
              << clock.us() / iterations << " us "
              << (ops / clock.ns()) << " GFLOPS";
  }

  // TEST 4: multi
  if (test4) {
    const int num_streams = 8;
    CUstream streams[num_streams];
    for (int i = 0; i < num_streams; ++i) {
      CHECK_CUDA(cuStreamCreate(&streams[i], CU_STREAM_NON_BLOCKING));
    }
    if (profile) CHECK_CUDA(cuProfilerStart());
    Clock clock;
    clock.start();
    for (int run = 0; run < iterations; ++run) {
      // Launch the vectoradd CUDA kernel.
      void *args[4] = {&d_a, &d_b, &d_c, &num_elements};
      CHECK_CUDA(cuLaunchKernel(vectoradd.handle(),
                                blocks_per_grid, 1, 1,
                                threads_per_block, 1, 1,
                                0, streams[run % num_streams], args, nullptr));
    }
    for (int i = 0; i < num_streams; ++i) {
      CHECK_CUDA(cuStreamSynchronize(streams[i]));
    }
    clock.stop();
    if (profile) CHECK_CUDA(cuProfilerStop());
    for (int i = 0; i < num_streams; ++i) {
      CHECK_CUDA(cuStreamDestroy(streams[i]));
    }

    LOG(INFO) << "multi: " << clock.cycles() / iterations << " cycles, "
              << clock.us() / iterations << " us "
              << (ops / clock.ns()) << " GFLOPS";
  }

  // Verify that the result vector is correct.
  if (test1) {
    for (int i = 0; i < num_elements; ++i) {
      if (fabs(h_a[i] + h_b[i] - h_c[i]) > 1e-5) {
        LOG(FATAL) << "Result verification failed at element " << i;
      }
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

