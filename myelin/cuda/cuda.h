#ifndef MYELIN_CUDA_CUDA_H_
#define MYELIN_CUDA_CUDA_H_

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/types.h"
#include "myelin/cuda/cuda-api.h"

namespace sling {
namespace myelin {

class CUDAModule;

// Check that CUDA call is successful.
#define CHECK_CUDA(op) CHECK_EQ((op), CUDA_SUCCESS)

// CUDA driver interface.
class CUDA {
 public:
  // Check if CUDA is supported on computer and it has a GPU.
  static bool Supported();

  // Return the number of CUDA-enabled GPUs.
  static int Devices();

 private:
  // Initialize CUDA. This function should only be called once.
  static void Init();
};

// CUDA device.
class CUDADevice {
 public:
  // Initialize CUDA device.
  CUDADevice(int number);
  ~CUDADevice();

  // Return device number.
  int number() const { return number_; }

  // Return handle for device.
  CUdevice handle() const { return handle_; }

  // Return context for device.
  CUcontext context() const { return context_; }

  // Compile PTX code and return module. The module is owned by the device
  // object and is destroyed together with the device object.
  CUDAModule *Compile(const char *ptx);

  // Return compute capability for device.
  int capability() const { return capability_; }

  // Get device attributes.
  int GetAttribute(CUdevice_attribute attr) const {
    int value;
    CHECK_CUDA(cuDeviceGetAttribute(&value, attr, handle_));
    return value;
  }

  // Return Number of multiprocessors on the device.
  int multiprocessors() const {
    return GetAttribute(CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT);
  }

  // Return GPU clock rate in Hz.
  int64 clock_rate() const {
    return 1000LL * GetAttribute(CU_DEVICE_ATTRIBUTE_CLOCK_RATE);
  }

  // Return GPU memory transfer rate in Hz.
  int64 memory_transfer_rate() const {
    return 1000LL * GetAttribute(CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE);
  }

  // Return global memory bus width in bits.
  int bus_width() const {
    return GetAttribute(CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH);
  }

  // Return L2 cache size.
  int l2_cache_size() const {
    return GetAttribute(CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE);
  }

  // Return number of cores per processor.
  int CoresPerSM() const;

  // Return number of cores.
  int cores() const { return multiprocessors() * CoresPerSM(); }

  // Return device name.
  string Name() const;

  // Return total amount of global memory on device.
  size_t TotalMemory() const;

  // Return device information as text.
  string ToString() const;

 public:
  // Device number.
  int number_;

  // CUDA device handle.
  CUdevice handle_;

  // Context for device.
  CUcontext context_;

  // Compute capabilities.
  int capability_;

  // List of modules owned by device.
  std::vector<CUDAModule *> modules_;
};

// CUDA module.
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

// CUDA function.
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

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_CUDA_CUDA_H_

