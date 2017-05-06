#include "myelin/cuda/cuda-api.h"

#include <dlfcn.h>

#include "base/logging.h"

namespace sling {
namespace myelin {

// Handle to CUDA library.
void *cuda_lib = nullptr;

// CUDA driver API functions.
CUresult (*cuInit)(unsigned int flags);
CUresult (*cuDeviceGetCount)(int *count);
CUresult (*cuDeviceGet)(CUdevice *device, int ordinal);
CUresult (*cuDeviceGetName)(char *name, int len, CUdevice dev);
CUresult (*cuDeviceComputeCapability)(int *major,
                                      int *minor,
                                      CUdevice dev);
CUresult (*cuDeviceTotalMem)(size_t *bytes, CUdevice dev);
CUresult (*cuDeviceGetAttribute)(int *pi,
                                 CUdevice_attribute attrib,
                                 CUdevice dev);
CUresult (*cuCtxCreate)(CUcontext *pctx, 
                        unsigned int flags,
                        CUdevice dev);
CUresult (*cuCtxDetach)(CUcontext ctx);
CUresult (*cuModuleLoadDataEx)(CUmodule *module,
                               const void *image,
                               unsigned int num_options,
                               CUjit_option *options,
                               void **option_values);
CUresult (*cuModuleUnload)(CUmodule hmod);
CUresult (*cuModuleGetFunction)(CUfunction *hfunc,
                                CUmodule hmod,
                                const char *name);
CUresult (*cuFuncGetAttribute)(int *pi,
                               CUfunction_attribute attrib,
                               CUfunction hfunc);
CUresult (*cuMemAlloc)(CUdeviceptr *dptr, size_t size);
CUresult (*cuMemFree)(CUdeviceptr dptr);
CUresult (*cuMemcpyHtoD)(CUdeviceptr dst,
                         const void *src,
                         size_t size);
CUresult (*cuMemcpyDtoH)(void *dst,
                         CUdeviceptr src,
                         size_t size);
CUresult (*cuLaunchKernel)(CUfunction f,
                           unsigned int grid_dim_x,
                           unsigned int grid_dim_y,
                           unsigned int grid_dim_z,
                           unsigned int block_dim_x,
                           unsigned int block_dim_y,
                           unsigned int block_dim_z,
                           unsigned int shared_mem_bytes,
                           CUstream hstream,
                           void **kernelParams,
                           void **extra);

#define LOAD_CUDA_FUNCTION(name, version) \
  name = reinterpret_cast<decltype(name)>(dlsym(cuda_lib , #name version)); \
  if (!name) LOG(WARNING) << #name version " not found in CUDA library"

bool LoadCUDALibrary() {
  // Try to load CUDA library.
  CHECK(cuda_lib == nullptr) << "CUDA library already loaded";
  cuda_lib = dlopen("libcuda.so", RTLD_LAZY);
  if (cuda_lib == nullptr) return false;

  // Resolve library functions.
  LOAD_CUDA_FUNCTION(cuInit, "");
  LOAD_CUDA_FUNCTION(cuDeviceGetCount, "");
  LOAD_CUDA_FUNCTION(cuDeviceGet, "");
  LOAD_CUDA_FUNCTION(cuDeviceGetName, "");
  LOAD_CUDA_FUNCTION(cuDeviceComputeCapability, "");
  LOAD_CUDA_FUNCTION(cuDeviceTotalMem, "_v2");
  LOAD_CUDA_FUNCTION(cuDeviceGetAttribute, "");
  LOAD_CUDA_FUNCTION(cuCtxCreate, "_v2");
  LOAD_CUDA_FUNCTION(cuCtxDetach, "");
  LOAD_CUDA_FUNCTION(cuModuleLoadDataEx, "");
  LOAD_CUDA_FUNCTION(cuModuleUnload, "");
  LOAD_CUDA_FUNCTION(cuModuleGetFunction, "");
  LOAD_CUDA_FUNCTION(cuFuncGetAttribute, "");  
  LOAD_CUDA_FUNCTION(cuMemAlloc, "_v2");
  LOAD_CUDA_FUNCTION(cuMemFree, "_v2");
  LOAD_CUDA_FUNCTION(cuMemcpyHtoD, "_v2");
  LOAD_CUDA_FUNCTION(cuMemcpyDtoH, "_v2");
  LOAD_CUDA_FUNCTION(cuLaunchKernel, "");

  return true;
}

}  // namespace myelin
}  // namespace sling

