// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SLING_MYELIN_SIMD_ASSEMBLER_H_
#define SLING_MYELIN_SIMD_ASSEMBLER_H_

#include "sling/myelin/macro-assembler.h"

namespace sling {
namespace myelin {

// Code generator for SIMD vector instructions.
class SIMDGenerator {
 public:
  SIMDGenerator(MacroAssembler *masm, bool aligned)
      : masm_(masm), aligned_(aligned) {}
  virtual ~SIMDGenerator() = default;

  // Number of bytes per vector register.
  virtual int VectorBytes() = 0;

  // Number of elements per vector register.
  virtual int VectorSize() = 0;

  // Load memory into register.
  virtual void Load(int dst, const jit::Operand &src) = 0;

  // Store register into memory.
  virtual void Store(const jit::Operand &dst, int src) = 0;

  // Broadcast memory into register.
  virtual void Broadcast(int dst, const jit::Operand &src) = 0;

  // Clear register.
  virtual void Zero(int reg) = 0;

  // Add src1 and src2 and store it in dst.
  virtual void Add(int dst, int src1, int src2) = 0;
  virtual void Add(int dst, int src1, const jit::Operand &src2) = 0;

  // Multiply src1 and src2 and add it to dst, destroying contents of src1.
  virtual void MultiplyAdd(int dst, int src1, const jit::Operand &src2) = 0;

  // Horizontal sum of all elements in register.
  virtual void Sum(int reg) = 0;

  // Some vector instructions support masking (e.g. AVX512) that allow loading
  // and storing partial results.
  virtual bool SupportsMasking();
  virtual void SetMask(int bits);
  virtual void MaskedLoad(int dst, const jit::Operand &src);
  virtual void MaskedStore(const jit::Operand &dst, int src);

 protected:
  MacroAssembler *masm_;  // assembler for code generation
  bool aligned_;          // aligned load/store allowed
};

// Assembler for SIMD vector code generation. The main generator is used for
// for the (unrolled) bulk of the vector operation and the residual generators
// are used for successively smaller vector registers for handling the remaining
// elements ending with a handler for scalars.
class SIMDAssembler {
 public:
  SIMDAssembler(MacroAssembler *masm, Type type, bool aligned);
  ~SIMDAssembler();

  // Main generator.
  SIMDGenerator *main() const { return main_; }

  // Residual generators.
  const std::vector<SIMDGenerator *> residuals() const { return residuals_; }

  // The residual generator for scalars is always the last one.
  SIMDGenerator *scalar() const { return residuals_.back(); }

  // Check if type is supported.
  static bool Supports(Type type);

  // Return biggest vector size in bytes.
  static int VectorBytes(Type type);

  const string &name() const { return name_; }

 private:
  // Generator name.
  string name_;

  // Main SIMD code generator.
  SIMDGenerator *main_ = nullptr;

  // Code generator for residuals.
  std::vector<SIMDGenerator *> residuals_;
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_SIMD_ASSEMBLER_H_
