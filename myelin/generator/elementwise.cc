#include "myelin/generator/elementwise.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

ElementwiseIndexGenerator::ElementwiseIndexGenerator(Step *step) {
  // Get size from first output.
  CHECK_GE(step->outdegree(), 1);
  type_ = step->output(0)->type();
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
    if (it.scalar() && it.constant()) continue;
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

  // Save macro assembler for constant generation.
  masm_ = masm;
}

void ElementwiseIndexGenerator::EndLoop(MacroAssembler *masm) {
  if (!single_) {
    __ addq(offset_, Immediate(vecsize_));
    __ cmpq(offset_, Immediate(size_));
    __ j(less, &begin_);
  }
}

Operand ElementwiseIndexGenerator::addr(Express::Var *var) {
  if (var->type == Express::NUMBER) {
    // System-defined constant.
    switch (type_) {
      case DT_FLOAT: {
        float number = Express::NumericFlt32(var->id);
        int repeat = vecsize_ / sizeof(float);
        return masm_->GetConstant(number, repeat)->address();
      }
      case DT_DOUBLE: {
        double number = Express::NumericFlt64(var->id);
        int repeat = vecsize_ / sizeof(double);
        return masm_->GetConstant(number, repeat)->address();
      }
      default:
        LOG(FATAL) << "Unsupported constant type";
        return Operand(rbp);
    }
  } else {
    // Load input or output.
    DCHECK(Valid(var));
    Iterator &it =
        var->type == Express::OUTPUT ? output_[var->id] : input_[var->id];

    if (it.scalar()) {
      // Scalar variable.
      if (var->type == Express::CONST) {
        // Scalar constant in code block, vectorized if needed.
        CHECK(it.constant());
        int size = it.var->element_size();
        int repeat = vecsize_ / size;
        return masm_->GetData(it.var->data(), size, repeat)->address();
      } else if (it.base.is_valid()) {
        // Index scalar using base register.
        return Operand(it.base);
      } else {
        // Index scalar using offset in instance.
        return Operand(instance_, it.var->offset());
      }
    } else if (single_) {
      // Single iteration.
      if (it.base.is_valid()) {
        // Index single element using base register.
        return Operand(it.base);
      } else {
        // Index single element using offset in instance.
        return Operand(instance_, it.var->offset());
      }
    } else {
      // Multiple iterations.
      if (it.base.is_valid()) {
        // Index element using base register and index.
        return Operand(it.base, offset_);
      } else {
        // Index element using offset in instance and index.
        return Operand(instance_, offset_, times_1, it.var->offset());
      }
    }
  }
}

bool ElementwiseIndexGenerator::Valid(Express::Var *var) const {
  if (var->type == Express::OUTPUT) {
    return var->id >= 0 && var->id < output_.size();
  } else {
    return var->id >= 0 && var->id < input_.size();
  }
}

}  // namespace myelin
}  // namespace sling

