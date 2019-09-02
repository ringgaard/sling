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

#include "sling/nlp/document/annotator.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/fingerprinter.h"

namespace sling {
namespace nlp {

using namespace task;

// Annotate anaphoric references like pronous and definite references.
class AnaphoraAnnotator : public Annotator {
 public:
  void Init(Task *task, Store *commons) override {
    // Bind symbols.
    CHECK(names_.Bind(commons));

    // Set up pronoun descriptors for language.
    string language = task->Get("language", "en");
    if (language == "en") {
      // English.
      AddPersonalPronoun("he", MASCULINE);
      AddPersonalPronoun("his", MASCULINE);
      AddPersonalPronoun("him", MASCULINE);
      AddPersonalPronoun("she", FEMININE);
      AddPersonalPronoun("her", FEMININE);
      AddPersonalPronoun("hers", FEMININE);
      AddDefiniteArticle("the");
    } else if (language == "da") {
      // Danish.
      AddPersonalPronoun("han", MASCULINE);
      AddPersonalPronoun("hans", MASCULINE);
      AddPersonalPronoun("hun", FEMININE);
      AddPersonalPronoun("hendes", FEMININE);
    } else {
      disabled_ = true;
    }
  }

  // Annotate relations in document.
  void Annotate(Document *document) override {
    // Skip annotation if anaphora resolution is not supported by language.
    if (disabled_) return;

    // Find all markables in the document.
    Store *store = document->store();
    std::vector<Markable> markables;
    int sentence = 0;
    int t = 0;
    while (t < document->length()) {
      // Increment current sentence number on begining of new sentence.
      const Token &token = document->token(t);
      if (token.brk() >= SENTENCE_BREAK)  {
        sentence++;
      }

      // Get top-level span at token.
      Markable markable;
      markable.sentence = sentence;
      markable.span = token.span();
      if (markable.span != nullptr) markable.span = markable.span->outer();

      // Check for anaphora trigger word.
      if (markable.span == nullptr || markable.span->length() == 1) {
        const auto f = triggers_.find(token.Fingerprint());
        if (f != triggers_.end()) {
          markable.pronoun = &f->second;
          if (markable.pronoun->personal) {
            // Get gender from pronoun descriptor.
            markable.gender = markable.pronoun->gender;

            // Add span for pronoun.
            if (markable.span == nullptr) {
              markable.span = document->AddSpan(t, t + 1);
            }

            // Try to find antecedent. Search backwards until a match is found
            // within the sentence window.
            int previous_sentence = sentence;
            int antecedent = -1;
            for (int a = markables.size() - 1; a >= 0; --a) {
              Markable &m = markables[a];

              // Stop searching when we cross sentence boundary and we have
              // either found a match or exceeded the search window.
              if (m.sentence != previous_sentence) {
                if (antecedent != -1) break;
                if (sentence - m.sentence > sentence_window_) break;
                previous_sentence = m.sentence;
              }

              // Check if antecedent matches reference.
              if (m.gender == markable.gender) antecedent = a;
            }

            // Add antecedent for markable.
            if (antecedent != -1) {
              Builder b(store);
              b.AddIs(markables[antecedent].entity);
              markable.span->Evoke(b.Create());
            }
          }
        }
      }

      // Try to determine gender for markable.
      if (markable.span != nullptr) {
        markable.entity = store->Resolve(markable.span->evoked());
        if (store->IsFrame(markable.entity)) {
          Frame frame(store, markable.entity);
          Handle gender = store->Resolve(frame.GetHandle(n_gender_));
          if (gender == n_male_) {
            markable.gender = MASCULINE;
          } else if (gender == n_female_) {
            markable.gender = FEMININE;
          }
        }
      }

      if (markable.span != nullptr) {
        // Add markable.
        markables.emplace_back(markable);

        // Go to next token after span.
        t = markable.span->end();
      } else {
        // Go to next token.
        t++;
      }
    }
  }

 private:
  // Pronoun gender.
  enum Gender {UNKNOWN, MASCULINE, FEMININE, NEUTRAL};

  // Pronoun descriptor. This also convers trigger words that might not
  // grammatically be pronouns like definite articles.
  struct Pronoun {
    Gender gender = NEUTRAL;   // grammatical gender.
    bool personal = false;     // reference to human
    bool definite = false;     // definite reference article
  };

  // A markable is a mention of an entity that can be an antecedent for a
  // reference. Pronouns are also markables themselves.
  struct Markable {
    Span *span = nullptr;               // token span for mention
    int sentence = 0;                   // sentence number for markable
    const Pronoun *pronoun = nullptr;   // pronoun descriptor for markable
    Gender gender = UNKNOWN;            // gender for markable
    Handle entity = Handle::nil();      // evoked entity
  };

  // Add person pronoun descriptor.
  void AddPersonalPronoun(Text word, Gender gender) {
    Pronoun &p = trigger(word);
    p.gender = gender;
    p.personal = true;
  }

  // Add descriptor for definite article.
  void AddDefiniteArticle(Text word) {
    Pronoun &p = trigger(word);
    p.definite = true;
  }

  // Get pronoun descriptor for word.
  Pronoun &trigger(Text word) {
    uint64 fp = Fingerprinter::Fingerprint(word);
    return triggers_[fp];
  }

  // Return name of gender.
  static const char *GenderName(Gender gender) {
    switch (gender) {
      case UNKNOWN: return "unknown";
      case MASCULINE: return "masculine";
      case FEMININE: return "feminine";
      case NEUTRAL: return "neutral";
      default: return "???";
    }
  }

  // The anaphora annotator is disabled for unsupported languages.
  bool disabled_ = false;

  // Mapping from word fingerprint to pronoun descriptor.
  std::unordered_map<uint64, Pronoun> triggers_;

  // Symbols.
  Names names_;
  Name n_gender_{names_, "P21"};
  Name n_male_{names_, "Q6581097"};
  Name n_female_{names_, "Q6581072"};

  // Hyperparameters.
  static const int sentence_window_ = 3;
};

REGISTER_ANNOTATOR("anaphora", AnaphoraAnnotator);

}  // namespace nlp
}  // namespace sling

