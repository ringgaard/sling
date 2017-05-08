#include "myelin/cuda/cuda.h"

#include <mutex>

#include "base/logging.h"
#include "myelin/cuda/cuda-api.h"
#include "string/printf.h"

namespace sling {
namespace myelin {

// Flag to check that we only try to initialize the CUDA library once.
static std::once_flag cuda_initialized;

// Number of CUDA-enabled devices.
static int num_cuda_devices = 0;

// Initialize CUDA support. This function should only be called once.
void CUDA::Init() {
  // Load the CUDA driver API.
  if (!LoadCUDALibrary()) return;

  // Initialize CUDA driver library.
  CHECK_CUDA(cuInit(0));

  // Get the number of CUDA-enabled devices.
  CHECK_CUDA(cuDeviceGetCount(&num_cuda_devices));
}

bool CUDA::Supported() {
  std::call_once(cuda_initialized, []() { Init(); });
  return num_cuda_devices > 0;
}

int CUDA::Devices() {
  if (!Supported()) return 0;
  return num_cuda_devices;
}

CUDADevice::CUDADevice(int number) : number_(number) {
  // Check that CUDA is supported.
  CHECK(CUDA::Supported());

  // Check that device is valid.
  CHECK_LT(number, num_cuda_devices);

  // Get device handle.
  CHECK_CUDA(cuDeviceGet(&handle_, number));

  // Create context for device.
  CHECK_CUDA(cuCtxCreate(&context_, CU_CTX_SCHED_SPIN, handle_));

  // Get compute capabilities.
  int minor, major;
  CHECK_CUDA(cuDeviceComputeCapability(&major, &minor, handle_));
  capability_ = major * 10 + minor;
}

CUDADevice::~CUDADevice() {
  CHECK_CUDA(cuCtxDetach(context_));
}

int CUDADevice::CoresPerSM() const {
  switch (capability_) {
    case 20: return 32;   // Fermi Generation (SM 2.0) GF100 class
    case 21: return 48;   // Fermi Generation (SM 2.1) GF10x class
    case 30: return 192;  // Kepler Generation (SM 3.0) GK10x class
    case 32: return 192;  // Kepler Generation (SM 3.2) GK10x class
    case 35: return 192;  // Kepler Generation (SM 3.5) GK11x class
    case 37: return 192;  // Kepler Generation (SM 3.7) GK21x class
    case 50: return 128;  // Maxwell Generation (SM 5.0) GM10x class
    case 52: return 128;  // Maxwell Generation (SM 5.2) GM20x class
    case 53: return 128;  // Maxwell Generation (SM 5.3) GM20x class
    case 60: return 64;   // Pascal Generation (SM 6.0) GP100 class
    case 61: return 128;  // Pascal Generation (SM 6.1) GP10x class
    case 62: return 128;  // Pascal Generation (SM 6.2) GP10x class
    default: return 128;
  }
}

string CUDADevice::Name() const {
  // Get GPU device name.
  char name[256];
  CHECK_CUDA(cuDeviceGetName(name, sizeof(name), handle_));
  return name;
}

size_t CUDADevice::TotalMemory() const {
  // Get size of GPU global memory.
  size_t memory;
  CHECK_CUDA(cuDeviceTotalMem(&memory, handle_));
  return memory;
}

string CUDADevice::ToString() const {
  int version;
  CHECK_CUDA(cuDriverGetVersion(&version));
  string name = Name();
  size_t memory = TotalMemory();
  int64 bandwidth = memory_transfer_rate() * (bus_width() / 8);
  string str;
  StringAppendF(&str, "%s, SM %d.%d, %lu MB RAM, "
                "%d cores @ %lld MHz, "
                "%lld GB/s bandwidth (%lld Mhz %d-bits), "
                "%d KB L2 cache, "
                "CUDA v%d.%d",
                name.c_str(),
                capability_ / 10, capability_ % 10,
                memory >> 20,
                cores(),
                clock_rate() / 1000000,
                bandwidth / 1000000000,
                memory_transfer_rate() / 1000000,
                bus_width(),
                l2_cache_size() >> 10,
                version / 1000, version % 1000);
  return str;
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

}  // namespace myelin
}  // namespace sling

