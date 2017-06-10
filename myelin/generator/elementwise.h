#ifndef MYELIN_GENERATOR_ELEMENTWISE_H_
#define MYELIN_GENERATOR_ELEMENTWISE_H_

#include <vector>

#include "myelin/generator/index.h"

namespace sling {
namespace myelin {

class ElementwiseIndexGenerator : public IndexGenerator {
 public:
  // Initialize element-wise index generator for step.
  ElementwiseIndexGenerator(Step *step);

  // Set the vector step size (in bytes).
  void SetVectorSize(size_t vecsize) {
    vecsize_ = vecsize;
    single_ = size_ <= vecsize_;
  }

  // Allocate registers. Return false in case of register overflow.
  bool AllocateRegisters(MacroAssembler *masm) override;

  // Return operand for accessing memory variable.
  jit::Operand addr(Express::Var *var) override;

  // Generate start and end of loop.
  void BeginLoop(MacroAssembler *masm);
  void EndLoop(MacroAssembler *masm);

 private:
  // Check if variable is a valid index.
  bool Valid(Express::Var *var) const;

  // Iterator for looping over (vector) elements in tensor.
  struct Iterator {
    // Check for single element tensor.
    bool scalar() const { return var->elements() == 1; }

    Tensor *var;                       // tensor that is being iterated
    jit::Register base = jit::no_reg;  // base register for tensor
  };

  // Output size.
  size_t size_;

  // Vector size.
  size_t vecsize_ = 1;

  // Loop begin label.
  jit::Label begin_;

  // Instance pointer register.
  jit::Register instance_;

  // Main loop register.
  jit::Register offset_;

  // Whether only one iteration is needed.
  bool single_ = false;

  // Input and output iterators.
  std::vector<Iterator> input_;
  std::vector<Iterator> output_;
};

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_GENERATOR_INDEX_H_

