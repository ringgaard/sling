#ifndef MYELIN_KERNEL_WAVENET_H_
#define MYELIN_KERNEL_WAVENET_H_

#include "myelin/compute.h"

namespace sling {
namespace myelin {

// Register WaveNet kernels.
void RegisterWaveNetLibrary(Library *library);

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_KERNEL_WAVENET_H_

