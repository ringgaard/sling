#include "myelin/generator/elementwise.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

ElementwiseIndexGenerator::ElementwiseIndexGenerator(Step *step) {
  // Get size from first output.
  CHECK_GE(step->outdegree(), 1);
  size_ = step->output(0)->size();

  // Allocate iterators for all inputs and outputs.
  input_.resize(step->indegree());
  for (int i = 0; i < step->indegree(); ++i) {
    input_[i].var = step->input(i);
  }
  output_.resize(step->outdegree());
  for (int i = 0; i < step->outdegree(); ++i) {
    output_[i].var = step->output(i);
  }
}

bool ElementwiseIndexGenerator::AllocateRegisters(MacroAssembler *masm) {
  // Allocate temp vars.
  if (!IndexGenerator::AllocateRegisters(masm)) return false;

  // Allocate register for offset.
  Registers &rr = masm->rr();
  if (!single_) {
    offset_ = rr.try_alloc();
    if (!offset_.is_valid()) return false;
  }
  instance_ = masm->instance();

  // Allocate registers for iterators.
  for (auto &it : input_) {
    if (it.var->offset() == -1 || it.var->ref()) {
      it.base = rr.try_alloc();
      if (!it.base.is_valid()) return false;
    }
  }
  for (auto &it : output_) {
    if (it.var->offset() == -1 || it.var->ref()) {
      it.base = rr.try_alloc();
      if (!it.base.is_valid()) return false;
    }
  }

  return true;
}

void ElementwiseIndexGenerator::BeginLoop(MacroAssembler *masm) {
  // Load tensor addresses.
  for (auto &it : input_) {
    if (it.base.is_valid()) {
      __ LoadTensorAddress(it.base, it.var);
    }
  }
  for (auto &it : output_) {
    if (it.base.is_valid()) {
      __ LoadTensorAddress(it.base, it.var);
    }
  }

  // Generate loop start, unless there is only one iteration.
  if (!single_) {
    __ xorq(offset_, offset_);
    __ bind(&begin_);
  }
}

void ElementwiseIndexGenerator::EndLoop(MacroAssembler *masm) {
  if (!single_) {
    __ addq(offset_, Immediate(vecsize_));
    __ cmpq(offset_, Immediate(size_));
    __ j(less, &begin_);
  }
}

Operand ElementwiseIndexGenerator::addr(Expression::Var *var) {
  DCHECK(Valid(var));
  Iterator &it =
      var->type == Expression::OUTPUT ? output_[var->id] : input_[var->id];
  if (single_) {
    if (it.base.is_valid()) {
      return Operand(it.base);
    } else {
      return Operand(instance_, it.var->offset());
    }
  } else {
    if (it.base.is_valid()) {
      return Operand(it.base, offset_);
    } else {
      return Operand(instance_, offset_, times_1, it.var->offset());
    }
  }
}

bool ElementwiseIndexGenerator::Valid(Expression::Var *var) const {
  if (var->type == Expression::OUTPUT) {
    return var->id >= 0 && var->id < output_.size();
  } else {
    return var->id >= 0 && var->id < input_.size();
  }
}

}  // namespace myelin
}  // namespace sling

