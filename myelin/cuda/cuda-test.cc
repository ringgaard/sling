#include <math.h>

#include "base/clock.h"
#include "base/init.h"
#include "base/logging.h"
#include "base/macros.h"
#include "myelin/cuda/cuda-api.h"

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
	.reg .pred 	%p<2>;
	.reg .f32 	%f<4>;
	.reg .b32 	%r<6>;
	.reg .b64 	%rd<11>;

	ld.param.u64 	%rd1, [param_0];
	ld.param.u64 	%rd2, [param_1];
	ld.param.u64 	%rd3, [param_2];
	ld.param.u32 	%r2, [param_3];

	mov.u32 	%r3, %ntid.x;
	mov.u32 	%r4, %ctaid.x;
	mov.u32 	%r5, %tid.x;

	mad.lo.s32 	%r1, %r4, %r3, %r5;
	setp.ge.s32	%p1, %r1, %r2;
	@%p1 bra 	L0;

	cvta.to.global.u64 	%rd4, %rd1;
	mul.wide.s32 	%rd5, %r1, 4;
	add.s64 	%rd6, %rd4, %rd5;

	cvta.to.global.u64 	%rd7, %rd2;
	add.s64 	%rd8, %rd7, %rd5;

	ld.global.f32 	%f1, [%rd8];
	ld.global.f32 	%f2, [%rd6];
	add.f32 	%f3, %f2, %f1;

	cvta.to.global.u64 	%rd9, %rd3;
	add.s64 	%rd10, %rd9, %rd5;
	st.global.f32 	[%rd10], %f3;

L0:
	ret;
}

)";

#define CHECK_CUDA(cond) { \
  CUresult res = (cond); \
  if (res != CUDA_SUCCESS) { \
    LOG(FATAL) << "CUDA error " << res; \
  } \
}

struct CUDADeviceAttr {
  const char *name;
  CUdevice_attribute number;
};

#define DEVATTR(name) {#name, CU_DEVICE_ATTRIBUTE_##name}

