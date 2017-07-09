#include "myelin/kernel/wavenet.h"

#include <algorithm>
#include <random>

#include "myelin/compute.h"
#include "myelin/macro-assembler.h"
#include "myelin/generator/index.h"
#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

class WaveNetTransformer : public Transformer {
 public:
  bool Transform(Flow *flow) override {
    // Convert 2D convolution to 1D convolution.
    int combines = 0;
    for (Flow::Operation *op : flow->Find("ExpandDims|Conv2D|Squeeze")) {
      VLOG(5) << "Convert to Conv1D " << op->name;
      Flow::Operation *squeeze = op;
      Flow::Operation *conv2d = squeeze->inputs[0]->producer;
      Flow::Operation *expand_dims = conv2d->inputs[0]->producer;
      string name = conv2d->name;
      flow->Fuse(expand_dims, conv2d, "");
      flow->Fuse(expand_dims, squeeze, "Conv1D");
      expand_dims->name = name;
      combines++;
    }

    // Fuse padding op to convolution.
    for (Flow::Operation *op : flow->Find("Conv1D|Pad")) {
      VLOG(5) << "Add padding Conv1D " << op->name;
      Flow::Operation *pad = op;
      Flow::Operation *conv1d = pad->inputs[0]->producer;
      flow->Fuse(conv1d, pad, "Conv1D");
      combines++;
    }

    // Convert concat into shift op.
    for (Flow::Operation *op : flow->Find("ConcatV2|StridedSlice")) {
      VLOG(5) << "Convert to Shift " << op->name;
      Flow::Operation *slice = op;
      Flow::Operation *concat = slice->inputs[0]->producer;
      flow->Fuse(concat, slice, "Shift");
      combines++;
    }

    // Convert split sigmoid and tanh to combined ops.
    for (Flow::Operation *op : flow->Find("Split|Tanh|Mul")) {
      VLOG(5) << "Convert to TanhMulSigmoid " << op->name;
      Flow::Operation *mul = op;
      Flow::Operation *tanh = mul->inputs[0]->producer;
      Flow::Operation *sigmoid = mul->inputs[1]->producer;
      Flow::Operation *split = tanh->inputs[0]->producer;
      if (sigmoid->inputs[0]->producer != split) continue;
      flow->Fuse(mul, tanh, "");
      flow->Fuse(mul, sigmoid, "");
      flow->Fuse(mul, split, "TanhMulSigmoid");
    }

    return combines > 0;
  }
};

// 1D convolution.
class Conv1D : public Kernel {
 public:
  // Maximum number of loop unrolls.
  static const int kMaxUnrolls = 8;

  string Name() override { return "WNConv1D"; }
  string Operation() override { return "Conv1D"; }

  bool Supports(Step *step) override {
    // Requires CPU with AVX support.
    if (!CPU::Enabled(AVX)) return false;

    // Check inputs and outputs.
    if (step->indegree() != 3 && step->indegree() != 4) return false;
    if (step->outdegree() != 1) return false;

    Tensor *in = step->input(0);
    Tensor *filter = step->input(2);
    Tensor *out = step->output(0);

    if (in->rank() != 3) return false;
    if (filter->rank() != 4) return false;
    if (out->rank() != 3) return false;

    if (in->dim(0) != out->dim(0)) return false;
    if (in->dim(1) != out->dim(1)) return false;
    if (in->dim(2) != filter->dim(2)) return false;
    if (out->dim(2) != filter->dim(3)) return false;

    if (in->type() != DT_FLOAT) return false;
    if (filter->type() != DT_FLOAT) return false;
    if (out->type() != DT_FLOAT) return false;

    // Output filter size must be a one or a multiple of 8.
    if (out->dim(2) != 1 && out->dim(2) % 8 != 0) return false;

    return true;
  }

