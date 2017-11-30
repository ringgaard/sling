#ifndef SLING_NLP_DOCUMENT_PHRASE_TOKENIZER_H_
#define SLING_NLP_DOCUMENT_PHRASE_TOKENIZER_H_

#include "sling/base/types.h"
#include "sling/nlp/document/text-tokenizer.h"
#include "sling/string/text.h"

namespace sling {
namespace nlp {

class PhraseTokenizer {
 public:
  PhraseTokenizer();

  // Tokenize phrase into tokens.
  void Tokenize(Text text, std::vector<string> *tokens) const;

  // Tokenize phrase and return token fingerprints for each token.
  uint64 TokenFingerprints(Text text, std::vector<uint64> *tokens) const;

  // Compute fingerprint for phrase.
  uint64 Fingerprint(Text text) const;

 private:
  // Text tokenizer.
  Tokenizer tokenizer_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_DOCUMENT_PHRASE_TOKENIZER_H_

