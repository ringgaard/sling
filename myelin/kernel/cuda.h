#ifndef MYELIN_KERNEL_CUDA_H_
#define MYELIN_KERNEL_CUDA_H_

#include "myelin/compute.h"

namespace sling {
namespace myelin {

// Register CUDA kernels.
void RegisterCUDALibrary(Library *library);

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_KERNEL_CUDA_H_