  void Adjust(Step *step) override {
    Tensor *in = step->input(0);
    Tensor *filter = step->input(2);
    Tensor *out = step->output(0);

    // Align to one ymm register (256 bits, 32 bytes).
    int byte_alignment = 256 / 8;
    in->SetMiniumAlignment(byte_alignment);
    filter->SetMiniumAlignment(byte_alignment);
    out->SetMiniumAlignment(byte_alignment);

    in->SetRequiredOrder(ROW_MAJOR);
    filter->SetRequiredOrder(ROW_MAJOR);
    out->SetRequiredOrder(ROW_MAJOR);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    Tensor *in = step->input(0);
    Tensor *filter = step->input(2);
    Tensor *out = step->output(0);

    // Compute sizes.
    int64 batches = in->dim(0);
    int64 in_size = in->dim(1);
    int64 out_size = out->dim(1);
    int64 filter_size = filter->dim(0) * filter->dim(1);
    int64 in_channels = filter->dim(2);
    int64 out_channels = filter->dim(3);

    if (out_channels == 1) {
      LOG(INFO) << "Size 1 filter not yet implemented";
      __ nop();
      return;
    }

    // Compute the number of unrolls.
    int unrolls = 1;
    for (int i = 2; i <= kMaxUnrolls; ++i) {
      if (out_channels % (i * 8) == 0) unrolls = i;
    }
    int blocks = out_channels / (unrolls * 8);

    LOG(INFO) << "Batches " << batches
              << " in size: " << in_size
              << " out size: " << out_size
              << " filter size: " << filter_size
              << " in channels: " << in_channels
              << " out channels: " << out_channels
              << " unrolls: " << unrolls
              << " blocks: " << blocks;
    step->set_variant(std::to_string(unrolls) + "*" +
                      std::to_string(blocks) + " " +
                      std::to_string(filter->size() / 1024) + "k");

    // Allocate registers.
    Registers &rr = masm->rr();
    SIMDRegisters &mm = masm->mm();
    YMMRegister elem = mm.allocy();
    YMMRegister acc = mm.allocy();
    std::vector<YMMRegister> sum;
    for (int i = 0; i < unrolls; ++i) {
      sum.push_back(mm.allocy());
    }

    // Load tensor locations.
    Register input = rr.alloc();
    Register output = rr.alloc();
    Register filt = rr.alloc();
    __ LoadTensorAddress(input, in);
    __ LoadTensorAddress(output, out);
    __ LoadTensorAddress(filt, filter);

    // Compute filter end address.
    Register fend = rr.alloc();
    int filter_bytes = filter_size * in_channels * out_channels * sizeof(float);
    __ leaq(fend, Operand(filt, filter_bytes));

    // Loop over batches.
    Register batch = rr.alloc();
    Label batch_loop;
    if (batches > 0) {
      __ xorq(batch, batch);
      __ bind(&batch_loop);
    }

    // Loop over input rows.
    Register row = rr.alloc();
    Label row_loop;
    __ xorq(row, row);
    __ bind(&row_loop);

    // Loop over filter column blocks.
    Label filter_block_loop;
    Register fptr = rr.alloc();
    Register col = rr.alloc();
    if (blocks > 1) {
      __ xorq(col, col);
      __ bind(&filter_block_loop);
      __ leaq(fptr, Operand(filt, col, times_4));
    } else {
      __ movq(fptr, filt);
    }

    // Initialize block with zero.
    Register inptr = rr.alloc();
      __ movq(inptr, input);
    for (int i = 0; i < unrolls; ++i) {
      __ vxorps(sum[i], sum[i], sum[i]);
    }

    // Inner loop over filter rows.
    Label filter_row_loop;
    __ bind(&filter_row_loop);

    // Load x[row].
    __ vbroadcastss(elem, Operand(inptr));
    __ addq(inptr, Immediate(sizeof(float)));

    // Multiply x[row] with f[row,col:col+n] and add to sum.
    for (int i = 0; i < unrolls; ++i) {
      if (masm->Enabled(FMA3)) {
        __ vfmadd231ps(sum[i], elem, Operand(fptr, i * 32));
      } else {
        __ vmulps(acc, elem, Operand(fptr, i * 32));
        __ vaddps(sum[i], sum[i], acc);
      }
    }

    // Next filter row.
    __ addq(fptr, Immediate(filter->stride(2)));
    __ cmpq(fptr, fend);
    __ j(less, &filter_row_loop);

    // Save to y[col:col+n].
    for (int i = 0; i < unrolls; ++i) {
      __ vmovaps(Operand(output, i * 32), sum[i]);
    }
    __ addq(output, Immediate(unrolls * 32));

    // Next filter column block.
    if (blocks > 1) {
      __ addq(col, Immediate(unrolls * 32));
      __ cmpq(col, Immediate(out_channels * sizeof(float)));
      __ j(less, &filter_block_loop);
    }

    // Next input row.
    __ incq(row);
    __ addq(input, Immediate(in->stride(1)));
    __ cmpq(row, Immediate(in_size));
    __ j(less, &row_loop);

    // Next batch.
    if (batches > 0) {
      __ incq(batch);
      __ cmpq(batch, Immediate(batches));
      __ j(less, &batch_loop);
    }
  }

  int64 Complexity(const Step *step) override {
    Tensor *in = step->input(0);
    Tensor *filter = step->input(2);
    Tensor *out = step->output(0);
    int64 batch = in->dim(0);
    int64 out_size = out->dim(1);
    int64 filter_size = filter->dim(0) * filter->dim(1);
    int64 in_channels = filter->dim(2);
    int64 out_channels = filter->dim(3);
    return batch * out_size * filter_size * in_channels * out_channels * 2;
  }
};

