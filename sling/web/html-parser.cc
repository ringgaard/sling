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

#include "sling/web/html-parser.h"

#include <strings.h>

#include "sling/base/types.h"
#include "sling/util/unicode.h"
#include "sling/web/entity-ref.h"

namespace sling {

bool HTMLParser::IsNameChar(int ch) {
  if (ch < 0) return false;
  if (ch == ' ') return false;
  if (ch == '\n') return false;
  if (ch == '\r') return false;
  if (ch == '\t') return false;
  if (ch == '=') return false;
  if (ch == '"') return false;
  if (ch == '\'') return false;
  if (ch == '/') return false;
  if (ch == '<') return false;
  if (ch == '>') return false;
  if (ch == '&') return false;
  return true;
}

HTMLParser::TagType HTMLParser::GetTagType(const char *tag) {
  switch (*tag) {
    case 'a': case 'A':
      if (strcasecmp(tag, "area") == 0) return SINGLE;
      break;

    case 'b': case 'B':
      if (strcasecmp(tag, "base") == 0) return SINGLE;
      if (strcasecmp(tag, "body") == 0) return IMPLICIT;
      if (strcasecmp(tag, "br") == 0) return SINGLE;
      break;

    case 'c': case 'C':
      if (strcasecmp(tag, "col") == 0) return SINGLE;
      if (strcasecmp(tag, "colgroup") == 0) return IMPLICIT;
      if (strcasecmp(tag, "command") == 0) return SINGLE;
      break;

    case 'd': case 'D':
      if (strcasecmp(tag, "dd") == 0) return IMPLICIT;
      if (strcasecmp(tag, "dt") == 0) return IMPLICIT;
      break;

    case 'e': case 'E':
      if (strcasecmp(tag, "embed") == 0) return SINGLE;
      break;

    case 'h': case 'H':
      if (strcasecmp(tag, "head") == 0) return IMPLICIT;
      if (strcasecmp(tag, "hr") == 0) return SINGLE;
      if (strcasecmp(tag, "html") == 0) return IMPLICIT;
      break;

    case 'i': case 'I':
      if (strcasecmp(tag, "img") == 0) return SINGLE;
      if (strcasecmp(tag, "input") == 0) return SINGLE;
      break;

    case 'k': case 'K':
      if (strcasecmp(tag, "keygen") == 0) return SINGLE;
      break;

    case 'l': case 'L':
      if (strcasecmp(tag, "li") == 0) return IMPLICIT;
      if (strcasecmp(tag, "link") == 0) return SINGLE;
      break;

    case 'm': case 'M':
      if (strcasecmp(tag, "meta") == 0) return SINGLE;
      break;

    case 'o': case 'O':
      if (strcasecmp(tag, "option") == 0) return IMPLICIT;
      break;

    case 'p': case 'P':
      if (strcasecmp(tag, "p") == 0) return IMPLICIT;
      if (strcasecmp(tag, "param") == 0) return SINGLE;
      if (strcasecmp(tag, "pre") == 0) return UNPARSED;
      break;

    case 's': case 'S':
      if (strcasecmp(tag, "script") == 0) return UNPARSED;
      if (strcasecmp(tag, "source") == 0) return SINGLE;
      break;

    case 't': case 'T':
      if (strcasecmp(tag, "tbody") == 0) return IMPLICIT;
      if (strcasecmp(tag, "td") == 0) return IMPLICIT;
      if (strcasecmp(tag, "tfoot") == 0) return IMPLICIT;
      if (strcasecmp(tag, "th") == 0) return IMPLICIT;
      if (strcasecmp(tag, "thead") == 0) return IMPLICIT;
      if (strcasecmp(tag, "tr") == 0) return IMPLICIT;
      if (strcasecmp(tag, "track") == 0) return UNPARSED;
      break;

    case 'w': case 'W':
      if (strcasecmp(tag, "wbr") == 0) return UNPARSED;
      break;
  }

  return REGULAR;
}

bool HTMLParser::Parse(Input *input) {
  int ch;
  string scratch;

  // Initialize document state.
  Init(input);
  if (!StartDocument()) return false;

  // Process HTML input.
  ch = ReadChar();
  while (ch >= 0) {
    if (ch != '<') {
      // Process text.
      if (ch == '&') {
        // Process entity reference.
        string &entref = scratch;
        entref.clear();
        entref.push_back('&');
        ch = ReadChar();
        while (ch >= 0 && ch != ';' && ch != '<' && ch != '&' &&
               ch != ' ' && ch != '\n') {
          entref.push_back(ch);
          ch = ReadChar();
        }
        entref.push_back(';');

        int code = ParseEntityRef(entref);
        if (code < 0) {
          AddString(entref.data());
        } else {
          char utf[UTF8::MAXLEN + 1];
          int utflen = UTF8::Encode(code, utf);
          utf[utflen] = 0;
          AddString(utf);
          ch = ReadChar();
        }
      } else {
        // Add text to heap buffer.
        AddText(ch);

        // Read next char.
        ch = ReadChar();
      }
    } else {
      bool endtag = false;
      bool single = false;

      // HTML markup, flush text before processing markup.
      if (txtptr_) {
        Add(0);
        if (!Text(txtptr_)) return false;
        bufptr_ = txtptr_;
        txtptr_ = nullptr;
      }

      ch = ReadChar();
      if (ch == '!') {
        // Comment, CDATA or DOCTYPE.
        ch = ReadChar();
        const char *comment = "--";
        const char *doctype = "DOCTYPE";
        const char *cdata = "[CDATA[";

        // Parse until end of tag.
        int prefix_size = 2;
        int suffix_size = 0;
        const char *suffix = ">";
        SpecialTagType type = EMPTY;
        AddText('<');
        AddText('!');
        while (true) {
          AddText(ch);

          // Check comment prefix.
          if (comment) {
            if (*comment == ch) {
              comment++;
              if (*comment == 0) {
                comment = nullptr;
                prefix_size = 4;
                suffix_size = 3;
                suffix = "-->";
                type = COMMENT;
              }
            } else {
              comment = nullptr;
            }
          }

          // Check DOCTYPE prefix prefix.
          if (doctype) {
            if (*doctype == ch) {
              doctype++;
              if (*doctype == 0) {
                doctype = nullptr;
                prefix_size = 9;
                suffix_size = 1;
                suffix = ">";
                type = DOCTYPE;
              }
            } else {
              doctype = nullptr;
            }
          }

          // Check CDATA prefix prefix.
          if (cdata) {
            if (*cdata == ch) {
              cdata++;
              if (*cdata == 0) {
                cdata = nullptr;
                prefix_size = 9;
                suffix_size = 3;
                suffix = "]]>";
                type = CDATA;
              }
            } else {
              cdata = nullptr;
            }
          }

          // Output as text if we reach end of file.
          if (ch < 0) {
            AddText(0);
            if (!Text(txtptr_)) return false;
            bufptr_ = txtptr_;
            txtptr_ = nullptr;
            break;
          }

          // Check for end of tag.
          if (ch == '>') {
            int size = bufptr_ - txtptr_;
            if (size >= prefix_size + suffix_size &&
                memcmp(bufptr_ - suffix_size, suffix, suffix_size) == 0) {
              if (suffix_size > 0) {
                bufptr_[-suffix_size] = 0;
              } else {
                AddText(0);
              }
              switch (type) {
                case EMPTY:
                  if (!Text(txtptr_)) return false;
                  break;
                case COMMENT:
                  if (!Comment(txtptr_ + prefix_size)) return false;
                  break;
                case DOCTYPE:
                  if (!DocType(txtptr_ + prefix_size)) return false;
                  break;
                case CDATA:
                  if (!CData(txtptr_ + prefix_size)) return false;
                  break;
              }
              bufptr_ = txtptr_;
              txtptr_ = nullptr;
              ch = ReadChar();
              break;
            }
          }

          ch = ReadChar();
        }
      } else {
        if (ch == '/') {
          // End tag.
          endtag = true;
          ch = ReadChar();
        }

        // Skip whitespace before tag name.
        ch = SkipWhitespace(ch);

        // Read tag name.
        element_.name = bufptr_;
        while (IsNameChar(ch)) {
          Add(ch);
          ch = ReadChar();
        }
        Add(0);
        TagType tagtype = GetTagType(element_.name);

        // Skip whitespace after tag name.
        ch = SkipWhitespace(ch);

        // Read attributes.
        while (IsNameChar(ch)) {
          // Read attribute name.
          int n = element_.attrs.size();
          element_.attrs.emplace_back(bufptr_, nullptr);
          while (IsNameChar(ch)) {
            Add(ch);
            ch = ReadChar();
          }
          Add(0);

          // Skip whitespace after attribute name.
          ch = SkipWhitespace(ch);

          // Parse optional attribute value.
          if (ch == '=') {
            // Skip whitespace after '='.
            ch = ReadChar();
            ch = SkipWhitespace(ch);

            if (ch == '"' || ch == '\'') {
              // Read string attribute value.
              char delim = ch;
              ch = ReadChar();
              element_.attrs[n].value = bufptr_;
              while (ch != delim) {
                if (ch < 0) break;

                if (ch != '&') {
                  // Add character to attribute value.
                  Add(ch);
                  ch = ReadChar();
                } else {
                  // Process entity reference.
                  string &entref = scratch;
                  entref.clear();
                  entref.push_back('&');
                  ch = ReadChar();
                  while (ch >= 0 && ch != ';' && ch != '"' && ch != '\'' &&
                         ch != ' ' && ch != '>') {
                    entref.push_back(ch);
                    ch = ReadChar();
                  }
                  entref.push_back(';');

                  int code = ParseEntityRef(entref);
                  if (code < 0) {
                    AddString(entref.data());
                  } else {
                    char utf[UTF8::MAXLEN + 1];
                    int utflen = UTF8::Encode(code, utf);
                    utf[utflen] = 0;
                    AddString(utf);
                    ch = ReadChar();
                  }
                }
              }

              // Skip delimiter at end of string attribute value.
              ch = ReadChar();
            } else {
              // Parse attribute value that is not delimited.
              while (IsNameChar(ch)) {
                Add(ch);
                ch = ReadChar();
              }
            }
            Add(0);

            // Skip whitespace after attribute value.
            ch = SkipWhitespace(ch);
          }
        }

        // Process end of tag after attributes.
        if (ch == '/') {
          // Single tag, i.e. <tag/>.
          single = true;
          ch = ReadChar();
        }

        // Skip whitespace before '>'.
        ch = SkipWhitespace(ch);

        // Expect tag to end with '>'.
        if (ch == '>') {
          ch = ReadChar();
        } else {
          // Skip to end of line to terminate tag.
          while (ch > 0 && ch != '>' && ch != '<' && ch != '\n') {
            ch = ReadChar();
          }
          if (ch == '>') ch = ReadChar();
        }

        // Process tag.
        if (endtag) {
          // Find matching start tag in stack.
          int match = -1;
          int top = stack_.size() - 1;
          for (int i = top; i >= 0; --i) {
            if (strcasecmp(element_.name, stack_[i]) == 0) {
              match = i;
              break;
            }
          }

          // Unwind tag stack.
          if (match != -1) {
            for (int i = top; i >= match; --i) {
              if (!EndElement(stack_[i])) return false;
            }
            bufptr_ = stack_[top];
            stack_.resize(match);
          }
          element_.Clear();
        } else if (single  || tagtype == SINGLE) {
          // Output start and end element.
          if (!StartElement(element_)) return false;
          if (!EndElement(element_.name)) return false;

          // Clear tag from heap buffer.
          bufptr_ = element_.name;
          element_.Clear();
        } else {
          // Close tag with implicit end tags.
          if (tagtype == IMPLICIT &&
              !stack_.empty() &&
              strcasecmp(stack_.back(), element_.name) == 0) {
            if (!EndElement(stack_.back())) return false;
          } else {
            // Add start tag to stack.
            stack_.push_back(element_.name);
          }

          // Output start element.
          if (!StartElement(element_)) return false;

          // Clear attributes from heap buffer, but keep element name used by
          // stack.
          if (!element_.attrs.empty()) bufptr_ = element_.attrs[0].name;

          // Parse tags like script and style that do not contain HTML markup.
          if (tagtype == UNPARSED) {
            // Scan forward until </tag> is found.
            int state = 1;
            scratch = element_.name;
            const char *tagend = nullptr;
            while (state != 0 && ch != -1) {
              AddText(ch);
              switch (state) {
                case 1:  // scanning
                  if (ch == '<') state = 2;
                  break;

                case 2:  // after <
                  if (ch == '/') {
                    state = 3;
                    tagend = scratch.c_str();
                  } else {
                    state = 1;
                  }
                  break;

                case 3:  // scan endtag
                  if (ch == *tagend) {
                    if (*++tagend == 0) state = 4;
                  } else {
                    state = 1;
                  }
                  break;

                case 4:  // after end tag
                  if (ch == '>') {
                    state = 0;
                  } else {
                    state = 1;
                  }
                  break;
              }
              ch = ReadChar();
            }
            AddText(0);

            // Output unparsed text.
            if (state == 0) {
              bufptr_[-(scratch.size() + 4)] = 0;
            }
            if (!Text(txtptr_)) return false;

            // Unwind tag stack.
            if (!EndElement(element_.name)) return false;
            bufptr_ = element_.name;
            stack_.pop_back();
            txtptr_ = nullptr;
          }
          element_.Clear();
        }
      }
    }
  }

  // Unwind tag stack.
  if (!stack_.empty()) {
    for (int i = stack_.size() - 1; i >= 0; --i) {
      if (!EndElement(stack_[i])) return false;
    }
  }

  // Mark end of document.
  if (!EndDocument()) return false;

  return true;
}

bool HTMLParser::DocType(const char *str) {
  return true;
}

bool HTMLParser::CData(const char *str) {
  return true;
}

}  // namespace sling

