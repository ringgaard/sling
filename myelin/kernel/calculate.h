#ifndef MYELIN_KERNEL_CALCULATE_H_
#define MYELIN_KERNEL_CALCULATE_H_

#include "myelin/compute.h"

namespace sling {
namespace myelin {

// Register expression calculation kernels.
void RegisterCalculateKernels(Library *library);

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_KERNEL_CALCULATE_H_