// Stub for Conv2DBackpropInput.
class Conv2DBackpropInput : public Kernel {
 public:
  string Name() override { return "DummyConv2DBackpropInput"; }
  string Operation() override { return "Conv2DBackpropInput"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
  }
};


// Zigzag index generator for computing expression over even and odd elements.
class ZigZag : public IndexGenerator {
 public:
  ZigZag(Tensor *x, Tensor *y) : x_(x), y_(y) {}

  void Initialize(size_t vecsize) override {
    vecsize_ = vecsize;
    ReserveAuxYMMRegisters(4);
  }

  bool AllocateRegisters(MacroAssembler *masm) override {
    // Allocate temp vars.
    if (!IndexGenerator::AllocateRegisters(masm)) return false;

    // Allocate loop registers.
    input_ = masm->rr().try_alloc();
    if (!input_.is_valid()) return false;
    output_ = masm->rr().try_alloc();
    if (!output_.is_valid()) return false;
    count_ = masm->rr().try_alloc();
    if (!count_.is_valid()) return false;

    // Save macro assembler for constant generation.
    masm_ = masm;

    return true;
  }

  Operand addr(Express::Var *var) override {
    if (var->type == Express::NUMBER) {
      float number = Express::NumericFlt32(var->id);
      int repeat = vecsize_ / sizeof(float);
      return masm_->GetConstant(number, repeat)->address();
    } else if (var->type == Express::OUTPUT) {
      return Operand(output_);
    } else {
      UNSUPPORTED;
      return Operand(rbp);
    }
  }

  void *data(Express::Var *var) override {
    return nullptr;
  }

  void BeginLoop(MacroAssembler *masm) {
    // Load input and output tensors.
    __ LoadTensorAddress(input_, x_);
    __ LoadTensorAddress(output_, y_);

    // Initialize loop.
    __ xorq(count_, count_);
    __ bind(&loop_);

    // Read next two vectors from input and split into even and odd elements.
    if (CPU::Enabled(AVX) && vecsize_ == 32) {
      YMMRegister a0 = ymmaux(0);
      YMMRegister a1 = ymmaux(1);
      YMMRegister b0 = ymmaux(2);
      YMMRegister b1 = ymmaux(3);
      YMMRegister tan = ymm(0);
      YMMRegister sig = ymm(1);

      __ vmovaps(a0, Operand(input_));      // [0 1 2 3 | 4 5 6 7]
      __ vmovaps(b0, Operand(input_, 32));  // [8 9 A B | C D E F]

      __ vperm2f128(a1, a0, a0, 1);         // [4 5 6 7 | 0 1 2 3]
      __ vpermilps(a0, a0, 0xD8);           // [0 2 1 3 | 4 6 5 7]
      __ vpermilps(a1, a1, 0x8D);           // [5 7 4 6 | 1 3 0 2]
      __ vblendps(a0, a0, a1, 0x3C);        // [0 2 4 6 | 1 3 5 7]

      __ vperm2f128(b1, b0, b0, 1);         // [C D E F | 8 9 A B]
      __ vpermilps(b0, b0, 0xD8);           // [8 A 9 B | C E D F]
      __ vpermilps(b1, b1, 0x8D);           // [D F C E | 9 B 8 A]
      __ vblendps(b0, b0, b1, 0x3C);        // [8 A C E | 9 B D F]

      __ vperm2f128(tan, a0, b0, 0x20);     // [0 2 4 6 | 8 A C E]
      __ vperm2f128(sig, a0, b0, 0x31);     // [1 3 5 7 | 9 B D F]
    } else if (CPU::Enabled(SSE) && vecsize_ == 16) {
      XMMRegister a = xmmaux(0);
      XMMRegister b = xmmaux(1);
      XMMRegister tan = xmm(0);
      XMMRegister sig = xmm(1);

      __ movaps(a, Operand(input_));        // [0 1 2 3]
      __ movaps(b, Operand(input_, 16));    // [4 5 6 7]
      __ movaps(tan, a);                    // [0 1 2 3]
      __ shufps(tan, b, 0x88);              // [0 2 4 6]
      __ movaps(sig, a);                    // [0 1 2 3]
      __ shufps(sig, b, 0xDD);              // [1 3 5 7]
    } else {
      UNSUPPORTED;
    }
  }

  void EndLoop(MacroAssembler *masm) {
    __ addq(input_, Immediate(2 * vecsize_));
    __ addq(output_, Immediate(vecsize_));
    __ addq(count_, Immediate(vecsize_));
    __ cmpq(count_, Immediate(y_->size()));
    __ j(less, &loop_);
  }

 private:
  // Input and output.
  Tensor *x_;
  Tensor *y_;

  // Vector size.
  int vecsize_ = 1;

  // Loop registers.
  Register input_;
  Register output_;
  Register count_;

  // Loop label.
  Label loop_;

