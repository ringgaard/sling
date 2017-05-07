#include "myelin/cuda/cuda-runtime.h"

#include "base/logging.h"
#include "myelin/cuda/cuda.h"

namespace sling {
namespace myelin {

CUDARuntime::CUDARuntime(int device_number) {
  // Check that CUDA is supported.
  CHECK(CUDA::Supported());

  // Initialize CUDA device.
  if (device_number == -1) {
    // Select the CUDA device with the most cores.
    device_ = new CUDADevice(0);
    for (int d = 1; d < CUDA::Devices(); ++d) {
      CUDADevice *candidate = new CUDADevice(d);
      if (candidate->cores() > device_->cores()) {
        delete device_;
        device_ = candidate;
      } else {
        delete candidate;
      }
    }
  } else {
    device_ = new CUDADevice(device_number);
  }
}

CUDARuntime::~CUDARuntime() {
  // Close device.
  delete device_;
}

string CUDARuntime::Description() {
  return "CUDA device " + std::to_string(device_->number()) +
         ": " + device_->ToString();
}

void CUDARuntime::AllocateInstance(Instance *instance) {
  // Allocate host memory for instance.
  void *data;
  int rc = posix_memalign(&data, instance->alignment(), instance->size());
  CHECK_EQ(rc, 0);
  instance->set_data(reinterpret_cast<char *>(data));

  // Set up CUDA runtime instance block which is located at the start of the
  // host instance block.
  CUDAInstance *rt = reinterpret_cast<CUDAInstance *>(data);

  // Allocate device instance block.
  int size = instance->cell()->device_instance_size();
  if (size > 0) {
    CHECK_CUDA(cuMemAlloc(&rt->data, size));
  } else {
    rt->data = DEVICE_NULL;
  }

  // Allocate streams for tasks.
  CHECK_CUDA(cuStreamCreate(&rt->mainstream, CU_STREAM_NON_BLOCKING));
  for (int i = 0; i < instance->num_tasks(); ++i) {
    Task *task = instance->task(i);

    // Allocate stream for each asynchronous task and store it in the task
    // state.
    CUstream stream;
    CHECK_CUDA(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING));
    task->state = stream;
  }
}

void CUDARuntime::FreeInstance(Instance *instance) {
  // Deallocate instance memory on device.
  CUDAInstance *rt = reinterpret_cast<CUDAInstance *>(instance->data());
  if (rt->data != DEVICE_NULL) {
    CHECK_CUDA(cuMemFree(rt->data));
  }

  // Destroy CUDA streams for instance.
  CHECK_CUDA(cuStreamDestroy(rt->mainstream));
  for (int i = 0; i < instance->num_tasks(); ++i) {
    Task *task = instance->task(i);
    CUstream stream = static_cast<CUstream>(task->state);
    CHECK_CUDA(cuStreamDestroy(stream));
  }

  // Deallocate host memory for instance.
  free(instance->data());
}

void CUDARuntime::ClearInstance(Instance *instance) {
  memset(instance->data(), 0, instance->size());
}

void CUDARuntime::StartTask(Task *task) {
  // The task is run in the calling thread. All the CUDA kernels in the task
  // will be launched asynchronously so they might not yet have completed when
  // returning from the task function.
  task->func(task->arg);
}

void CUDARuntime::WaitTask(Task *task) {
  // Wait until all operations have completed in the task stream.
  CUstream stream = static_cast<CUstream>(task->state);
  CHECK_CUDA(cuStreamSynchronize(stream));
}

void CUDARuntime::SyncMain(void *instance) {
  CUDAInstance *rt = static_cast<CUDAInstance *>(instance);
  CHECK_CUDA(cuStreamSynchronize(rt->mainstream));
}

DevicePtr CUDARuntime::CopyTensorToDevice(Tensor *tensor) {
  // Allocate memory for constant tensor on device.
  DevicePtr dest;
  CHECK_CUDA(cuMemAlloc(&dest, tensor->space()));

  // Copy tensor data to device.
  CHECK_CUDA(cuMemcpyHtoD(dest, tensor->data(), tensor->space()));

  return dest;
}

void CUDARuntime::RemoveTensorFromDevice(Tensor *tensor) {
  CHECK_CUDA(cuMemFree(tensor->device_data()));
}

}  // namespace myelin
}  // namespace sling

