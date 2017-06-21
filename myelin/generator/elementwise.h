#ifndef MYELIN_GENERATOR_ELEMENTWISE_H_
#define MYELIN_GENERATOR_ELEMENTWISE_H_

#include <vector>

#include "myelin/generator/index.h"

namespace sling {
namespace myelin {

class ElementwiseIndexGenerator : public IndexGenerator {
 public:
  // Create element-wise index generator for step.
  ElementwiseIndexGenerator(const Step *step);
  ~ElementwiseIndexGenerator();

  // Initialize index generator for vector size.
  void Initialize(size_t vecsize) override;

  // Allocate registers. Return false in case of register overflow.
  bool AllocateRegisters(MacroAssembler *masm) override;

  // Return operand for accessing memory variable.
  jit::Operand addr(Express::Var *var) override;

  // Generate start and end of loop.
  void BeginLoop(MacroAssembler *masm);
  void EndLoop(MacroAssembler *masm);

  // Whether only one iteration is needed.
  bool single() const { return single_; }

 private:
  enum IteratorType {SIMPLE, SCALAR, CONST, REPEAT, BROADCAST};
  struct Locator;
  struct Iterator;

  // Initialize locator for variable.
  bool InitializeLocator(Tensor *var, Locator *loc);

  // Allocate registers for locator.
  bool AllocateLocatorRegisters(Locator *loc, MacroAssembler *masm);

  // Get locator for variable.
  Locator *GetLocator(Express::Var *var) {
    return var->type == Express::OUTPUT ? &output_[var->id] : &input_[var->id];
  }

  // Check if variable is a valid index.
  bool Valid(Express::Var *var) const;

  // Return the output element size.
  size_t element_size() const {
    return TypeTraits::of(type_).size();
  }

  // Create new iterator.
  Iterator *NewIterator(IteratorType type) {
    Iterator *it = new Iterator(type);
    iterators_.push_back(it);
    return it;
  }

  // Iterator for looping over (vector) elements in tensor.
  struct Iterator {
    Iterator(IteratorType type) : type(type) {}

    IteratorType type;                   // iterator type
    size_t size = 0;                     // number of elements to iterate over
    size_t broadcast = 0;                // broadcast iterations
    jit::Register block = jit::no_reg;   // block base
    jit::Register offset = jit::no_reg;  // offset from base
    jit::Register repeat = jit::no_reg;  // broadcast counter
  };

  // Locator for generating address operands for variables.
  struct Locator {
    Tensor *var;                        // variable to iterate over
    jit::Register base = jit::no_reg;   // base address register
    Iterator *iterator = nullptr;       // iterator for iterating over elements
  };

  // Output type.
  Type type_;

  // Output shape.
  Shape shape_;

  // Vector size.
  size_t vecsize_ = 1;

  // Loop begin label.
  jit::Label begin_;

  // Instance pointer register.
  jit::Register instance_;

  // Output offset register.
  jit::Register offset_;

  // Whether only one iteration is needed.
  bool single_ = false;

  // Input and output locators.
  std::vector<Locator> input_;
  std::vector<Locator> output_;

  // Iterators.
  std::vector<Iterator *> iterators_;

  // Assembler for generating code and data.
  MacroAssembler *masm_ = nullptr;
};

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_GENERATOR_INDEX_H_