  // Assembler for generating code and data.
  MacroAssembler *masm_ = nullptr;
};

// ZigZagTanhMulSigmoid for computing Mul(Tanh(Even(x)), Sigmoid(Odd(x))).
class ZigZagTanhMulSigmoid : public Kernel {
 public:
  string Name() override { return "ZigZagTanhMulSigmoid"; }
  string Operation() override { return "TanhMulSigmoid"; }

  bool Supports(Step *step) override {
    if (step->indegree() != 2 || step->outdegree() != 1) return false;
    Tensor *input = step->input(1);
    Tensor *output = step->output(0);
    if (input->type() != DT_FLOAT) return false;
    if (output->type() != DT_FLOAT) return false;
    if (input->elements() != output->elements() * 2) return false;
    if (input->elements() % 16 != 0) return false;
    return true;
  }

  void Adjust(Step *step) override {
    CHECK(step->AllowInPlace(1, 0, false));
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Compile expression.
    Express expr;
    expr.Parse("@0=Mul(Tanh(!0),Sigmoid(!1))", true);

    // Initialize zigzag index generator.
    Tensor *input = step->input(1);
    Tensor *output = step->output(0);
    ZigZag zigzag(input, output);

    // Select expression generator.
    Type type = output->type();
    int elements = output->elements();
    ExpressionGenerator *generator =
        ExpressionGenerator::Select(expr, type, elements);
    CHECK(generator != nullptr);

    // Initialize expression and index generators.
    generator->Initalize(expr, type, 0, &zigzag);
    zigzag.AllocateRegisters(masm);

    // Generate loop.
    generator->GenerateInit(masm);
    zigzag.BeginLoop(masm);
    generator->GenerateBody(masm);
    zigzag.EndLoop(masm);

    delete generator;
  }

  int64 Complexity(const Step *step) override {
    Express expr;
    expr.Parse("@0=Mul(Tanh(!0),Sigmoid(!1))", true);
    return step->output(0)->elements() * expr.Complexity();
  }
};

// Shift input.
class Shift : public Kernel {
 public:
  string Name() override { return "Shift"; }
  string Operation() override { return "Shift"; }

  bool Supports(Step *step) override {
    if (step->indegree() != 6 || step->outdegree() != 1) return false;
    Tensor *input = step->input(1);
    Tensor *output = step->output(0);
    if (input->type() != DT_FLOAT) return false;
    if (output->type() != DT_FLOAT) return false;
    if (input->elements() != output->elements()) return false;

    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get input and output.
    Tensor *input = step->input(1);
    Tensor *output = step->output(0);

    // Allocate registers.
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);

    // Load output tensor.
    __ LoadTensorAddress(dst, output);

    // Append zero element to output.
    __ movl(Operand(dst), Immediate(0));
    __ addq(dst, Immediate(sizeof(float)));

    // Append input except last element to output.
    __ LoadTensorAddress(src, input);
    __ movq(cnt, Immediate(input->size() - sizeof(float)));
    __ repmovsb();
  }
};

// Simple random generator for generating noise.
static void SimpleRandomGenerator(const TensorData &shape,
                                  const TensorData &seed,
                                  TensorData *result) {
  int elements = result->shape().elements();
  uint32 prng = seed.value<int64>();
  float *random = &result->at<float>(0);
  for (int i = 0; i < elements; ++i) {
    int32 rnd = (((prng = prng * 214013L + 2531011L) >> 16) & 0x7fff);
    random[i] = rnd / 327678.0;
  }
}

// Random generator for generating noise.
static void RandomGenerator(const TensorData &shape,
                            const TensorData &seed,
                            TensorData *result) {
  int elements = result->shape().elements();
  int64 prng_seed = seed.value<int64>();
  float *random = &result->at<float>(0);


  std::mt19937_64 prng(prng_seed);
  std::uniform_real_distribution<float> unit(0.0, 1.0);
  for (int i = 0; i < elements; ++i) {
    random[i] = unit(prng);
  }
}

// Register WaveNet library.
void RegisterWaveNetLibrary(Library *library) {
  library->Register("RandomUniform", "RandomGenerator", RandomGenerator)
     .Input(0, DT_INT32, 1)
     .Input(1, DT_INT64, 0)
     .Output(0, DT_FLOAT);

  library->Register("RandomUniform", "SRandGenerator", SimpleRandomGenerator)
     .Input(0, DT_INT32, 1)
     .Input(1, DT_INT64, 0)
     .Output(0, DT_FLOAT);

  library->Register(new Conv1D());
  //library->Register(new Conv1DAdd());
  library->Register(new Conv2DBackpropInput());
  library->Register(new ZigZagTanhMulSigmoid());
  library->Register(new Shift());

  library->RegisterTransformer(new WaveNetTransformer());
}

}  // namespace myelin
}  // namespace sling

