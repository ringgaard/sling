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

#include <math.h>
#include <unordered_map>
#include <vector>

#include "sling/base/types.h"
#include "sling/frame/store.h"
#include "sling/frame/object.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/learning.h"
#include "sling/nlp/kb/facts.h"
#include "sling/nlp/parser/parser-codec.h"

namespace sling {
namespace nlp {

using namespace sling::myelin;

// Biaffine decoder.
class BiaffineDecoder : public ParserDecoder {
 public:
  ~BiaffineDecoder() override {}

  // Set up biaffine decoder.
  void Setup(task::Task *task, Store *commons) override {
    // Get parameters.
    task->Fetch("max_sentence_length", &max_sentence_length_);
    task->Fetch("ff_dims", &ff_dims_);

    // Get entity types.
    FactCatalog catalog;
    catalog.Init(commons);
    Taxonomy *types = catalog.CreateEntityTaxonomy();
    type_map_[Handle::nil()] = 0;
    types_.push_back(Handle::nil());
    for (auto &it : types->typemap()) {
      Handle type = it.first;
      type_map_[type] = types_.size();
      types_.push_back(type);
    }
    delete types;
  }

  // Build model for biaffine decoder.
  void Build(Flow *flow, Flow::Variable *encodings, bool learn) override {
    // Get token enmbedding dimensions.
    int token_dim = encodings->elements();
    auto dt = encodings->type;

    // The number of labels is the number of types plus one additional label
    // for "no span".
    int K = types_.size() + 1;

    // Build biaffine scorer.
    FlowBuilder f(flow, "biaffine");

    // Add token encoding input. The input sentences are capped at a maximum
    // sentence length.
    auto *tokens = f.Placeholder("tokens", dt, {1, token_dim});
    tokens->set_dynamic()->set_unique();
    tokens = f.Resize(tokens, {max_sentence_length_, token_dim});

    // FFNNs for start and end.
    std::vector<int> layers = ff_dims_;
    auto *start = f.Name(FFNN(&f, tokens, layers, "S"), "start");
    auto *end = f.Name(FFNN(&f, tokens, layers, "E"), "end");

    // Bilinear mapping to compute scores.
    int L = max_sentence_length_;
    int D = layers.back();
    auto *U = f.Parameter("U", dt, {D, K * D});
    f.RandomNormal(U);
    auto *scores =
        f.Reshape(
          f.MatMul(
            f.Reshape(f.MatMul(start, U), {L * K, D}),
            f.Transpose(end)),
          {L, K, L});
    f.Name(scores, "scores");
    scores->set_out();

    // Build loss and loss gradient.
    FlowBuilder l(flow, "loss");

    auto *logits = l.Placeholder("logits", dt, scores->shape);
    logits->set_ref();
    auto *y = l.Placeholder("y", dt, scores->shape);

    // Compute softmax for logits.
    auto *softmax = l.SoftMax(logits, 1);
    auto *dlogits = l.Name(l.Sub(softmax, y), "d_logits");
    dlogits->set_ref();

    // Compute loss (negative log-likelihood).
    auto *loss = l.Mean(l.Neg(l.Log(l.Max(l.Mul(y, softmax), 1))));
    l.Name(loss, "loss");

    flow->Connect({scores, logits});

    if (learn) {
      Gradient(flow, f.func());
      auto *dscores = flow->GradientVar(scores);
      flow->Connect({dlogits, dscores});
    }
  }

  // Build FFNN for input transformation.
  static Flow::Variable *FFNN(FlowBuilder *f,
                              Flow::Variable *input,
                              std::vector<int> &layers,
                              const string &prefix) {
    Flow::Variable *v = input;
    for (int l = 0; l < layers.size(); ++l) {
      int height = v->dim(1);
      int width = layers[l];

      // Add weight matrix.
      string idx = std::to_string(l);
      auto *W = f->Parameter(prefix + "W" + idx, v->type, {height, width});
      auto *b = f->Parameter(prefix + "b" + idx, v->type, {width});
      f->RandomNormal(W);
      v = f->Add(f->MatMul(v, W), b);
      if (l != layers.size() - 1) v = f->Relu(v);
    }
    return v;
  }

  // Save model.
  void Save(Flow *flow, Builder *spec) override {
    spec->Set("type", "biaffine");
    spec->Set("types", Array(spec->store(), types_));
  }

  // Load model.
  void Load(Flow *flow, const Frame &spec) override {
    // Initialize types.
    Array types = spec.Get("types").AsArray();
    if (types.valid()) {
      for (int i = 0; i < types.length(); ++i) {
        types_.push_back(types.get(i));
      }
    }
  }

  // Initialize model.
  void Initialize(const Network &model) override {
  }

  // Biaffine decoder predictor.
  class BiaffinePredictor : public ParserDecoder::Predictor {
   public:
    BiaffinePredictor(const BiaffineDecoder *decoder)
        : decoder_(decoder) {}

    void Switch(Document *document) override {
      document_ = document;
    }

    void Decode(int begin, int end, Channel *encodings) override {
    }

  private:
    const BiaffineDecoder *decoder_;
    Document *document_;
  };

  Predictor *CreatePredictor() override {
    return new BiaffinePredictor(this);
  }

  // Biaffine decoder learner.
  class BiaffineLearner : public ParserDecoder::Learner {
   public:
    BiaffineLearner(const BiaffineDecoder *decoder)
        : decoder_(decoder) {}

    void Switch(Document *document) override {
    }

    Channel *Learn(int begin, int end, Channel *encodings) override {
      return nullptr;
    }

    void UpdateLoss(float *loss_sum, int *loss_count) override {
      *loss_sum += loss_sum_;
      *loss_count += loss_count_;
      loss_sum_ = 0.0;
      loss_count_ = 0;
    }

    void CollectGradients(Instances *gradients) override {
    }

   private:
    const BiaffineDecoder *decoder_;

    float loss_sum_ = 0.0;
    int loss_count_ = 0;
  };

  Learner *CreateLearner() override {
    return new BiaffineLearner(this);
  }

 private:
  // Entity types.
  std::vector<Handle> types_;
  HandleMap<int> type_map_;

  // Maximum sentence length.
  int max_sentence_length_ = 128;

  // Feed-forward hidden layer dimensions.
  std::vector<int> ff_dims_;
};

REGISTER_PARSER_DECODER("biaffine", BiaffineDecoder);

}  // namespace nlp
}  // namespace sling

