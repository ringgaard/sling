#ifndef SLING_NLP_DOCUMENT_TOKENS_H_
#define SLING_NLP_DOCUMENT_TOKENS_H_

#include <string>
#include <vector>

#include "sling/base/types.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"

namespace sling {
namespace nlp {

class Tokens {
 public:
  // Initialize tokens from document.
  Tokens(const Frame &document);

  // Initialize tokens from token array.
  Tokens(const Array &tokens);

  // Locate token at byte position.
  int Locate(int position) const;

  // Return phrase for token span.
  string Phrase(int begin, int end) const;

 private:
  // Token array.
  sling::Array tokens_;

  // Start positions for tokens.
  std::vector<int> positions_;

  // Symbol names.
  Names names_;
  Name n_document_tokens_{names_, "/s/document/tokens"};
  Name n_token_start_{names_, "/s/token/start"};
  Name n_token_text_{names_, "/s/token/text"};
  Name n_token_break_{names_, "/s/token/break"};
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_DOCUMENT_TOKENS_H_