CUDADeviceAttr device_attribute_list[] = {
  DEVATTR(MAX_THREADS_PER_BLOCK),
  DEVATTR(MAX_BLOCK_DIM_X),
  DEVATTR(MAX_BLOCK_DIM_Y),
  DEVATTR(MAX_BLOCK_DIM_Z),
  DEVATTR(MAX_GRID_DIM_X),
  DEVATTR(MAX_GRID_DIM_Y),
  DEVATTR(MAX_GRID_DIM_Z),
  DEVATTR(MAX_SHARED_MEMORY_PER_BLOCK),
  DEVATTR(TOTAL_CONSTANT_MEMORY),
  DEVATTR(WARP_SIZE),
  DEVATTR(MAX_PITCH),
  DEVATTR(MAX_REGISTERS_PER_BLOCK),
  DEVATTR(CLOCK_RATE),
  //DEVATTR(TEXTURE_ALIGNMENT),
  DEVATTR(GPU_OVERLAP),
  DEVATTR(MULTIPROCESSOR_COUNT),
  DEVATTR(KERNEL_EXEC_TIMEOUT),
  DEVATTR(INTEGRATED),
  DEVATTR(CAN_MAP_HOST_MEMORY),
  DEVATTR(COMPUTE_MODE),
  //DEVATTR(MAXIMUM_TEXTURE1D_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURE2D_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURE2D_HEIGHT),
  //DEVATTR(MAXIMUM_TEXTURE3D_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURE3D_HEIGHT),
  //DEVATTR(MAXIMUM_TEXTURE3D_DEPTH),
  //DEVATTR(MAXIMUM_TEXTURE2D_LAYERED_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURE2D_LAYERED_HEIGHT),
  //DEVATTR(MAXIMUM_TEXTURE2D_LAYERED_LAYERS),
  //DEVATTR(SURFACE_ALIGNMENT),
  DEVATTR(CONCURRENT_KERNELS),
  DEVATTR(ECC_ENABLED),
  DEVATTR(PCI_BUS_ID),
  DEVATTR(PCI_DEVICE_ID),
  DEVATTR(TCC_DRIVER),
  DEVATTR(MEMORY_CLOCK_RATE),
  DEVATTR(GLOBAL_MEMORY_BUS_WIDTH),
  DEVATTR(L2_CACHE_SIZE),
  DEVATTR(MAX_THREADS_PER_MULTIPROCESSOR),
  DEVATTR(ASYNC_ENGINE_COUNT),
  DEVATTR(UNIFIED_ADDRESSING),
  //DEVATTR(MAXIMUM_TEXTURE1D_LAYERED_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURE1D_LAYERED_LAYERS),
  //DEVATTR(CAN_TEX2D_GATHER),
  //DEVATTR(MAXIMUM_TEXTURE2D_GATHER_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURE2D_GATHER_HEIGHT),
  //DEVATTR(MAXIMUM_TEXTURE3D_WIDTH_ALTERNATE),
  //DEVATTR(MAXIMUM_TEXTURE3D_HEIGHT_ALTERNATE),
  //DEVATTR(MAXIMUM_TEXTURE3D_DEPTH_ALTERNATE),
  DEVATTR(PCI_DOMAIN_ID),
  //DEVATTR(TEXTURE_PITCH_ALIGNMENT),
  //DEVATTR(MAXIMUM_TEXTURECUBEMAP_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURECUBEMAP_LAYERED_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURECUBEMAP_LAYERED_LAYERS),
  //DEVATTR(MAXIMUM_SURFACE1D_WIDTH),
  //DEVATTR(MAXIMUM_SURFACE2D_WIDTH),
  //DEVATTR(MAXIMUM_SURFACE2D_HEIGHT),
  //DEVATTR(MAXIMUM_SURFACE3D_WIDTH),
  //DEVATTR(MAXIMUM_SURFACE3D_HEIGHT),
  //DEVATTR(MAXIMUM_SURFACE3D_DEPTH),
  //DEVATTR(MAXIMUM_SURFACE1D_LAYERED_WIDTH),
  //DEVATTR(MAXIMUM_SURFACE1D_LAYERED_LAYERS),
  //DEVATTR(MAXIMUM_SURFACE2D_LAYERED_WIDTH),
  //DEVATTR(MAXIMUM_SURFACE2D_LAYERED_HEIGHT),
  //DEVATTR(MAXIMUM_SURFACE2D_LAYERED_LAYERS),
  //DEVATTR(MAXIMUM_SURFACECUBEMAP_WIDTH),
  //DEVATTR(MAXIMUM_SURFACECUBEMAP_LAYERED_WIDTH),
  //DEVATTR(MAXIMUM_SURFACECUBEMAP_LAYERED_LAYERS),
  //DEVATTR(MAXIMUM_TEXTURE1D_LINEAR_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURE2D_LINEAR_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURE2D_LINEAR_HEIGHT),
  //DEVATTR(MAXIMUM_TEXTURE2D_LINEAR_PITCH),
  //DEVATTR(MAXIMUM_TEXTURE2D_MIPMAPPED_WIDTH),
  //DEVATTR(MAXIMUM_TEXTURE2D_MIPMAPPED_HEIGHT),
  DEVATTR(COMPUTE_CAPABILITY_MAJOR),
  DEVATTR(COMPUTE_CAPABILITY_MINOR),
  //DEVATTR(MAXIMUM_TEXTURE1D_MIPMAPPED_WIDTH),
  DEVATTR(STREAM_PRIORITIES_SUPPORTED),
  DEVATTR(GLOBAL_L1_CACHE_SUPPORTED),
  DEVATTR(LOCAL_L1_CACHE_SUPPORTED),
  DEVATTR(MAX_SHARED_MEMORY_PER_MULTIPROCESSOR),
  DEVATTR(MAX_REGISTERS_PER_MULTIPROCESSOR),
  DEVATTR(MANAGED_MEMORY),
  DEVATTR(MULTI_GPU_BOARD),
  DEVATTR(MULTI_GPU_BOARD_GROUP_ID),
};

class CUDADevice {
 public:
  // Initialize CUDA device.
  CUDADevice(int device_number);
  ~CUDADevice();

  // Return handle for device.
  CUdevice handle() const { return handle_; }

  // Return context for device.
  CUcontext context() const { return context_; }

  // Return the major and minor compute capability.
  int major() const { return major_; }
  int minor() const { return minor_; }

  // Return the number of CUDA-enabled devices.
  static int count();

  // Dump device attributes to log.
  void DumpAttributes();

 public:
  // CUDA device handle.
  CUdevice handle_;

  // Context for device.
  CUcontext context_;

  // Compute capabilities.
  int major_;
  int minor_;

  // Has the CUDA library been initialied?
  static bool initialized;
};

class CUDAModule {
 public:
  // Compile and initialize PTX module.
  CUDAModule(const char *ptx);
  ~CUDAModule();

  // Return module handle.
  CUmodule handle() const { return handle_; }

  // Get function handle.
  CUfunction function(const char *name);

 private:
  // CUDA module handle.
  CUmodule handle_;
};

class CUDAFunction {
 public:
  // Initialize CUDA kernel function.
  CUDAFunction(CUfunction handle) : handle_(handle) {}
  CUDAFunction(const CUDAModule &module, const char *name);

  // Return function handle.
  CUfunction handle() const { return handle_; }

  // Get function attributes.
  int GetAttribute(CUfunction_attribute attr) const {
    int value;
    CHECK_CUDA(cuFuncGetAttribute(&value, attr, handle_));
    return value;
  }

