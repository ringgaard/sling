#ifndef UTIL_WEB_RFC822_HEADERS_H_
#define UTIL_WEB_RFC822_HEADERS_H_

#include <string>
#include <utility>
#include <vector>

#include "base/types.h"
#include "stream/input.h"
#include "string/text.h"

namespace sling {

// Process RFC822 headers. RFC822 headers consists of a number of lines ending
// with an empty line. The first line is the "From" line, and the remaining
// lines are name/value pairs separated by colon.
class RFC822Headers : public std::vector<std::pair<Text, Text>> {
 public:
  // Parse RFC822 headers from input.
  bool Parse(Input *input);

  // Clear headers.
  void Clear();

  // Get value for header. Returns an empty text if the header is not found.
  Text Get(Text name) const;

  // First line of the header, i.e. the "From" line in RFC822.
  Text from() const { return from_; }

  // Return raw header buffer.
  const string &buffer() const { return buffer_; }

 private:
  // Header buffer.
  string buffer_;

  // First line of the header block.
  Text from_;
};

}  // namespace sling

#endif  // UTIL_WEB_RFC822_HEADERS_H_

