#include "myelin/compute.h"

namespace sling {
namespace myelin {

// cuda-matmul.cc
void RegisterCUDAMatMulLibrary(Library *library);

// cuda-operators.cc
void RegisterCUDAOperatorLibrary(Library *library);

// cuda-arithmetic.cc
void RegisterCUDAArithmeticLibrary(Library *library);

// Register CUDA kernels.
void RegisterCUDAKernels(Library *library) {
  RegisterCUDAMatMulLibrary(library);
  RegisterCUDAOperatorLibrary(library);
  RegisterCUDAArithmeticLibrary(library);
}

}  // namespace myelin
}  // namespace sling

