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

#include <functional>

#include "sling/nlp/parser/parser.h"

#include "sling/frame/serialization.h"
#include "sling/myelin/kernel/dragnn.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/features.h"
#include "sling/nlp/document/lexicon.h"
#include "sling/string/strcat.h"

using namespace std::placeholders;

namespace sling {
namespace nlp {

void Parser::Load(Store *store, const string &model) {
  // Load and analyze parser flow file.
  myelin::Flow flow;
  CHECK(flow.Load(model));

  // FIXME(ringgaard): Patch feature cell output.
  flow.Var("features/feature_vector")->set_in();

  // Register DRAGNN kernel to support legacy parser models.
  RegisterDragnnLibrary(compiler_.library());

  // Compile parser flow.
  compiler_.Compile(&flow, &network_);

  // Initialize lexical encoder.
  encoder_.Initialize(network_);
  encoder_.LoadLexicon(&flow);

  // Load commons and action stores.
  myelin::Flow::Blob *commons = flow.DataBlock("commons");
  CHECK(commons != nullptr);
  StringDecoder decoder(store, commons->data, commons->size);
  decoder.DecodeAll();

  // Read the cascade specification and implementation from the flow.
  Frame cascade_spec(store, "/cascade");
  CHECK(cascade_spec.valid());
  cascade_.Initialize(network_, cascade_spec);

  // Initialize action table.
  store_ = store;
  actions_.Init(store);
  cascade_.set_actions(&actions_);
  roles_.Init(actions_);

  // Initialize decoder feature model.
  myelin::Flow::Blob *spec = flow.DataBlock("spec");
  decoder_ = network_.GetCell("ff_trunk");
  feature_model_.Init(decoder_, spec, &roles_, actions_.frame_limit());
}

void Parser::Parse(Document *document) const {
  // Parse each sentence of the document.
  for (SentenceIterator s(document); s.more(); s.next()) {
    // Initialize parser model instance data.
    ParserInstance data(this, document, s.begin(), s.end());
    LexicalEncoderInstance &encoder = data.encoder_;

    // Run the lexical encoder.
    auto bilstm = encoder.Compute(*document, s.begin(), s.end());

    // Run FF to predict transitions.
    ParserState &state = data.state_;
    for (;;) {
      // Allocate space for next step.
      data.activations_.push();

      // Attach instance to recurrent layers.
      data.decoder_.Clear();
      data.AttachChannels(bilstm);

      // Extract features.
      data.ExtractDecoderFeatures();

      // Compute FF hidden layer.
      data.decoder_.Compute();

      // Apply the cascade.
      ParserAction action;
      data.cascade_.Compute(&data.activations_, &state, &action, data.trace_);
      state.Apply(action);

      // Check if we are done.
      if (action.type == ParserAction::STOP) break;
    }
    if (data.trace_ != nullptr) data.trace_->Write(document);
  }
}

ParserInstance::ParserInstance(const Parser *parser, Document *document,
                               int begin, int end)
    : parser_(parser),
      encoder_(parser->encoder()),
      state_(document, begin, end),
      features_(&parser->feature_model_, &state_),
      decoder_(parser->decoder_),
      activations_(parser->feature_model_.hidden()),
      cascade_(&parser->cascade_),
      trace_(nullptr) {
  // Reserve two transitions per token.
  int length = end - begin;
  activations_.reserve(length * 2);
  if (parser->trace()) {
    trace_ = new Trace();
    trace_->begin = begin;
    trace_->end = end;
    encoder_.set_trace(std::bind(&Trace::AddLSTM, trace_, _1, _2, _3));
  }
}

}  // namespace nlp
}  // namespace sling

