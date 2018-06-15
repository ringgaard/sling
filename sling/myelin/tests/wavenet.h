#ifndef SLING_MYELIN_KERNEL_WAVENET_H_
#define SLING_MYELIN_KERNEL_WAVENET_H_

#include "sling/myelin/compute.h"

namespace sling {
namespace myelin {

// Register WaveNet kernels.
void RegisterWaveNetLibrary(Library *library);

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_KERNEL_WAVENET_H_

