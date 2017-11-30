#include <set>
#include <string>
#include <unordered_map>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/textmap.h"
#include "sling/nlp/document/phrase-tokenizer.h"
#include "sling/nlp/wiki/wiki.h"
#include "sling/task/frames.h"
#include "sling/task/task.h"
#include "sling/task/reducer.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

// Extract aliases from profiles.
class ProfileAliasExtractor : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    string lang = task->Get("language", "en");
    language_ = commons_->Lookup("/lang/" + lang);
  }

  void Process(Slice key, const Frame &frame) override {
    // Create frame with all aliases matching language.
    Builder a(frame.store());
    for (const Slot &s : frame) {
      if (s.name == n_profile_alias_) {
        Frame alias(frame.store(), s.value);
        if (alias.GetHandle(n_lang_) == language_) {
          a.Add(n_profile_alias_, alias);
        }
      } else if (s.name == n_instance_of_) {
        // Discard categories, disambiguations, info boxes and templates.
        if (s.value == n_category_ ||
            s.value == n_disambiguation_ ||
            s.value == n_infobox_ ||
            s.value == n_template_) {
          return;
        }
      }
    }

    // Output aliases matching language.
    Frame aliases = a.Create();
    if (aliases.size() != 0) {
      Output(key, aliases);
    }
  }

 private:
  // Symbols.
  Name n_lang_{names_, "lang"};
  Name n_profile_alias_{names_, "/s/profile/alias"};

  Name n_instance_of_{names_, "P31"};
  Name n_category_{names_, "Q4167836"};
  Name n_disambiguation_{names_, "Q4167410"};
  Name n_template_{names_, "Q11266439"};
  Name n_infobox_{names_, "Q19887878"};

  // Language for aliases.
  Handle language_;
};

REGISTER_TASK_PROCESSOR("profile-alias-extractor", ProfileAliasExtractor);

class ProfileAliasReducer : public task::Reducer {
 public:
  struct Alias {
    std::unordered_map<string, int> variants;
    int sources = 0;
    int count = 0;
  };

  void Start(task::Task *task) override {
    Reducer::Start(task);

    // Get parameters.
    string lang = task->Get("language", "en");
    language_ = commons_.Lookup("/lang/" + lang);
    names_.Bind(&commons_);
    commons_.Freeze();
    task->Fetch("anchor_threshold", &anchor_threshold_);

    // Read toxic aliases.
    TextMapInput aliases(task->GetInputFiles("toxic-aliases"));
    string alias;
    while (aliases.Read(nullptr, &alias, nullptr)) {
      uint64 fp = tokenizer_.Fingerprint(alias);
      toxic_aliases_.insert(fp);
    }
  }

  void Reduce(const task::ReduceInput &input) override {
    // Collect all the aliases for the item.
    Text qid = input.key();
    Store store(&commons_);
    std::unordered_map<uint64, Alias *> aliases;
    for (task::Message *message : input.messages()) {
      // Get next alias profile.
      Frame profile = DecodeMessage(&store, message);

      // Get all aliases from profile.
      for (const Slot &slot : profile) {
        if (slot.name != n_profile_alias_) continue;
        Frame alias(&store, slot.value);
        string name = alias.GetString(n_name_);
        int count = alias.GetInt(n_alias_count_, 1);
        int sources = alias.GetInt(n_alias_sources_);

        // Check that alias is valid UTF-8.
        if (!UTF8::Valid(name)) {
          VLOG(9) << "Skipping invalid alias for " << qid << ": " << name;
          continue;
        }

        // Compute fingerprint.
        uint64 fp = tokenizer_.Fingerprint(name);

        // Update alias table.
        Alias *a = aliases[fp];
        if (a == nullptr) {
          a = new Alias;
          aliases[fp] = a;
        }
        a->sources |= sources;
        a->count += count;
        a->variants[name] += count;
      }
    }

    // Select aliases.
    Builder merged(&store);
    for (auto it : aliases) {
      bool toxic = toxic_aliases_.count(it.first) != 0;
      Alias *alias = it.second;
      if (!SelectAlias(alias, toxic)) continue;

      // Find most common variant.
      int max_count = -1;
      string name;
      for (auto variant : alias->variants) {
        if (variant.second > max_count) {
          max_count = variant.second;
          name = variant.first;
        }
      }
      if (name.empty()) continue;

      // Add alias to output.
      Builder a(&store);
      a.Add(n_name_, name);
      a.Add(n_lang_, language_);
      a.Add(n_alias_count_, alias->count);
      a.Add(n_alias_sources_, alias->sources);
      merged.Add(n_profile_alias_, a.Create());
    }

    // Output alias profile.
    Output(input.shard(), task::CreateMessage(qid, merged.Create()));

    // Delete alias table.
    for (auto it : aliases) delete it.second;
  }

  // Check if alias should be selected.
  bool SelectAlias(Alias *alias, bool toxic) {
    // Keep aliases from trusted sources.
    if (alias->sources & (WIKIDATA_LABEL |
                          WIKIPEDIA_TITLE |
                          WIKIPEDIA_REDIRECT)) {
      return true;
    }

    // Only keep Wikidata alias if it is not toxic.
    if ((alias->sources & WIKIDATA_ALIAS) && !toxic) return true;

    // Disambiguation links need to be backed by anchors.
    if (alias->sources & WIKIPEDIA_DISAMBIGUATION) {
      if (alias->sources & WIKIPEDIA_ANCHOR) return true;
    }

    // Pure anchors need high counts to be selected.
    if (alias->sources & WIKIPEDIA_ANCHOR) {
      if (alias->count >= anchor_threshold_) return true;
    }

    return false;
  }

 private:
  // Alias source masks.
  enum AliasSourceMask {
    GENERIC = 1 << SRC_GENERIC,
    WIKIDATA_LABEL = 1 << SRC_WIKIDATA_LABEL,
    WIKIDATA_ALIAS = 1 << SRC_WIKIDATA_ALIAS,
    WIKIPEDIA_TITLE  = 1 << SRC_WIKIPEDIA_TITLE,
    WIKIPEDIA_REDIRECT = 1 << SRC_WIKIPEDIA_REDIRECT,
    WIKIPEDIA_ANCHOR = 1 << SRC_WIKIPEDIA_ANCHOR,
    WIKIPEDIA_DISAMBIGUATION = 1 << SRC_WIKIPEDIA_DISAMBIGUATION,
  };

  // Commons store.
  Store commons_;

  // Symbols.
  Names names_;
  Name n_name_{names_, "name"};
  Name n_lang_{names_, "lang"};
  Name n_profile_alias_{names_, "/s/profile/alias"};
  Name n_alias_count_{names_, "/s/alias/count"};
  Name n_alias_sources_{names_, "/s/alias/sources"};

  // Language.
  Handle language_;

  // Phrase tokenizer for computing phrase fingerprints.
  nlp::PhraseTokenizer tokenizer_;

  // Threshold for pure anchors.
  int anchor_threshold_ = 100;

  // Fingerprint for toxic aliases.
  std::set<uint64> toxic_aliases_;
};

REGISTER_TASK_PROCESSOR("profile-alias-reducer", ProfileAliasReducer);

}  // namespace nlp
}  // namespace sling

