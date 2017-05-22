#include "myelin/kernel/wavenet.h"

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
                                           "BiasAdd"})) {
      LOG(INFO) << "Convert to Conv1DAdd " << op->name;
      Flow::Operation *bias_add = op;
      Flow::Operation *squeeze = bias_add->inputs[0]->producer;
      Flow::Operation *conv2d = squeeze->inputs[0]->producer;
      Flow::Operation *expand_dims = conv2d->inputs[0]->producer;
      flow->Fuse(expand_dims, conv2d, "");
      flow->Fuse(expand_dims, squeeze, "");
      flow->Fuse(expand_dims, bias_add, "Conv1DAdd");
      combines++;
    }

    for (Flow::Operation *op : flow->Find({"ExpandDims",
                                           "Conv2D",
                                           "Squeeze"})) {
      LOG(INFO) << "Convert to Conv1D " << op->name;
      Flow::Operation *squeeze = op;
      Flow::Operation *conv2d = squeeze->inputs[0]->producer;
      Flow::Operation *expand_dims = conv2d->inputs[0]->producer;
      flow->Fuse(expand_dims, conv2d, "");
      flow->Fuse(expand_dims, squeeze, "Conv1D");
      combines++;
    }

    return combines > 0;
  }
};

// Register WaveNet kernels.
void RegisterWaveNetKernels(Library *library) {
  library->RegisterTransformer(new WaveNetTransformer());
}

}  // namespace myelin
}  // namespace sling

