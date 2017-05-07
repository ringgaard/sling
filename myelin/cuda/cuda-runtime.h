#ifndef MYELIN_CUDA_CUDA_RUNTIME_H_
#define MYELIN_CUDA_CUDA_RUNTIME_H_

#include <string>

#include "base/types.h"
#include "myelin/compute.h"
#include "myelin/cuda/cuda.h"

namespace sling {
namespace myelin {

// Instance data for cells running on CUDA devices. This is stored at the
// beginning of the host data instance block.
struct CUDAInstance {
  DevicePtr data;       // pointer to instance data allocated on device
  CUstream mainstream;  // stream for synchronizing operations in main task
};

// Runtime for executing kernels on GPUs using the Nvidia CUDA API.
class CUDARuntime : public Runtime {
 public:
  // Initialize runtime for running ops on CUDA devices. If the device number is
  // -1 the runtime tried to selected the best GPU device for computations.
  CUDARuntime(int device_number = -1);
  ~CUDARuntime();
  string Description() override;

  // Instance data allocation.
  void AllocateInstance(Instance *instance) override;
  void FreeInstance(Instance *instance) override;
  void ClearInstance(Instance *instance) override;

  // Asynchronous execution.
  bool SupportsAsync() override { return true; }
  TaskFunc StartTaskFunc() override { return StartTask; }
  TaskFunc WaitTaskFunc() override { return WaitTask; }
  InstanceFunc SyncMainFunc() override { return SyncMain; }

  static void StartTask(Task *task);
  static void WaitTask(Task *task);
  static void SyncMain(void *instance);

  // Allocate CUDA instance in data instance block.
  int ExtraInstanceData(Cell *cell) override { return sizeof(CUDAInstance); }

  // Constant tensor copying.
  DevicePtr CopyTensorToDevice(Tensor *tensor) override;
  void RemoveTensorFromDevice(Tensor *tensor) override;

  // Instance tensor copying.
  void EmitCopyTensorToDevice(Tensor *tensor,
                              Cell *cell,
                              int taskidx,
                              MacroAssembler *masm) override;
  void EmitCopyTensorFromDevice(Tensor *tensor,
                                Cell *cell,
                                int taskidx,
                                MacroAssembler *masm) override;

 private:
  // CUDA device for computations.
  CUDADevice *device_;
};

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_CUDA_CUDA_RUNTIME_H_

