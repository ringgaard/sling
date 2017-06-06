#include "myelin/kernel/wavenet.h"

#include <algorithm>

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
    for (Flow::Operation *op : flow->Find({"ExpandDims",
                                           "Conv2D",
                                           "Squeeze",
                                           "Add"})) {
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

    for (Flow::Operation *op : flow->Find({"ExpandDims",
                                           "Conv2D",
                                           "Squeeze",
                                           "BiasAdd"})) {
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

    for (Flow::Operation *op : flow->Find({"ExpandDims",
                                           "Conv2D",
                                           "Squeeze"})) {
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

    return combines > 0;
  }
};

// Stub for RandomUniform.
class RandomUniform : public Kernel {
 public:
  string Name() override { return "DummyRandomUniform"; }
  string Operation() override { return "RandomUniform"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
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

// Stub for Add.
class Add : public Kernel {
 public:
  string Name() override { return "DummyAdd"; }
  string Operation() override { return "Add"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Adjust(Step *step) override {
    int a = step->input(0)->elements();
    int b = step->input(1)->elements();
    int c = step->output(0)->elements();
    bool shared = false;
    if (a == c) shared = step->AllowInPlace(0, 0);
    if (!shared && b == c) step->AllowInPlace(1, 0);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
  }

  int64 Complexity(const Step *step) override {
    CHECK_EQ(step->indegree(), 2);
    return std::max(step->input(0)->elements(), step->input(1)->elements());
  }
};

// Stub for Mul.
class Mul : public Kernel {
 public:
  string Name() override { return "DummyMul"; }
  string Operation() override { return "Mul"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
  }

  int64 Complexity(const Step *step) override {
    CHECK_EQ(step->indegree(), 2);
    return std::max(step->input(0)->elements(), step->input(1)->elements());
  }
};

// Stub for StridedSlice.
class StridedSlice : public Kernel {
 public:
  string Name() override { return "DummyStridedSlice"; }
  string Operation() override { return "StridedSlice"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
  }
};


// Stub for Pad.
class Pad : public Kernel {
 public:
  string Name() override { return "DummyPad"; }
  string Operation() override { return "Pad"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
  }
};

// Stub for Split.
class Split : public Kernel {
 public:
  string Name() override { return "DummySplit"; }
  string Operation() override { return "Split"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    __ nop();
  }
};

// Register WaveNet kernels.
void RegisterWaveNetKernels(Library *library) {
  library->Register(new RandomUniform());
  library->Register(new Conv1D());
  library->Register(new Conv1DAdd());
  library->Register(new Conv2DBackpropInput());

  library->Register(new Add());
  library->Register(new Mul());

  library->Register(new StridedSlice());
  library->Register(new Pad());
  library->Register(new Split());

  library->RegisterTransformer(new WaveNetTransformer());
}

}  // namespace myelin
}  // namespace sling

