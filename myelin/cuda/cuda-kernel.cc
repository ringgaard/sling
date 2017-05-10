#include "myelin/cuda/cuda-kernel.h"

#include <stdlib.h>

#include "base/logging.h"
#include "myelin/compute.h"
#include "myelin/cuda/cuda.h"
#include "myelin/cuda/cuda-runtime.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Base register used for data instance.
static Register datareg = rbp;

// Temporary register.
static Register tmpreg = r10;

PTXMacroAssembler::PTXMacroAssembler(const string &name): PTXAssembler(name) {
  // Kernel functions take one parameter with the address of the device data
  // instance block.
  data_ = param("u64", "data");

  // Grid size for kernel.
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

  // Compute block dimensions.
  int x = ptx.grid_dim(0);
  int y = ptx.grid_dim(1);
  int z = ptx.grid_dim(2);
  int block_dim_x = 1;
  int block_dim_y = 1;
  int block_dim_z = 1;
  if (x >= block_size) {
    // The x dimension takes up the whole block.
    block_dim_x = block_size;
  } else {
    // Distribute block to y dimension.
    block_dim_x = x;
    block_dim_y = block_size / block_dim_x;
    if (y < block_dim_y) {
      // Distribute block to z dimension.
      block_dim_y = y;
      block_dim_z = block_size / (block_dim_x * block_dim_y);
    }
  }

  // Compute grid dimensions.
  int grid_dim_x = (x + block_dim_x - 1) / block_dim_x;
  int grid_dim_y = (y + block_dim_y - 1) / block_dim_y;
  int grid_dim_z = (z + block_dim_z - 1) / block_dim_z;

  VLOG(5) << step->name() << ", block size " << block_size << ", thread ("
          << x << "," << y << "," << z
          << "), block ("
          << block_dim_x << "," << block_dim_y << "," << block_dim_z
          << "), grid ("
          <<  grid_dim_x << "," << grid_dim_y << "," << grid_dim_z << ")";

  // Get offset of stream in data instance block.
  int streamofs;
  if (step->task_index() == -1) {
    // Main task stream is stored in runtime block.
    streamofs = offsetof(CUDAInstance, mainstream);
  } else {
    // Parallel task stream is stored in task block.
    streamofs = step->cell()->task_offset(step->task_index()) +
                 offsetof(Task, state);
  }

  // Build parameter array with device instance address as the only parameter.
  Register params = tmpreg;
  __ pushq(Operand(datareg, offsetof(CUDAInstance, data)));
  __ pushq(rsp);
  __ movq(params, rsp);

  // Set up register-based parameters for launching kernel.
  __ movp(arg_reg_1, func.handle());
  __ movq(arg_reg_2, Immediate(grid_dim_x));
  __ movq(arg_reg_3, Immediate(grid_dim_y));
  __ movq(arg_reg_4, Immediate(grid_dim_z));
  __ movq(arg_reg_5, Immediate(block_dim_x));
  __ movq(arg_reg_6, Immediate(block_dim_y));

  // Set up stack-based parameters for launching kernel.
  __ pushq(Immediate(0));  // extra options
  __ pushq(params);
  __ pushq(Operand(datareg, streamofs));
  __ pushq(Immediate(0));  // shared memory
  __ pushq(Immediate(block_dim_z));

  // Call cuLaunchKernel.
  __ movp(tmpreg, reinterpret_cast<void *>(cuLaunchKernel));
  __ call(tmpreg);
  __ addq(rsp, Immediate(7 * 8));
}

}  // namespace myelin
}  // namespace sling

