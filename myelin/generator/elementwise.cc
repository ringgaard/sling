#include "myelin/generator/elementwise.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

ElementwiseIndexGenerator::ElementwiseIndexGenerator(Step *step) {
  // Get size from first output.
  CHECK_GE(step->outdegree(), 1);
  type_ = step->output(0)->type();
  shape_ = step->output(0)->shape();

  // Allocate locators for all inputs and outputs.
  input_.resize(step->indegree());
  for (int i = 0; i < step->indegree(); ++i) {
    CHECK(step->input(i)->type() == type_);
    CHECK(InitializeLocator(step->input(i), &input_[i]));
  }
  output_.resize(step->outdegree());
  for (int i = 0; i < step->outdegree(); ++i) {
    CHECK(step->input(i)->type() == type_);
    CHECK(step->output(i)->shape() == step->output(0)->shape());
    CHECK(InitializeLocator(step->output(i), &output_[i]));
  }
}

ElementwiseIndexGenerator::~ElementwiseIndexGenerator() {
  for (auto *i : iterators_) delete i;
}

bool ElementwiseIndexGenerator::InitializeLocator(Tensor *var, Locator *loc) {
  // Set variable for locator.
  loc->var = var;

  // Determine iterator type for variable.
  if (var->elements() == 1) {
    // Variable only has one element; use a scalar/const iterator.
    loc->iterator = NewIterator(var->IsConstant() ? CONST : SCALAR);
  } else if (var->shape() == shape_) {
    // Variable has same shape as output; use simple iterator.
    loc->iterator = NewIterator(SIMPLE);
  } else {
    // Find common suffix between variable and output.
    CHECK_LE(var->rank(), shape_.rank());
    int n = 1;
    int d1 = var->rank() - 1;
    int d2 = shape_.rank() - 1;
    while (d1 >= 0) {
      int n1 = var->dim(d1);
      int n2 = shape_.dim(d2);
      if (n1 != n2) break;
      n *= n1;
      d1--;
      d2--;
    }

    if (n == var->elements()) {
      if (var->elements() == shape_.elements()) {
        // The variable shape prefix is a one vector so use a simple iterator.
        loc->iterator = NewIterator(SIMPLE);
      } else {
        // Variable shape is a suffix of the output shape; use a repeated
        // iterator.
        DCHECK(shape_.elements() % n == 0);
        loc->iterator = NewIterator(REPEAT);
        loc->iterator->size = n;
      }
    } else if (d1 >= 0 && d2 >= 0 && var->dim(d1) == 1) {
      // Create broadcast iterator.
      CHECK_EQ(var->elements() * shape_.dim(d2), shape_.elements());
      loc->iterator = NewIterator(BROADCAST);
      loc->iterator->size = n;
      loc->iterator->broadcast = shape_.dim(d2);
    } else {
      LOG(WARNING) << "Unsupported broadcast: " << var->name()
                   << " input: " << var->shape().ToString()
                   << " output: " << shape_.ToString();
      return false;
    }
  }

  return true;
}

void ElementwiseIndexGenerator::Initialize(size_t vecsize) {
  vecsize_ = vecsize;
  single_ = shape_.elements() <= vecsize_;
}

bool ElementwiseIndexGenerator::AllocateRegisters(MacroAssembler *masm) {
  // Allocate temp vars.
  if (!IndexGenerator::AllocateRegisters(masm)) return false;

  // Allocate register for output offset.
  Registers &rr = masm->rr();
  if (!single_) {
    offset_ = rr.try_alloc();
    if (!offset_.is_valid()) return false;
  }
  instance_ = masm->instance();

  // Allocate registers for locators.
  for (auto &loc : input_) {
    if (!AllocateLocatorRegisters(&loc, masm)) return false;
  }
  for (auto &loc : output_) {
    if (!AllocateLocatorRegisters(&loc, masm)) return false;
  }

  return true;
}

bool ElementwiseIndexGenerator::AllocateLocatorRegisters(
    Locator *loc, MacroAssembler *masm) {
  Registers &rr = masm->rr();
  switch (loc->iterator->type) {
    case SIMPLE:
    case SCALAR:
      // Allocate base register for non-instance variables.
      if (loc->var->offset() == -1 || loc->var->ref()) {
        loc->base = rr.try_alloc();
        if (!loc->base.is_valid()) return false;
      }
      break;
    case CONST:
      // Constants use pc-relative addressing, so no extra registers are needed.
      break;
    case REPEAT:
      // Allocate base register for non-instance variables.
      if (loc->var->offset() == -1 || loc->var->ref()) {
        loc->base = rr.try_alloc();
        if (!loc->base.is_valid()) return false;
      }

      // Allocate index register.
      loc->iterator->offset = rr.try_alloc();
      if (!loc->iterator->offset.is_valid()) return false;
      break;
    case BROADCAST:
      // TODO: add support for broadcast.
      LOG(FATAL) << "BROADCAST not yet implemented";
      return false;
    default:
      return false;
  };

  return true;
}

void ElementwiseIndexGenerator::BeginLoop(MacroAssembler *masm) {
  // Load tensor addresses.
  for (auto &loc : input_) {
    if (loc.base.is_valid()) {
      __ LoadTensorAddress(loc.base, loc.var);
    }
  }
  for (auto &loc : output_) {
    if (loc.base.is_valid()) {
      __ LoadTensorAddress(loc.base, loc.var);
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
    size_t size = TypeTraits::of(type_).size() * shape_.elements();
    __ addq(offset_, Immediate(vecsize_));
    __ cmpq(offset_, Immediate(size));
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
    // Get locator.
    DCHECK(Valid(var));
    Locator *loc = GetLocator(var);

    // Return operand for adressing variable.
    switch (loc->iterator->type) {
      case SIMPLE:
        if (single_) {
          // Single iteration.
          if (loc->base.is_valid()) {
            // Index single element using base register.
            return Operand(loc->base);
          } else {
            // Index single element using offset in instance.
            return Operand(instance_, loc->var->offset());
          }
        } else {
          // Multiple iterations.
          if (loc->base.is_valid()) {
            // Index element using base register and index.
            return Operand(loc->base, offset_);
          } else {
            // Index element using offset in instance and index.
            return Operand(instance_, offset_, times_1, loc->var->offset());
          }
        }
      case SCALAR:
        if (loc->base.is_valid()) {
          // Index scalar using base register.
          return Operand(loc->base);
        } else {
          // Index scalar using offset in instance.
          return Operand(instance_, loc->var->offset());
        }
      case CONST: {
        // Scalar constant in code block, vectorized if needed.
        DCHECK(loc->var->IsConstant());
        int size = loc->var->element_size();
        int repeat = vecsize_ / size;
        return masm_->GetData(loc->var->data(), size, repeat)->address();
      }
      case REPEAT:
        LOG(FATAL) << "REPEAT not yet implemented";
        return Operand(rbp);
        break;
      case BROADCAST:
        LOG(FATAL) << "BROADCAST not yet implemented";
        return Operand(rbp);
        break;
      default:
        LOG(FATAL) << "Unsupported iterator type";
        return Operand(rbp);
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