  // Return the maximum number of threads per block, beyond which a launch of
  // the function would fail. This number depends on both the function and the
  // device on which the function is currently loaded.
  int max_threads_per_block() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK);
  }

  // Return the size in bytes of statically-allocated shared memory per block
  // required by this function. This does not include dynamically-allocated
  // shared memory requested by the user at runtime.
  int shared_size() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES);
  }

  // Return the size in bytes of user-allocated constant memory required by this
  // function.
  int const_size() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES);
  }

  // Return the size in bytes of local memory used by each thread of this
  // function.
  int local_size() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES);
  }

  // Return the number of registers used by each thread of this function.
  int num_regs() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_NUM_REGS);
  }

  // Return the PTX virtual architecture version for which the function was
  // compiled. This value is the major PTX version * 10 + the minor PTX version.
  int ptx_version() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_PTX_VERSION);
  }

  // Return the binary architecture version for which the function was compiled.
  // This value is the major binary version * 10 + the minor binary version.
  int binary_version() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_BINARY_VERSION);
  }

 private:
  CUfunction handle_;
};

bool CUDADevice::initialized = false;

int CUDADevice::count() {
  // Initialize if needed.
  if (!initialized) {
    CHECK_CUDA(cuInit(0));
    initialized = true;
  }

  int num_devices;
  CHECK_CUDA(cuDeviceGetCount(&num_devices));
  return num_devices;
}

CUDADevice::CUDADevice(int device_number) {
  // Check that device is valid.
  CHECK_GT(count(), device_number);

  // Get device handle.
  CHECK_CUDA(cuDeviceGet(&handle_, device_number));

  // Create context for device.
  CHECK_CUDA(cuCtxCreate(&context_, 0, handle_));

  // Get compute capabilities.
  char name[100];
  CHECK_CUDA(cuDeviceGetName(name, sizeof(name), handle_));
  LOG(INFO) << "Using CUDA device " << device_number << ": " << name;
  CHECK_CUDA(cuDeviceComputeCapability(&major_, &minor_, handle_));
  LOG(INFO) << "GPU device has SM " << major_ << "." << minor_
            << " compute capability";

  // Get size of GPU global memory.
  size_t memory;
  CHECK_CUDA(cuDeviceTotalMem(&memory, handle_));
  LOG(INFO) << "GPU global memory: " << (memory >> 20) << " MB";
}

CUDADevice::~CUDADevice() {
  CHECK_CUDA(cuCtxDetach(context_));
}

void CUDADevice::DumpAttributes() {
  for (int i = 0; i < ARRAYSIZE(device_attribute_list); ++i) {
    int value;
    auto &attr = device_attribute_list[i];
    CHECK_CUDA(cuDeviceGetAttribute(&value, attr.number, handle_));
    LOG(INFO) << attr.name << " = " << value;
  }
}

CUDAModule::CUDAModule(const char *ptx) {
  const static int buffer_size = 1024;
  const static int num_options = 5;
  char log[buffer_size];
  char errors[buffer_size];
  CUjit_option option[num_options];
  void *value[num_options];

  option[0] = CU_JIT_INFO_LOG_BUFFER;
  value[0] = log;
  memset(log, 0, buffer_size);

  option[1] = CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES;
  value[1] = reinterpret_cast<void *>(buffer_size);

  option[2] = CU_JIT_ERROR_LOG_BUFFER;
  value[2] = errors;
  memset(errors, 0, buffer_size);

  option[3] = CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES;
  value[3] = reinterpret_cast<void *>(buffer_size);

  option[4] = CU_JIT_FALLBACK_STRATEGY;
  value[4] = reinterpret_cast<void *>(CU_PREFER_PTX);

  CUresult res = cuModuleLoadDataEx(&handle_, ptx, num_options, option, value);
  if (res != CUDA_SUCCESS) {
    LOG(FATAL) << "PTX compile error " << res << ": " << errors;
  }
  if (strlen(log) > 0) {
    LOG(INFO) << log;
  }
}

CUDAModule::~CUDAModule() {
  CHECK_CUDA(cuModuleUnload(handle_));
}

CUfunction CUDAModule::function(const char *name) {
  CUfunction func;
  CHECK_CUDA(cuModuleGetFunction(&func, handle_, name));
  return func;
}

CUDAFunction::CUDAFunction(const CUDAModule &module, const char *name) {
  CHECK_CUDA(cuModuleGetFunction(&handle_, module.handle(), name));
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  LOG(INFO) << "Load CUDA library";
  CHECK(LoadCUDALibrary());

  LOG(INFO) << "Initialize CUDA";
  CUDADevice device(0);
  device.DumpAttributes();

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

