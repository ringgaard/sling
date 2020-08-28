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

#include "sling/myelin/kernel/avx.h"

#include <string>

#include "sling/myelin/compute.h"
#include "sling/myelin/macro-assembler.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Float dot product for CPUs with AVX.
class AVXFltDotProduct : public Kernel {
 public:
  // Maximum number of loop unrolls.
  static constexpr int kMaxUnrolls = 4;

  // Maximum number of adder registers.
  static constexpr int kMaxAdders = 4;

  string Name() override { return "AVXFltDotProduct"; }
  string Operation() override { return "MatMul"; }

  bool Supports(Step *step) override {
    // Requires CPU with AVX support.
    if (!CPU::Enabled(AVX)) return false;

    // Two tensor inputs and one tensor output.
    if (step->indegree() != 2 || step->outdegree() != 1) return false;
    Tensor *a = step->input(0);
    Tensor *b = step->input(1);
    Tensor *c = step->output(0);
    if (a->type() != DT_FLOAT) return false;
    if (b->type() != DT_FLOAT) return false;
    if (c->type() != DT_FLOAT) return false;
    if (a->elements() != b->elements()) return false;
    if (c->elements() != 1) return false;

    // Size of be multiple of YMM register size.
    if (a->elements() % 8  != 0) return false;

    // Horizontal summation is not strict math compatible.
    if (step->GetAttr("strict", false)) return false;

    return true;
  }

  int64 Complexity(const Step *step) override {
    return step->output(0)->elements() * 2;
  }

  void Adjust(Step *step) override {
    // Get input and output tensors.
    Tensor *a = step->input(0);
    Tensor *b = step->input(1);

    // Align to one SIMD register (256 bits, 32 bytes).
    bool avx512 = CPU::Enabled(AVX512F) && a->elements() % 16 == 0;
    a->SetMiniumAlignment(avx512 ? 64 : 32);
    b->SetMiniumAlignment(avx512 ? 64 : 32);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    Registers &rr = masm->rr();
    SIMDRegisters &mm = masm->mm();

    // Get input and output tensors.
    Tensor *a = step->input(0);
    Tensor *b = step->input(1);
    Tensor *c = step->output(0);

    // Get number of elements.
    int n = a->elements();

    // Compute the number of unrolls and adders.
    bool avx512 = masm->Enabled(AVX512F) && n % 16 == 0;
    int vecsize = avx512 ? 16 : 8;
    int unrolls = 0;
    for (int i = 1; i <= kMaxUnrolls; ++i) {
      int batch_size = i * vecsize;
      if (n >= batch_size && n % batch_size == 0) unrolls = i;
    }
    int adders = unrolls;
    if (adders < 1) adders = 1;
    if (adders > kMaxAdders) adders = kMaxAdders;
    string variant = "U" + std::to_string(unrolls);
    variant += "A" + std::to_string(adders);
    if (avx512) variant += "Z";
    step->set_variant(variant);

    // Allocate general registers.
    Register idx = rr.alloc();
    Register aptr = rr.alloc();
    Register bptr = rr.alloc();
    Register cptr = rr.alloc();

    // Allocate SIMD registers.
    std::vector<ZMMRegister> elem;
    for (int i = 0; i < std::max(unrolls, 1); ++i) {
      elem.push_back(mm.allocz(avx512));
    }
    std::vector<ZMMRegister> sum;
    for (int i = 0; i < adders; ++i) {
      sum.push_back(mm.allocz(avx512));
    }
    ZMMRegister acc = mm.allocz(avx512);

    // Load tensor locations.
    __ LoadTensorAddress(aptr, a);
    __ LoadTensorAddress(bptr, b);
    __ xorq(idx, idx);
    for (int i = 0; i < adders; ++i) {
      if (avx512) {
        __ vxorps(sum[i], sum[i], sum[i]);
      } else {
        __ vxorps(sum[i].ymm(), sum[i].ymm(), sum[i].ymm());
      }
    }

    // Outer loop over elements.
    Label l;
    __ LoopStart(&l);

    // Multiply and sum next batch.
    for (int i = 0; i < unrolls; ++i) {
      // Load a[idx:idx+n].
      int disp = vecsize * i * sizeof(float);
      if (avx512) {
        __ vmovaps(elem[i], Operand(aptr, idx, times_4, disp));
      } else {
        __ vmovaps(elem[i].ymm(), Operand(aptr, idx, times_4, disp));
      }
    }
    for (int i = 0; i < unrolls; ++i) {
      // Multiply a[idx:idx+n] with b[idx:idx+n] and add to sum.
      int disp = vecsize * i * sizeof(float);
      int a = i % adders;
      if (avx512) {
        __ vfmadd231ps(sum[a], elem[i], Operand(bptr, idx, times_4, disp));
      } else if (masm->Enabled(FMA3)) {
        __ vfmadd231ps(sum[a].ymm(), elem[i].ymm(),
                       Operand(bptr, idx, times_4, disp));
      } else {
        __ vmulps(elem[i].ymm(), elem[i].ymm(),
                  Operand(bptr, idx, times_4, disp));
        __ vaddps(sum[a].ymm(), sum[a].ymm(), elem[i].ymm());
      }
    }

    // Move to next batch.
    if (n > vecsize * unrolls) {
      __ addq(idx, Immediate(vecsize * unrolls));
      __ cmpq(idx, Immediate(n));
      __ j(less, &l);
    }

    // Sum adders in sum[0].
    if (avx512) {
      if (adders == 4) {
        __ vaddps(sum[0], sum[0], sum[2]);
        __ vaddps(sum[1], sum[1], sum[3]);
        __ vaddps(sum[0], sum[0], sum[1]);
      } else {
        for (int n = 1; n < adders; ++n) {
          __ vaddps(sum[0], sum[0], sum[n]);
        }
      }
    } else {
      if (adders == 4) {
        __ vaddps(sum[0].ymm(), sum[0].ymm(), sum[2].ymm());
        __ vaddps(sum[1].ymm(), sum[1].ymm(), sum[3].ymm());
        __ vaddps(sum[0].ymm(), sum[0].ymm(), sum[1].ymm());
      } else {
        for (int n = 1; n < adders; ++n) {
          __ vaddps(sum[0].ymm(), sum[0].ymm(), sum[n].ymm());
        }
      }
    }

    // Add elements in sum[0] horizontally.
    if (avx512) {
      __ Reduce(REDUCE_ADD, DT_FLOAT, sum[0], acc);
    } else {
      __ Reduce(REDUCE_ADD, DT_FLOAT, sum[0].ymm(), acc.ymm());
    }

    // Save result to c.
    __ LoadTensorAddress(cptr, c);
    if (avx512) {
      __ vmovss(Operand(cptr), sum[0]);
    } else {
      __ vmovss(Operand(cptr), sum[0].ymm());
    }
  }
};

