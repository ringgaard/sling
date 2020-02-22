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

#include <unordered_map>
#include <vector>

#include "sling/base/types.h"
#include "sling/frame/store.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/learning.h"
#include "sling/nlp/kb/facts.h"
#include "sling/nlp/parser/parser-codec.h"

namespace sling {
namespace nlp {

using namespace sling::myelin;

// BIO tag for sequence tagging.
struct BIO {
  enum Tag : int32 {
    OUTSIDE = 0,  // no chunk
    BEGIN = 1,    // begin new chunk
    INSIDE = 2,   // inside chunk started by BEGIN
    END = 3,      // end of chunk started by BEGIN optionally followed by INSIDE
    SINGLE = 4,   // singleton chunk; cannot follow BEGIN/INSIDE
  };

  // Initialize BIO tag.
  BIO(Tag tag, int type) : tag(tag), type(type)  {}

  // Initialize BIO tag from index. BIO tags are numbered as follows:
  //  0 = OUTSIDE
  //  1 = BEGIN(0), 2 = INSIDE(0), 3 = END(0), 4 = SINGLE(0)
  //  5 = BEGIN(1), 6 = INSIDE(1), 7 = END(1), 8 = SINGLE(1)
  //  ...
  BIO(int index) {
    if (index != 0) {
      tag = static_cast<Tag>((index - 1) % 4 + 1);
      type = (index - 1) / 4;
    }
  }

  // Compute the number of tags for a given number of types.
  static int tags(int types) { return 1 + 4 * types; }

  Tag tag = OUTSIDE;  // basic tag action
  int type = 0;       // tag type
};

// BIO tagging decoder.
class BIODecoder : public ParserDecoder {
 public:
  ~BIODecoder() override {}

  // Set up BIO decoder.
  void Setup(task::Task *task, Store *commons) override {
    // Get parameters.
    task->Fetch("max_sentence", &max_sentence_);
    task->Fetch("ff_dims", &max_sentence_);

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
    num_tags_ = BIO::tags(types_.size());
  }

  // Build model for BIO decoder.
  void Build(Flow *flow, Flow::Variable *encodings, bool learn) override {
    // Get token enmbedding dimensions.
    int token_dim = encodings->elements();

    // Build tagger.
    FlowBuilder f(flow, "tagger");

    // Add token encoding input.
    auto *tokens = f.Placeholder("tokens", encodings->type, encodings->shape);
    tokens->set_dynamic()->set_unique();

    // Resize token inputs to the maximum sentence length.
    auto *sentence = f.Resize(tokens, {max_sentence_, token_dim});
    f.Name(sentence, "sentence");

    // Feed-forward layer(s).
    std::vector<int> layers = ff_dims_;
    layers.push_back(num_tags_);
    auto *scores = f.Name(f.FNN(sentence, layers, true), "scores");
    scores->set_out();

    // Build tagger gradient.
    if (learn) {
      Gradient(flow, f.func());
      auto *dscores = flow->GradientVar(scores);
      loss_.Build(flow, scores, dscores);
    }

    // Link recurrences.
    flow->Connect({tokens, encodings});
  }

  // Save model.
  void Save(Flow *flow, Builder *spec) override {
  }

  // Load model.
  void Load(Flow *flow, const Frame &spec) override {
  }

  // Initialize model.
  void Initialize(const Network &model) override {
  }

  // Decoder predictor.
  class Predictor : public ParserDecoder::Predictor {
   public:
    Predictor(const BIODecoder *decoder) : decoder_(decoder) {}
    ~Predictor() override  {}

    void Switch(Document *document) override {
    }

    void Decode(int begin, int end, Channel *encodings) override {
    }

  private:
    const BIODecoder *decoder_;
  };

  Predictor *CreatePredictor() override { return new Predictor(this); }

  // Decoder learner.
  class Learner : public ParserDecoder::Learner {
   public:
    Learner(const BIODecoder *decoder) : decoder_(decoder) {}
    ~Learner() {}

    void Switch(Document *document) override {
    }

    Channel *Learn(int begin, int end, Channel *encodings) override {
      return nullptr;
    }

    void UpdateLoss(float *loss_sum, int *loss_count) override {
    }

    void CollectGradients(Instances *gradients) override {
    }

   private:
    const BIODecoder *decoder_;
  };

  Learner *CreateLearner() override { return new Learner(this); }

 private:
  // Entity types.
  std::vector<Handle> types_;
  HandleMap<int> type_map_;

  // Number of BIO tags.
  int num_tags_;

  // Maximum sentence length.
  int max_sentence_ = 128;

  // Feed-forward hidden layer dimensions.
  std::vector<int> ff_dims_;

  // Tagger model.
  myelin::Cell *cell_ = nullptr;
  myelin::Tensor *encodings_ = nullptr;
  myelin::Tensor *scores_ = nullptr;

  myelin::Cell *gcell_ = nullptr;
  myelin::Tensor *primal_ = nullptr;
  myelin::Tensor *dencodings_ = nullptr;
  myelin::Tensor *dscores_ = nullptr;

  // Loss function.
  myelin::CrossEntropyLoss loss_;
};

REGISTER_PARSER_DECODER("bio", BIODecoder);

}  // namespace nlp
}  // namespace sling

