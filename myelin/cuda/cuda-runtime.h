#ifndef MYELIN_CUDA_CUDA_RUNTIME_H_
#define MYELIN_CUDA_CUDA_RUNTIME_H_

#include <string>

#include "base/types.h"
#include "myelin/compute.h"
#include "myelin/cuda/cuda.h"

namespace sling {
namespace myelin {

// Runtime for executing kernels on GPUs using the Nvidia CUDA API.
class CUDARuntime : public Runtime {
 public:
};

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_CUDA_CUDA_RUNTIME_H_

