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
    for (Flow::Operation *op : flow->Find("ExpandDims|Conv2D|Squeeze|Add")) {
      VLOG(5) << "Convert to Conv1DAdd " << op->name;
      Flow::Operation *add = op;
      Flow::Operation *squeeze = add->inputs[0]->producer;
      Flow::Operation *conv2d = squeeze->inputs[0]->producer;
      Flow::Operation *expand_dims = conv2d->inputs[0]->producer;
      string name = conv2d->name;
      flow->Fuse(expand_dims, conv2d, "");
      flow->Fuse(expand_dims, squeeze, "");
      flow->Fuse(expand_dims, add, "Conv1DAdd");
      expand_dims->name = name;
      combines++;
    }

    for (Flow::Operation *op : flow->Find("ExpandDims|Conv2D|Squeeze|Add")) {
      VLOG(5) << "Convert to Conv1DAdd " << op->name;
      Flow::Operation *add = op;
      Flow::Operation *squeeze = add->inputs[0]->producer;
      Flow::Operation *conv2d = squeeze->inputs[0]->producer;
      Flow::Operation *expand_dims = conv2d->inputs[0]->producer;
      string name = conv2d->name;
      flow->Fuse(expand_dims, conv2d, "");
      flow->Fuse(expand_dims, squeeze, "");
      flow->Fuse(expand_dims, add, "Conv1DAdd");
      expand_dims->name = name;
      combines++;
    }

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

// Stub for Conv1D.
class Conv1D : public Kernel {
 public:
  Conv1D() : bias_(false) {}
  Conv1D(bool bias) : bias_(bias) {}

  string Name() override { return "DummyConv1D"; }
  string Operation() override { return "Conv1D"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
  }

  int64 Complexity(const Step *step) override {
    Tensor *in = step->input(0);
    Tensor *filter = step->input(2);
    Tensor *out = step->output(0);
    int64 batch = in->dim(0);
    int64 out_width = out->dim(1);
    int64 filter_width, in_channels, out_channels;
    if (filter->rank() == 4) {
      filter_width = filter->dim(0) * filter->dim(1);
      in_channels = filter->dim(2);
      out_channels = filter->dim(3);
    } else {
      filter_width = filter->dim(0);
      in_channels = filter->dim(1);
      out_channels = filter->dim(2);
    }
    int64 bias = bias_ ? out_width : 0;
    return batch * out_width * filter_width * in_channels * out_channels + bias;
  }

 private:
  bool bias_;
};

// Stub for Conv1DAdd.
class Conv1DAdd : public Conv1D {
 public:
  Conv1DAdd() : Conv1D(true) {}

  string Name() override { return "DummyConv1DAdd"; }
  string Operation() override { return "Conv1DAdd"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
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
    if (CPU::Enabled(AVX2) && vecsize_ == 32) {
      YMMRegister a0 = ymmaux(0);
      YMMRegister a1 = ymmaux(1);
      YMMRegister b0 = ymmaux(2);
      YMMRegister b1 = ymmaux(3);
      YMMRegister tan = ymm(0);
      YMMRegister sig = ymm(1);

      __ vmovaps(a0, Operand(input_));      // [0 1 2 3 | 4 5 6 7]
      __ vmovaps(b0, Operand(input_, 32));  // [8 9 A B | C D E F]

      __ vpermq(a1, a0, 0x4E);         // [4 5 6 7 | 0 1 2 3] 01001110b = 0x4E
      __ vpermilps(a0, a0, 0xD8);      // [0 2 1 3 | 4 6 5 7] 11011000b = 0xD8
      __ vpermilps(a1, a1, 0x8D);      // [5 7 4 6 | 1 3 0 2] 10001101b = 0x8D
      __ vblendps(a0, a0, a1, 0x3C);   // [0 2 4 6 | 1 3 5 7] 00111100b = 0x3C
      __ vpermq(a1, a0, 0x4E);         // [1 3 5 7 | 0 2 4 6]

      __ vpermq(b1, b0, 0x4E);         // [C D E F | 8 9 A B]
      __ vpermilps(b0, b0, 0xD8);      // [8 A 9 B | C E D F]
      __ vpermilps(b1, b1, 0x8D);      // [D F C E | 9 B 8 A]
      __ vblendps(b0, b0, b1, 0x3C);   // [8 A C E | 9 B D F]
      __ vpermq(b1, b0, 0x4E);         // [9 B D F | 8 A C E]

      __ vblendps(tan, a0, b1, 0xF0);  // [0 2 4 6 | 8 A C E]
      __ vblendps(sig, a1, b0, 0xF0);  // [1 3 5 7 | 9 B D F]
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
    if (!CPU::Enabled(AVX2)) return false;
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
  library->Register(new Conv1DAdd());
  library->Register(new Conv2DBackpropInput());
  library->Register(new ZigZagTanhMulSigmoid());
  library->Register(new Shift());

  library->RegisterTransformer(new WaveNetTransformer());
}

}  // namespace myelin
}  // namespace sling