// Float accumulating outer product for CPUs with AVX (C += A * B).
class AVXFltAssignAddOuter : public Kernel {
 public:
  // Block size.
  static constexpr int kRowRegs = 4;
  static constexpr int kColRegs = 4;

  string Name() override { return "AVXFltAssignAddOuter"; }
  string Operation() override { return "AssignAddMatMul"; }

  bool Supports(Step *step) override {
    // Requires CPU with AVX support.
    if (!CPU::Enabled(AVX)) return false;

    // Three matrix inputs.
    if (step->indegree() != 3 || step->outdegree() != 0) return false;
    Tensor *c = step->input(0);
    Tensor *a = step->input(1);
    Tensor *b = step->input(2);
    if (a->type() != DT_FLOAT || a->rank() != 2) return false;
    if (b->type() != DT_FLOAT || b->rank() != 2) return false;
    if (c->type() != DT_FLOAT || c->rank() != 2) return false;
    if (a->dim(1) != 1 || a->dim(0) != c->dim(0)) return false;
    if (b->dim(0) != 1 || b->dim(1) != c->dim(1)) return false;

    if (step->GetAttr("transpose_a", false)) return false;
    if (step->GetAttr("transpose_b", false)) return false;
    if (step->GetAttr("transpose_c", false)) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Get tensors.
    Tensor *c = step->input(0);
    Tensor *a = step->input(1);
    Tensor *b = step->input(2);

    // Align to SIMD register.
    bool avx512 = CPU::Enabled(AVX512F);
    int byte_alignment = avx512 ? 64 : 32;
    a->SetMiniumAlignment(byte_alignment);
    b->SetMiniumAlignment(byte_alignment);
    c->SetMiniumAlignment(byte_alignment);

    // Output must be row-major.
    c->RequireOrder(ROW_MAJOR);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get tensors.
    Tensor *c = step->input(0);
    Tensor *a = step->input(1);
    Tensor *b = step->input(2);

    // FMA is not strict math compatible.
    bool fma = masm->Enabled(FMA3) && !step->GetAttr("strict", false);
    bool avx512 = masm->Enabled(AVX512F);

    // Get matrix dimensions.
    int vecsize = avx512 ? 16 : 8;
    int rows = c->dim(0);
    int cols = c->dim(1);
    int rowsize = c->stride(0);
    int colblk = vecsize * kColRegs;
    int main_cols = (cols / colblk) * colblk;
    int remaining_cols = cols - main_cols;
    int main_rows = (rows / kRowRegs) * kRowRegs;

    // Allocate general registers.
    Register cptr = masm->rr().alloc();
    Register aptr = masm->rr().alloc();
    Register bptr = masm->rr().alloc();
    Register col = masm->rr().alloc();
    Register row = masm->rr().alloc();

    // Allocate SIMD registers.
    std::vector<ZMMRegister> areg;
    std::vector<ZMMRegister> breg;
    std::vector<ZMMRegister> creg;
    std::vector<ZMMRegister> acc;
    for (int i = 0; i < kRowRegs; ++i) {
      areg.push_back(masm->mm().allocz(avx512));
    }
    for (int i = 0; i < kColRegs; ++i) {
      breg.push_back(masm->mm().allocz(avx512));
      creg.push_back(masm->mm().allocz(avx512));
      acc.push_back(masm->mm().allocz(avx512));
    }

    // Load tensor locations.
    __ LoadTensorAddress(cptr, c);
    __ LoadTensorAddress(aptr, a);
    __ LoadTensorAddress(bptr, b);

    // Initialize mask.
    OpmaskRegister mask = masm->kk().alloc();
    if (avx512 && remaining_cols % 16 != 0) {
      __ LoadMask(remaining_cols % 16, mask);
    }

    // First compute rows in blocks (stage 0) and then the remaining ones one
    // row at a time (stage 1).
    __ xorq(row, row);
    for (int stage = 0; stage < 2; ++stage) {
      // Determine the row block size.
      int rowblk;
      bool single;
      bool more;
      if (stage == 0) {
        if (rows < kRowRegs) continue;
        rowblk = kRowRegs;
        single = (rows == kRowRegs);
        more = !single || rows % kRowRegs != 0;
      } else {
        if (rows % kRowRegs == 0) continue;
        rowblk = 1;
        single = (rows % kRowRegs == 1);
        more = !single;
      }

      // Outer loop over row blocks.
      Label l1;
      __ bind(&l1);

      // Load a[row] block.
      for (int r = 0; r < rowblk; ++r) {
        int disp = r * sizeof(float);
        if (avx512) {
          __ vbroadcastss(areg[r], Operand(aptr, row, times_4, disp));
        } else {
          __ vbroadcastss(areg[r].ymm(), Operand(aptr, row, times_4, disp));
        }
      }

      // Compute columns in blocks.
      if (main_cols > 0) {
        // Inner loop over column blocks.
        __ xorq(col, col);
        Label l2;
        __ bind(&l2);

        // Load b[col] block.
        for (int c = 0; c < kColRegs; ++c) {
          int disp = c * vecsize * sizeof(float);
          if (avx512) {
            __ vmovups(breg[c], Operand(bptr, col, times_4, disp));
          } else {
            __ vmovups(breg[c].ymm(), Operand(bptr, col, times_4, disp));
          }
        }

        // Multiply a[row] block with b[col] block and add to c[row,col] block.
        for (int r = 0; r < rowblk; ++r) {
          for (int c = 0; c < kColRegs; ++c) {
            int disp = r * rowsize + c * vecsize * sizeof(float);
            if (avx512) {
              __ vmovups(creg[c], Operand(cptr, col, times_4, disp));
              __ vfmadd231ps(creg[c], areg[r], breg[c]);
              __ vmovups(Operand(cptr, col, times_4, disp), creg[c]);
            } else {
              __ vmovups(creg[c].ymm(), Operand(cptr, col, times_4, disp));
              if (fma) {
                __ vfmadd231ps(creg[c].ymm(), areg[r].ymm(), breg[c].ymm());
              } else {
                __ vmulps(acc[c].ymm(), areg[r].ymm(), breg[c].ymm());
                __ vaddps(creg[c].ymm(), creg[c].ymm(), acc[c].ymm());
              }
              __ vmovups(Operand(cptr, col, times_4, disp), creg[c].ymm());
            }
          }
        }

        if (main_cols > vecsize * kColRegs) {
          __ addq(col, Immediate(vecsize * kColRegs));
          __ cmpq(col, Immediate(main_cols));
          __ j(less, &l2);
        }
      }

      // Compute remaining columns.
      int coldisp = main_cols * sizeof(float);
      int left = remaining_cols;
      if (avx512) {
        // First 16 floats at a time using AVX512 without masking.
        while (left >= 16) {
          // Load b[col].
          __ vmovups(breg[0], Operand(bptr, coldisp));

          // Multiply a[row] block with b[col] and add to c[row,col] block.
          for (int r = 0; r < rowblk; ++r) {
            int disp = r * rowsize + coldisp;
            __ vmovups(creg[0], Operand(cptr, disp));
            __ vfmadd231ps(creg[0], areg[r], breg[0]);
            __ vmovups(Operand(cptr, disp), creg[0]);
          }

          left -= 16;
          coldisp += 16 * sizeof(float);
        }

        // Compute remaining columns using AVX512 with masking.
        if (left > 0) {
          // Load b[col].
          __ vmovups(breg[0], Operand(bptr, coldisp), Mask(mask, zeroing));

          // Multiply a[row] block with b[col] and add to c[row,col] block.
          for (int r = 0; r < rowblk; ++r) {
            int disp = r * rowsize + coldisp;
            __ vmovups(creg[0], Operand(cptr, disp), Mask(mask, zeroing));
            __ vfmadd231ps(creg[0], areg[r], breg[0]);
            __ vmovups(Operand(cptr, disp), creg[0], Mask(mask, merging));
          }
        }
      } else {
        // First 8 floats at a time using AVX.
        while (left >= 8) {
          // Load b[col].
          __ vmovups(breg[0].ymm(), Operand(bptr, coldisp));

          // Multiply a[row] block with b[col] and add to c[row,col] block.
          for (int r = 0; r < rowblk; ++r) {
            int disp = r * rowsize + coldisp;
            __ vmovups(creg[0].ymm(), Operand(cptr, disp));
            if (fma) {
              __ vfmadd231ps(creg[0].ymm(), areg[r].ymm(), breg[0].ymm());
            } else {
              __ vmulps(acc[0].ymm(), areg[r].ymm(), breg[0].ymm());
              __ vaddps(creg[0].ymm(), creg[0].ymm(), acc[0].ymm());
            }
            __ vmovups(Operand(cptr, disp), creg[0].ymm());
          }

          left -= 8;
          coldisp += 8 * sizeof(float);
        }

        // Compute next four columns using SSE.
        if (left >= 4) {
          // Load b[col].
          __ vmovups(breg[0].xmm(), Operand(bptr, coldisp));

          // Multiply a[row] block with b[col] and add to c[row,col] block.
          for (int r = 0; r < rowblk; ++r) {
            int disp = r * rowsize + coldisp;
            __ vmovups(creg[0].xmm(), Operand(cptr, disp));
            if (fma) {
              __ vfmadd231ps(creg[0].xmm(), areg[r].xmm(), breg[0].xmm());
            } else {
              __ vmulps(acc[0].xmm(), areg[r].xmm(), breg[0].xmm());
              __ vaddps(creg[0].xmm(), creg[0].xmm(), acc[0].xmm());
            }
            __ vmovups(Operand(cptr, disp), creg[0].xmm());
          }

          left -= 4;
          coldisp += 4 * sizeof(float);
        }

        // Compute remaining remaining columns (0-3).
        while (left > 0) {
          // Load b[col].
          __ vmovss(breg[0].xmm(), Operand(bptr, coldisp));

          // Multiply a[row] block with b[col] and add to c[row,col] block.
          for (int r = 0; r < rowblk; ++r) {
            int disp = r * rowsize + coldisp;
            __ vmovss(creg[0].xmm(), Operand(cptr, disp));
            if (fma) {
              __ vfmadd231ss(creg[0].xmm(), areg[r].xmm(), breg[0].xmm());
            } else {
              __ vmulss(acc[0].xmm(), areg[r].xmm(), breg[0].xmm());
              __ vaddss(creg[0].xmm(), creg[0].xmm(), acc[0].xmm());
            }
            __ vmovss(Operand(cptr, disp), creg[0].xmm());
          }

          left -= 1;
          coldisp += sizeof(float);
        }
      }

      // Next row block.
      if (more) {
        __ addq(cptr, Immediate(rowblk * rowsize));
      }
      if (!single) {
        __ addq(row, Immediate(rowblk));
        __ cmpq(row, Immediate(stage == 0 ? main_rows : rows));
        __ j(less, &l1);
      }
    }
  }

  int64 Complexity(const Step *step) override {
    return step->input(0)->elements() * 2;
  }
};

void RegisterAVXMatMul(Library *library) {
  library->Register(new AVXFltDotProduct());
  library->Register(new AVXFltAssignAddOuter());
}

}  // namespace myelin
}  // namespace sling

