#include "myelin/cuda/cuda-kernel.h"

#include "base/logging.h"
#include "myelin/compute.h"
#include "myelin/cuda/cuda.h"

namespace sling {
namespace myelin {

using namespace jit;

PTXMacroAssembler::PTXMacroAssembler(const string &name) : PTXAssembler(name) {
 grid_dim_[0] = 1;
 grid_dim_[1] = 1;
 grid_dim_[2] = 1;
}

bool CUDAKernel::Supports(Step *step) {
  CUDADevice *device = step->cell()->runtime()->Device();
  return device != nullptr;
}

void CUDAKernel::Generate(Step *step, MacroAssembler *masm) {
  // Set up macro-assembler for generating PTX code for kernel.
  CUDADevice *device = step->cell()->runtime()->Device();
  CHECK(device != nullptr);
  string name = Name();
  PTXMacroAssembler ptx(name.c_str());
  ptx.set_target(device->capability());
  ptx.param("u64", "data");

  // Generate PTX code for GPU kernel.
  GeneratePTX(step, &ptx);
  string code;
  ptx.Generate(&code);

  // Compile PTX into a CUDA module.
  CUDAModule *module = device->Compile(code.c_str());
  CUDAFunction func(*module, name.c_str());

  // Compute kernel block size.
  int grid_size = ptx.grid_size();
  int min_grid_size;
  int block_size;
  CHECK_CUDA(cuOccupancyMaxPotentialBlockSize (
      &min_grid_size, &block_size, func.handle(),
      nullptr, func.shared_size(), grid_size));

  LOG(INFO) << min_grid_size;
  LOG(INFO) << block_size;
}

}  // namespace myelin
}  // namespace sling

