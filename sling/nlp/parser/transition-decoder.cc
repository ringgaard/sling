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

#include "sling/nlp/parser/transition-decoder.h"

#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"
#include "sling/nlp/parser/transition-generator.h"

namespace sling {
namespace nlp {

using namespace sling::myelin;

// Transition decoder version number.
static const int DECODER_VERSION = 0;

TransitionDecoder::~TransitionDecoder() {
  for (auto *d : delegates_) delete d;
}

void TransitionDecoder::Setup(task::Task *task, Store *commons) {
  // Get training parameters.
  task->Fetch("mark_depth", &mark_depth_);
  task->Fetch("mark_dim", &mark_dim_);
  task->Fetch("frame_limit", &frame_limit_);
  task->Fetch("history_size", &history_size_);
  task->Fetch("out_roles_size", &out_roles_size_);
  task->Fetch("in_roles_size", &in_roles_size_);
  task->Fetch("labeled_roles_size", &labeled_roles_size_);
  task->Fetch("unlabeled_roles_size", &unlabeled_roles_size_);
  task->Fetch("roles_dim", &roles_dim_);
  task->Fetch("activations_dim", &activations_dim_);
  task->Fetch("link_dim_token", &link_dim_token_);
  task->Fetch("link_dim_step", &link_dim_step_);
  task->Fetch("ff_l2reg", &ff_l2reg_);
}

static Flow::Variable *LinkedFeature(
    FlowBuilder *f, const string &name, Flow::Variable *embeddings,
    int size, int dim) {
  int link_dim = embeddings->dim(1);
  auto *features = f->Placeholder(name, DT_INT32, {size, 1});
  auto *oov = f->Parameter(name + "_oov", DT_FLOAT, {link_dim});
  auto *gather = f->Gather(embeddings, features, oov);
  auto *transform = f->Parameter(name + "_transform", DT_FLOAT,
                                 {link_dim, dim});
  f->RandomNormal(transform);
  return f->Reshape(f->MatMul(gather, transform), {1, size * dim});
}

void TransitionDecoder::Build(Flow *flow,  Flow::Variable *encodings,
                              bool learn) {
  // Get token enmbedding dimensions.
  int token_dim = encodings->elements();

  // Build parser decoder.
  FlowBuilder f(flow, "decoder");
  std::vector<Flow::Variable *> features;

  // Add inputs for recurrent channels.
  auto *tokens = f.Placeholder("tokens", DT_FLOAT, {1, token_dim}, true);
  auto *steps = f.Placeholder("steps", DT_FLOAT, {1, activations_dim_}, true);

  // Role features.
  if (roles_.size() > 0 && in_roles_size_ > 0) {
    features.push_back(f.Feature("in_roles", roles_.size() * frame_limit_,
                                 in_roles_size_, roles_dim_));
  }
  if (roles_.size() > 0 && out_roles_size_ > 0) {
    features.push_back(f.Feature("out_roles", roles_.size() * frame_limit_,
                                 out_roles_size_, roles_dim_));
  }
  if (roles_.size() > 0 && labeled_roles_size_ > 0) {
    features.push_back(f.Feature("labeled_roles",
                                 roles_.size() * frame_limit_ * frame_limit_,
                                 labeled_roles_size_, roles_dim_));
  }
  if (roles_.size() > 0 && unlabeled_roles_size_ > 0) {
    features.push_back(f.Feature("unlabeled_roles",
                                 frame_limit_ * frame_limit_,
                                 unlabeled_roles_size_, roles_dim_));
  }

  // Link features.
  features.push_back(LinkedFeature(&f, "token", tokens, 1, link_dim_token_));
  features.push_back(LinkedFeature(&f, "attention_tokens",
                                   tokens, frame_limit_, link_dim_token_));
  features.push_back(LinkedFeature(&f, "attention_steps",
                                   steps, frame_limit_, link_dim_step_));
  features.push_back(LinkedFeature(&f, "history",
                                   steps, history_size_, link_dim_step_));

  // Mark features.
  features.push_back(LinkedFeature(&f, "mark_tokens",
                                   tokens, mark_depth_, link_dim_token_));
  features.push_back(LinkedFeature(&f, "mark_steps",
                                   steps, mark_depth_, link_dim_step_));

  // Pad feature vector.
  const static int alignment = 16;
  int n = 0;
  for (auto *f : features) n += f->elements();
  if (n % alignment != 0) {
    int padding = alignment - n % alignment;
    auto *zeroes = f.Const(nullptr, DT_FLOAT, {1, padding});
    features.push_back(zeroes);
  }

  // Concatenate mapped feature inputs.
  auto *fv = f.Concat(features, 1);
  int fvsize = fv->elements();

  // Feed-forward layer.
  auto *W = f.Parameter("W0", DT_FLOAT, {fvsize, activations_dim_});
  auto *b = f.Parameter("b0", DT_FLOAT, {1, activations_dim_});
  f.RandomNormal(W);
  if (ff_l2reg_ != 0.0) W->SetAttr("l2reg", ff_l2reg_);
  auto *activation = f.Name(f.Relu(f.Add(f.MatMul(fv, W), b)), "activation");
  activation->set_in()->set_out()->set_ref();

  // Build function decoder gradient.
  Flow::Variable *dactivation = nullptr;
  if (learn) {
    Gradient(flow, f.func());
    dactivation = flow->GradientVar(activation);
  }

  // Build flows for delegates.
  for (Delegate *delegate : delegates_) {
    delegate->Build(flow, activation, dactivation, learn);
  }

  // Link recurrences.
  flow->Connect({tokens, encodings});
  flow->Connect({steps, activation});
  if (learn) {
    auto *dsteps = flow->GradientVar(steps);
    flow->Connect({dsteps, dactivation});
  }
}

void TransitionDecoder::Save(Flow *flow, Builder *spec) {
  spec->Set("type", "transition");
  spec->Set("version", DECODER_VERSION);
  spec->Set("frame_limit", frame_limit_);
  spec->Set("sentence_reset", sentence_reset_);

  Handles role_list(spec->store());
  roles_.GetList(&role_list);
  spec->Set("roles", Array(spec->store(), role_list));

  Array delegates(spec->store(), delegates_.size());
  for (int i = 0; i < delegates_.size(); ++i) {
    Builder delegate_spec(spec->store());
    delegates_[i]->Save(flow, &delegate_spec);
    delegates.set(i, delegate_spec.Create().handle());
  }
  spec->Set("delegates", delegates);
}

void TransitionDecoder::Load(Flow *flow, const Frame &spec) {
  // Initialize decoder.
  frame_limit_ = spec.GetInt("frame_limit", frame_limit_);
  sentence_reset_ = spec.GetBool("sentence_reset", sentence_reset_);

  // Check compatibility.
  int version = spec.GetInt("version", 0);
  CHECK_EQ(version, DECODER_VERSION)
      << "Unsupported transition decoder version";

  // Initialize roles.
  Array roles = spec.Get("roles").AsArray();
  if (roles.valid()) {
    for (int i = 0; i < roles.length(); ++i) {
      roles_.Add(roles.get(i));
    }
  }

  // Initialize cascade.
  Array delegates = spec.Get("delegates").AsArray();
  CHECK(delegates.valid());
  for (int i = 0; i < delegates.length(); ++i) {
    Frame delegate_spec(spec.store(), delegates.get(i));
    string type = delegate_spec.GetString("type");
    Delegate *delegate = Delegate::Create(type);
    delegate->Load(flow, delegate_spec);
    delegates_.push_back(delegate);
  }
}

void TransitionDecoder::Initialize(const Network &model) {
  // Get decoder cells and tensors.
  cell_ = model.GetCell("decoder");
  encodings_ = cell_->GetParameter("decoder/tokens");
  activations_ = cell_->GetParameter("decoder/steps");
  activation_ = cell_->GetParameter("decoder/activation");

  gcell_ = cell_->Gradient();
  if (gcell_ != nullptr) {
    primal_ = cell_->Primal();
    dencodings_ = encodings_->Gradient();
    dactivations_ = activations_->Gradient();
    dactivation_ = activation_->Gradient();
  }

  // Initialize delegates.
  for (auto *d : delegates_) d->Initialize(model);

  // Initialize feature model,
  feature_model_.Init(cell_, &roles_, frame_limit_);
}

void TransitionDecoder::GenerateTransitions(
    const Document &document, int begin, int end,
    Transitions *transitions) const {
  transitions->clear();
  Generate(document, begin, end, [&](const ParserAction &action) {
    transitions->push_back(action);
  });
}

TransitionDecoder::Predictor::Predictor(const TransitionDecoder *decoder)
  : decoder_(decoder),
    features_(&decoder->feature_model_, &state_),
    data_(decoder->cell_),
    activations_(decoder->feature_model_.activation()) {
  for (auto *d : decoder->delegates_) {
    delegates_.push_back(d->CreatePredictor());
  }
}

TransitionDecoder::Predictor::~Predictor() {
  for (auto *d : delegates_) delete d;
}

void TransitionDecoder::Predictor::Switch(Document *document) {
  state_.Switch(document, 0, document->length(), true);
  activations_.clear();
}

void TransitionDecoder::Predictor::Decode(int begin, int end,
                                          Channel *encodings) {
  // Reset parse state.
  DCHECK_EQ(end - begin, encodings->size());
  state_.Switch(state_.document(), begin, end, decoder_->sentence_reset_);
  if (decoder_->sentence_reset_) activations_.clear();

  // Run decoder to predict transitions.
  while (!state_.done()) {
    // Allocate space for next step.
    activations_.push();

    // Attach instance to recurrent layers.
    data_.Clear();
    features_.Attach(encodings, &activations_, &data_);

    // Extract features.
    features_.Extract(&data_);

    // Compute decoder activations.
    data_.Compute();

    // Run the cascade.
    ParserAction action(ParserAction::CASCADE, 0);
    int step = state_.step();
    float *activation = reinterpret_cast<float *>(activations_.at(step));
    int d = 0;
    for (;;) {
      delegates_[d]->Predict(activation, &action);
      if (action.type != ParserAction::CASCADE) break;
      CHECK_GT(action.delegate, d);
      d = action.delegate;
    }

    // Fall back to SHIFT if predicted action is not valid.
    if (!state_.CanApply(action)) {
      action.type = ParserAction::SHIFT;
    }

    // Apply action to parser state.
    state_.Apply(action);
  }
}

TransitionDecoder::Learner::Learner(const TransitionDecoder *decoder)
  : decoder_(decoder),
    features_(&decoder->feature_model_, &state_),
    gdecoder_(decoder->gcell_),
    activations_(decoder->activations_),
    dactivations_(decoder->dactivations_),
    dencodings_(decoder->dencodings_) {
  for (auto *d : decoder->delegates_) {
    delegates_.push_back(d->CreateLearner());
  }
}

TransitionDecoder::Learner::~Learner() {
  for (auto *d : decoders_) delete d;
  for (auto *d : delegates_) delete d;
  delete document_;
}

void TransitionDecoder::Learner::Switch(Document *document) {
  // Make an unannotated copy of the document.
  delete document_;
  document_ = new Document(*document, false);

  // Initialize parse state on unannotated document.
  state_.Switch(document_, 0, document_->length(), true);

  // Save golden document.
  golden_ = document;
}

Channel *TransitionDecoder::Learner::Learn(int begin, int end,
                                           Channel *encodings) {
  // Generate transitions for original sentence.
  decoder_->GenerateTransitions(*golden_, begin, end, &transitions_);

  // Compute the number of decoder steps.
  int steps = 0;
  for (const ParserAction &action : transitions_) {
    if (action.type != ParserAction::CASCADE) steps++;
  }

  // Reset parse state.
  DCHECK_EQ(end - begin, encodings->size());
  state_.Switch(document_, begin, end, decoder_->sentence_reset_);

  // Set up channels and instances for decoder.
  activations_.resize(steps);
  dactivations_.resize(steps);
  while (decoders_.size() < steps) {
    decoders_.push_back(new Instance(decoder_->cell_));
  }

  // Run decoder and delegates on all steps in the transition sequence.
  int t = 0;
  for (int s = 0; s < steps; ++s) {
    // Run next step of decoder.
    Instance *decoder = decoders_[s];
    activations_.zero(s);
    dactivations_.zero(s);

    // Attach instance to recurrent layers.
    decoder->Clear();
    features_.Attach(encodings, &activations_, decoder);

    // Extract features.
    features_.Extract(decoder);

    // Compute decoder activations.
    decoder->Compute();

    // Run the cascade.
    float *fwd = reinterpret_cast<float *>(activations_.at(s));
    float *bkw = reinterpret_cast<float *>(dactivations_.at(s));
    int d = 0;
    for (;;) {
      ParserAction &action = transitions_[t];
      float loss = delegates_[d]->Compute(fwd, bkw, action);
      loss_sum_ += loss;
      loss_count_++;
      if (action.type != ParserAction::CASCADE) break;
      CHECK_GT(action.delegate, d);
      d = action.delegate;
      t++;
    }

    // Apply action to parser state.
    state_.Apply(transitions_[t++]);
  }

  // Propagate gradients back through decoder.
  dencodings_.reset(end - begin);
  for (int step = steps - 1; step >= 0; --step) {
    gdecoder_.Set(decoder_->primal_, decoders_[step]);
    gdecoder_.Set(decoder_->dencodings_, &dencodings_);
    gdecoder_.Set(decoder_->dactivations_, &dactivations_);
    gdecoder_.Set(decoder_->dactivation_, &dactivations_, step);
    gdecoder_.Compute();
  }

  return &dencodings_;
}

void TransitionDecoder::Learner::UpdateLoss(float *loss_sum, int *loss_count) {
  *loss_sum += loss_sum_;
  *loss_count += loss_count_;
  loss_sum_ = 0.0;
  loss_count_ = 0;
}

void TransitionDecoder::Learner::CollectGradients(Instances *gradients) {
  gradients->Add(&gdecoder_);
  for (auto *d : delegates_) d->CollectGradients(gradients);
}

REGISTER_PARSER_DECODER("transition", TransitionDecoder);

}  // namespace nlp
}  // namespace sling

