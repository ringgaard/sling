#include "myelin/compute.h"

namespace sling {
namespace myelin {

// cuda-matmul.cc
void RegisterCUDAMatMulLibrary(Library *library);

// cuda-arithmetic.cc
void RegisterCUDAArithmeticLibrary(Library *library);

// cuda-array.cc
void RegisterCUDAArrayLibrary(Library *library);

// Register CUDA kernels.
void RegisterCUDALibrary(Library *library) {
  RegisterCUDAMatMulLibrary(library);
  RegisterCUDAArithmeticLibrary(library);
  RegisterCUDAArrayLibrary(library);
}

}  // namespace myelin
}  // namespace sling

