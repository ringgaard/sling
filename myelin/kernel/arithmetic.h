#ifndef MYELIN_KERNEL_ARITHMETIC_H_
#define MYELIN_KERNEL_ARITHMETIC_H_

#include "myelin/compute.h"

namespace sling {
namespace myelin {

// Register arithmetic library.
void RegisterArithmeticLibrary(Library *library);

// Register arithmetic transforms.
void RegisterArithmeticTransforms(Library *library);

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_KERNEL_ARITHMETIC_H_

