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

#include "sling/nlp/kb/facts.h"
#include "sling/nlp/parser/action-table.h"
#include "sling/nlp/parser/multiclass-delegate.h"
#include "sling/nlp/parser/transition-decoder.h"
#include "sling/nlp/parser/transition-generator.h"

namespace sling {
namespace nlp {

using namespace task;
using namespace myelin;

// Relation types for parser.
static const char *relation_taxonomy[] = {
  "P31",    // instance of
  "P279",   // subclass of
  "P527",   // has part
  "P361",   // part of
  "P1365",  // replaces
  "P1366",  // replaced by
  "P17",    // country
  "P27",    // country of citizenship
  "P495",   // country of origin
  "P131",   // located in the administrative territorial entity
  "P159",   // headquarters location
  "P276",   // location
  "P551",   // residence
  "P740",   // location of formation
  "P115",   // home venue
  "P1532",  // country for sport
  "P69",    // educated at
  "P512",   // academic degree
  "P106",   // occupation
  "P39",    // position held
  "P108",   // employer
  "P54",    // member of sports team
  "P641",   // sport
  "P463",   // member of
  "P102",   // member of political party
  "P1142",  // political ideology
  "P140",   // religion
  "P413",   // position played on team / speciality
  "P101",   // field of work
  "P410",   // military rank
  "P241",   // military branch
  "P1416",  // affiliation
  "P166",   // award received
  "P169",   // chief executive officer
  "P1308",  // officeholder
  "P35",    // head of state
  "P6",     // head of government
  "P710",   // participant
  "P1344",  // participant in
  "P511",   // honorific prefix
  "P97",    // noble title
  "P585",   // point in time
  "P580",   // start time
  "P582",   // end time
  "P569",   // date of birth
  "P19",    // place of birth
  "P570",   // date of death
  "P20",    // place of death
  "P509",   // cause of death
  "P26",    // spouse
  "P451",   // unmarried partner
  "P22",    // father
  "P25",    // mother
  "P40",    // child
  "P3373",  // sibling
  "P112",   // founded by
  "P571",   // inception
  "P576",   // dissolved, abolished or demolished
  "P1830",  // owner of
  "P127",   // owned by
  "P176",   // manufacturer
  "P1037",  // director / manager
  "P488",   // chairperson
  "P749",   // parent organization
  "P355",   // subsidiary
  "P199",   // business division
  "P452",   // industry
  "P577",   // publication date
  "P175",   // performer
  "P161",   // cast member
  "P57",    // director
  "P50",    // author
  "P86",    // composer
  "P162",   // producer
  "P170",   // creator
  "P136",   // genre
  "P98",    // editor
  "P123",   // publisher
  "P6087",  // coach of sports team
  "P800",   // notable work
  "P1303",  // instrument
  "P264",   // record label
  "P118",   // league
  "P607",   // conflict
  "P137",   // operator
  nullptr
};

// Main delegate for coarse-grained SHIFT/MARK/CASCADE classification.
class MainDelegate : public MultiClassDelegate {
 public:
  MainDelegate(int other) : MultiClassDelegate("main") {
    // Set up coarse actions.
    actions_.Add(ParserAction(ParserAction::SHIFT));
    actions_.Add(ParserAction(ParserAction::MARK));
    for (int i = 0; i < other; ++i) {
      actions_.Add(ParserAction(ParserAction::CASCADE, i + 1));
    }
  }
};

// Delegate for evoking frames.
class EvokeDelegate : public MultiClassDelegate {
 public:
  EvokeDelegate(const ActionTable &actions) : MultiClassDelegate("evoke") {
    for (const ParserAction &action : actions.list()) {
      actions_.Add(action);
    }
  }
};

// Delegate for connecting frames.
class ConnectDelegate : public MultiClassDelegate {
 public:
  ConnectDelegate(const ActionTable &actions) : MultiClassDelegate("connect") {
    for (const ParserAction &action : actions.list()) {
      actions_.Add(action);
    }
  }
};

// Parser decoder for knowledge extraction.
class KnolexDecoder : public TransitionDecoder {
 public:
   // Set up KNOLEX parser model.
   void Setup(task::Task *task, Store *commons) override {
    // Set up transition decoder.
    TransitionDecoder::Setup(task, commons);

    // Reset parser state between sentences.
    sentence_reset_ = true;

    // Set up EVOKEs for all entity types.
    ActionTable evokes;
    FactCatalog catalog;
    catalog.Init(commons);
    Taxonomy *types = catalog.CreateEntityTaxonomy();
    evokes.Add(ParserAction::Evoke(0, Handle::nil()));
    evokes.Add(ParserAction::Evoke(1, Handle::nil()));
    for (auto &it : types->typemap()) {
      Handle type = it.first;
      evokes.Add(ParserAction::Evoke(0, type));
      evokes.Add(ParserAction::Evoke(1, type));
    }
    delete types;

    // Set up CONNECTs for all relation types.
    ActionTable connects;
    int depth = task->Get("attention_depth", 5);
    for (const char **rel = relation_taxonomy; *rel != nullptr; ++rel) {
      Handle relation = commons->LookupExisting(*rel);
      if (relation.IsNil()) {
        LOG(WARNING) << "Ignoring unknown relation: " << *rel;
        continue;
      }
      for (int d = 1; d <= depth; ++d) {
        connects.Add(ParserAction::Connect(0, relation, d));
        connects.Add(ParserAction::Connect(d, relation, 0));
      }
    }
    roles_.Add(connects.list());

    // Set up delegates.
    delegates_.push_back(new MainDelegate(2));
    delegates_.push_back(new EvokeDelegate(evokes));
    delegates_.push_back(new ConnectDelegate(connects));
  }

  // Transition generator.
  void GenerateTransitions(const Document &document, int begin, int end,
                           Transitions *transitions) const override {
    transitions->clear();
    Generate(document, begin, end, [&](const ParserAction &action) {
      if (action.type == ParserAction::EVOKE) {
        transitions->emplace_back(ParserAction::CASCADE, 1);
      } else if (action.type == ParserAction::CONNECT) {
        transitions->emplace_back(ParserAction::CASCADE, 2);
      }
      transitions->push_back(action);
    });
  }
};

REGISTER_PARSER_DECODER("knolex", KnolexDecoder);

}  // namespace nlp
}  // namespace sling

