#include "myelin/compute.h"

namespace sling {
namespace myelin {

// cuda-matmul.cc
void RegisterCUDAMatMul(Library *library);

// cuda-operators.cc
void RegisterCUDAOperators(Library *library);

// Register CUDA kernels.
void RegisterCUDAKernels(Library *library) {
  RegisterCUDAMatMul(library);
  RegisterCUDAOperators(library);
}

}  // namespace myelin
}  // namespace sling

