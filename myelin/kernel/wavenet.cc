#include "myelin/kernel/wavenet.h"

#include <algorithm>
#include <random>

#include "myelin/compute.h"
#include "myelin/macro-assembler.h"

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


// Stub for ZigZagTanhMulSigmoid.
class ZigZagTanhMulSigmoid : public Kernel {
 public:
  string Name() override { return "ZigZagTanhMulSigmoid"; }
  string Operation() override { return "TanhMulSigmoid"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Adjust(Step *step) override {
    CHECK(step->AllowInPlace(1, 0, false));
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
  }
};

// Stub for Shift.
class Shift : public Kernel {
 public:
  string Name() override { return "DummyShift"; }
  string Operation() override { return "Shift"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
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

