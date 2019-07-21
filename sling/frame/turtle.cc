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

#include "sling/frame/turtle.h"

#include <string>

#include "sling/string/ctype.h"

namespace sling {

TurtleTokenizer::TurtleTokenizer(Input *input) : Scanner(input) {
  NextToken();
}

int TurtleTokenizer::NextToken() {
  // Clear token text buffer.
  token_text_.clear();

  // Keep reading until we either read a token or reach the end of the input.
  for (;;) {
    // Skip whitespace.
    while (current_ != -1 && ascii_isspace(current_)) NextChar();

    // Parse next token (or comment).
    switch (current_) {
      case -1:
        return Token(END);

      case '"': case '\'':
        return ParseString();

      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
      case '+': case '-':
        return ParseNumber();

      case '#':
        // Parse comment.
        NextChar();
        while (current_ != -1 && current_ != '\n') NextChar();
        continue;

      case '<':
        return ParseURI();

      case '=':
        return Select('>', IMPLIES_TOKEN, '=');

      case '^':
        return Select('^', TYPE_TOKEN, '^');

      case '.':
        return Select(current_);

      default:
        for (;;) {
          if (current_ == -1) {
            break;
          } else if (current_ == ':') {
            // First colon ends the name prefix.
            if (colon_ != -1) colon_ = token_text_.size();
            Append(':');
            NextChar();
          } else if (current_ == '\\') {
            // Parse character escape (\c).
            NextChar();
            if (current_ == -1) return Error("invalid escape sequence in name");
            Append(current_);
            NextChar();
          } else if (current_ == '%') {
            // Parse hex escape (%00).
            NextChar();
            int ch = HexToDigit(current_);
            NextChar();
            ch = (ch << 4) + HexToDigit(current_);
            NextChar();
            if (ch < 0) return Error("invalid hex escape in name");
            Append(ch);
          } else if (current_ >= 128 || ascii_isalnum(current_) ||
                    current_ == '_' || current_ == '.' || current_ == '-') {
            Append(current_);
            NextChar();
          } else {
            break;
          }
        }

        if (token_text_.size() == 0) {
          // Single-character token.
          return Select(current_);
        } else if (colon_ != -1) {
          // Prefixed name.
          return NAME_TOKEN;
        } else {
          // Name or reserved word.
          return Token(LookupKeyword());
        }
    }
  }
}

int TurtleTokenizer::ParseURI() {
  // Skip start delimiter.
  NextChar();

  // Parse URI.
  while (current_ != '>') {
    if (current_ <= ' ') {
      return Error("Unterminated URI");
    } else if (current_ == '\\') {
      NextChar();
      if (current_ == 'u') {
        // Parse unicode hex escape (\u0000).
        NextChar();
        if (!ParseUnicode(4)) {
          return Error("Invalid Unicode escape in URI");
        }
      } else if (current_ == 'U') {
        // Parse unicode hex escape (\U00000000).
        NextChar();
        if (!ParseUnicode(8)) {
          return Error("Invalid Unicode escape in URI");
        }
      } else {
        return Error("Invalid URI");
      }
    } else {
      // Add character to URI.
      Append(current_);
      NextChar();
    }
  }

  NextChar();
  return Token(URI_TOKEN);
}

int TurtleTokenizer::ParseString() {
  // Skip start delimiter(s).
  int delimiter = current_;
  int delimiters = 0;
  while (delimiters < 3 && current_ == delimiter) {
    NextChar();
    delimiters++;
  }
  bool multi_line = false;
  if (delimiters == 3) {
    // Multi-line string.
    multi_line = true;
  } else if (delimiters == 2) {
    // Empty string.
    return Token(STRING_TOKEN);
  }

  // Read rest of string.
  bool done = false;
  delimiters = 0;
  while (!done) {
    // Check for unterminated string.
    if (current_ == -1 || (!multi_line && current_ == '\n')) {
      return Error("Unterminated string");
    }

    // Check for delimiters.
    if (current_ == delimiter) {
      if (multi_line) {
        if (++delimiters == 3) {
          // End of multi-line string. Remove two previous delimiters.
          token_text_.resize(token_text_.size() - 2);
          NextChar();
          done = true;
        } else {
          Append(current_);
          NextChar();
        }
      } else {
        // End of string.
        NextChar();
        done = true;
      }
    } else {
      delimiters = 0;
      if (current_ == '\\') {
        // Handle escape characters.
        NextChar();
        switch (current_) {
          case 'b': Append('\b'); NextChar(); break;
          case 'f': Append('\f'); NextChar(); break;
          case 'n': Append('\n'); NextChar(); break;
          case 'r': Append('\r'); NextChar(); break;
          case 't': Append('\t'); NextChar(); break;
          case 'u':
            // Parse unicode hex escape (\u0000).
            NextChar();
            if (!ParseUnicode(4)) {
              return Error("Invalid Unicode escape in string");
            }
            break;
          case 'U':
            // Parse unicode hex escape (\U00000000).
            NextChar();
            if (!ParseUnicode(8)) {
              return Error("Invalid Unicode escape in string");
            }
            break;
          default:
            // Just escape the next character.
            Append(current_);
            NextChar();
        }
      } else {
        // Add character to string.
        Append(current_);
        NextChar();
      }
    }
  }

  return Token(STRING_TOKEN);
}

int TurtleTokenizer::ParseNumber() {
  // Parse sign.
  int sign = 0;
  if (current_ == '+' || current_ == '-') {
    sign = current_;
    Append(current_);
    NextChar();
    if (!ascii_isdigit(current_) &&
        current_ != '.' &&
        current_ != 'e' &&
        current_ != 'E') {
      return Token(sign);
    }
  }

  // Parse integral part.
  int integral_digits = ParseDigits();

  // Parse decimal part.
  int decimal_digits = 0;
  if (current_ == '.') {
    Append('.');
    NextChar();
    decimal_digits = ParseDigits();
    if (!sign && integral_digits == 0 && decimal_digits == 0) {
      return Token('.');
    }
  }

  // Parse exponent.
  int exponent_digits = 0;
  if (current_ == 'e' || current_ == 'E') {
    Append('e');
    NextChar();
    if (current_ == '-' || current_ == '+') {
      Append(current_);
      NextChar();
    }
    exponent_digits = ParseDigits();
    if (exponent_digits == 0) {
      return Error("Missing exponent in number");
    }
  }

  // Determine number type.
  if (exponent_digits != 0) {
    return Token(FLOAT_TOKEN);
  } else if (decimal_digits != 0) {
    return Token(DECIMAL_TOKEN);
  } else if (integral_digits != 0) {
    return Token(INTEGER_TOKEN);
  } else {
    return Error("Invalid number");
  }
}

int TurtleTokenizer::LookupKeyword() {
  const char *name = token_text_.data();
  int first = *name;
  switch (token_text_.size()) {
    case 1:
      if (first == 'a') return A_TOKEN;
      break;

    case 4:
      if (first == 't' && memcmp(name, "true", 4) == 0) return TRUE_TOKEN;
      if (first == 'b' && memcmp(name, "base", 4) == 0) return BASE_TOKEN;
      if (first == 'B' && memcmp(name, "BASE", 4) == 0) return BASE_TOKEN;
      break;

    case 5:
      if (first == 'f' && memcmp(name, "false", 5) == 0) return FALSE_TOKEN;
      break;

    case 6:
      if (first == 'p' && memcmp(name, "prefix", 6) == 0) return PREFIX_TOKEN;
      if (first == 'P' && memcmp(name, "PREFIX", 6) == 0) return PREFIX_TOKEN;
      break;
  }

  return NAME_TOKEN;
}

Object TurtleParser::Read() {
  return Frame::nil();
}

}  // namespace sling
