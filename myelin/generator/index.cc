#include "myelin/generator/index.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

bool IndexGenerator::RegisterOverflow(int *usage) {
  // Set up macro assembler for collecting register allocations.
  static const int kMaxRegisterUsage = 12;
  MacroAssembler masm(nullptr, 0);
  masm.rr().usage(kMaxRegisterUsage);

  // Let index generator allocate registers.
  if (!AllocateRegisters(&masm)) return false;

  // Compute the number of register used.
  *usage = kMaxRegisterUsage - masm.rr().num_free();
  return true;
}

bool IndexGenerator::AllocateRegisters(MacroAssembler *masm) {
  // Allocate fixed registers.
  bool ok = true;
  for (auto r : fixed_) {
    ok |= !masm->rr().used(r);
    masm->rr().alloc_fixed(r);
  }

  // Allocate temporary registers.
  for (auto &r : regs_) {
    r = masm->rr().try_alloc();
    if (!r.is_valid()) ok = false;
  }
  for (auto &m : mmregs_) {
    m = masm->mm().try_alloc();
    if (m == -1) ok = false;
  }

  // Allocate auxiliary registers.
  for (auto &r : aux_) {
    r = masm->rr().try_alloc();
    if (!r.is_valid()) ok = false;
  }
  for (auto &m : mmaux_) {
    m = masm->mm().try_alloc();
    if (m == -1) ok = false;
  }

  return ok;
}

void IndexGenerator::ReserveFixedRegister(jit::Register reg) {
  fixed_.push_back(reg);
}

void IndexGenerator::ReserveRegisters(int count) {
  for (int n = 0; n < count; ++n) {
    regs_.push_back(no_reg);
  }
}

void IndexGenerator::ReserveAuxRegisters(int count) {
  for (int n = 0; n < count; ++n) {
    aux_.push_back(no_reg);
  }
}

void IndexGenerator::ReserveXMMRegisters(int count) {
  for (int n = 0; n < count; ++n) {
    mmregs_.push_back(-1);
  }
}

void IndexGenerator::ReserveAuxXMMRegisters(int count) {
  for (int n = 0; n < count; ++n) {
    mmaux_.push_back(-1);
  }
}

void IndexGenerator::ReserveYMMRegisters(int count) {
  ReserveXMMRegisters(count);
}

void IndexGenerator::ReserveAuxYMMRegisters(int count) {
  ReserveAuxXMMRegisters(count);
}

}  // namespace myelin
}  // namespace sling

