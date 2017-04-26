#ifndef MYELIN_COMPARE_KERNELS_H_
#define MYELIN_COMPARE_KERNELS_H_

#include <string>
#include <vector>

#include "base/types.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "third_party/jit/assembler.h"

namespace sling {
namespace myelin {

class KernelComparator {
 public:
  // Create kernel comparator for comparing a test kernel with a base kernel.
  KernelComparator(const Library &library,
                   const string &operation_name,
                   const string &test_kernel_name,
                   const string &base_kernel_name);

 protected:
  // Kernel library with kernels to be compares.
  const Library &library_;

  // Flow describing kernel operation.
  Flow flow_;
  Flow::Operation *op_;
  std::vector<Flow::Variable *> inputs_;
  std::vector<Flow::Variable *> outputs_;

  // Operation name.
  string operation_name_;

  // Base test and base kernel names.
  string test_kernel_name_;
  string base_kernel_name_;
};

class FltKernelComparator : public KernelComparator {
 public:
  FltKernelComparator(const Library &library,
                      const string &operation_name,
                      const string &test_kernel_name,
                      const string &base_kernel_name)
      : KernelComparator(library, operation_name,
                         test_kernel_name, base_kernel_name) {}

  // Add input.
  void AddInput(const string &name, const Shape &shape,
                float low, float high);

  // Add output.
  void AddOutput(const string &name, const Shape &shape, float tolerance);

  // Check test kernel by comparing it to the output of the base kernel.
  bool Check(int iterations);

 private:
  // Range for random input values.
  std::vector<float> low_;
  std::vector<float> high_;

  // Error tolerance for each output.
  std::vector<float> tolerance_;
};

class IntKernelComparator : public KernelComparator {
 public:
  IntKernelComparator(const Library &library,
                      const string &operation_name,
                      const string &test_kernel_name,
                      const string &base_kernel_name)
      : KernelComparator(library, operation_name,
                         test_kernel_name, base_kernel_name) {}

  // Add input.
  void AddInput(const string &name, const Shape &shape, Type type);

  // Add output.
  void AddOutput(const string &name, const Shape &shape, Type type);

  // Check test kernel by comparing it to the output of the base kernel.
  bool Check(int iterations);
};

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_COMPARE_KERNELS_H_

