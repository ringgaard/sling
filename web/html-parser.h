#ifndef WEB_HTML_PARSER_H_
#define WEB_HTML_PARSER_H_

#include "web/xml-parser.h"

namespace sling {

class HTMLParser : public XMLParser {
 public:
  HTMLParser() {}
  virtual ~HTMLParser() {}

  // Parse HTML from input and call callbacks.
  virtual bool Parse(Input *input);

  // Callbacks.
  virtual bool DocType(const char *str);

 private:
  // HTML tag type.
  enum TagType {REGULAR, UNPARSED, SINGLE, IMPLICIT};

  // Special tag type for tags starting with exclamation mark.
  enum SpecialTagType {EMPTY, COMMENT, DOCTYPE, CDATA};

  // Check if character is a HTML name character.
  static bool IsNameChar(int ch);

  // Get the type for an HTML tag.
  static TagType GetTagType(const char *tag);
};

}  // namespace sling

#endif  // WEB_HTML_PARSER_H_

