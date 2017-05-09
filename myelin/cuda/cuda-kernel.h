#ifndef MYELIN_CUDA_CUDA_KERNEL_H_
#define MYELIN_CUDA_CUDA_KERNEL_H_

#include <string>

#include "myelin/compute.h"
#include "myelin/macro-assembler.h"
#include "myelin/cuda/cuda.h"

namespace sling {
namespace myelin {

// PTX macro-assembler for generating code for CUDA kernels.
class PTXMacroAssembler : public PTXAssembler {
 public:
  PTXMacroAssembler(const string &name);

  // Grid size for kernel.
  int grid_dim(int d) const { return grid_dim_[d]; }
  void set_grid_dim(int d, int size) { grid_dim_[d] = size; }

  // Return grid size for kernel.
  int grid_size() const { return grid_dim_[0] * grid_dim_[1] * grid_dim_[2]; }

 private:
  // Grid size for x, y, and z dimension.
  int grid_dim_[3];
};

// Kernel for launching CUDA kernels on GPUs.
class CUDAKernel : public Kernel {
 public:
  // Run kernel on CUDA device.
  Placement Location() { return DEVICE; }

  // Checks if CUDA is supported by runtime.
  bool Supports(Step *step) override;

  // Generate code for launching CUDA kernel.
  void Generate(Step *step, MacroAssembler *masm) override;

  // Generate PTX code for CUDA kernel.
  virtual void GeneratePTX(Step *step, PTXMacroAssembler *ptx) = 0;
};

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_CUDA_CUDA_KERNEL_H_

