// Copyright 2021 Ringgaard Research ApS
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

#include "sling/util/json.h"

namespace sling {

string JSON::EMPTY_STRING;
JSON JSON::ERROR_VALUE;

static bool is_space(int c) {
  return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f';
}

static bool is_digit(int c) {
  return c >= '0' && c <= '9';
}

static int hex_to_digit(int ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}

static int OutputUTF8(IOBuffer *output,
                       const unsigned char *s,
                       const unsigned char *end) {
  // Get UTF8 length.
  if (s == end) return -1;
  unsigned char c = *s;
  int n;
  if ((c & 0x80) == 0x00) {
    n = 1;
  } else if ((c & 0xe0) == 0xc0) {
    n = 2;
  } else if ((c & 0xf0) == 0xe0) {
    if (c == 0xed) return -1;
    n = 3;
  } else if ((c & 0xf8) == 0xf0) {
    if (c == 0xf4) return -1;
    n = 4;
  } else {
    return -1;
  }

  // Check boundaries.
  if (s + n > end) return -1;

  // Check that UTF-8 code point is valid.
  for (int i = 1; i < n; ++i) {
    if ((s[i] & 0xc0) != 0x80) return -1;
  }

  // Output UTF8-encoded code point.
  output->Write(s, n);
  return n;
}

static void OutputString(IOBuffer *output, const std::string &str) {
  // Hexadecimal digits.
  static char hexdigit[] = "0123456789ABCDEF";

  output->Write('"');
  const unsigned char *s = reinterpret_cast<const unsigned char *>(str.data());
  const unsigned char *end = s + str.size();
  while (s < end) {
    unsigned char c = *s;
    if (c < ' ') {
      // Control character.
      switch (c) {
        case '\n': output->Write("\\n", 2); break;
        case '\t': output->Write("\\t", 2); break;
        case '\b': output->Write("\\b", 2); break;
        case '\f': output->Write("\\f", 2); break;
        case '\r': output->Write("\\r", 2); break;
        default:
          output->Write("\\u00");
          output->Write(hexdigit[c >> 4]);
          output->Write(hexdigit[c & 0x0f]);
      }
      s++;
    } else if (*s == '\\') {
      output->Write("\\\\", 2);
      s++;
    } else if (*s == '"') {
      output->Write("\\\"", 2);
      s++;
    } else if (*s < 0x80) {
      // ASCII character.
      output->Write(c);
      s++;
    } else {
      // UTF-8 Unicode character.
      int len = OutputUTF8(output, s, end);
      if (len < 0) break;
      s += len;
    }
  }
  output->Write('"');
}

JSON::~JSON() {
  switch (type_) {
    case STRING: delete s_; break;
    case OBJECT: delete o_; break;
    case ARRAY: delete a_; break;
    default: break;
  }
}

void JSON::Write(IOBuffer *output) const {
  switch (type_) {
    case NIL:
      output->Write("null");
      break;
    case INT:
      output->Write(std::to_string(i_));
      break;
    case FLOAT:
      output->Write(std::to_string(f_));
      break;
    case BOOL:
      output->Write(i_ ? "true" : "false");
      break;
    case STRING:
      OutputString(output, *s_);
      break;
    case OBJECT:
      o_->Write(output);
      break;
    case ARRAY:
      a_->Write(output);
      break;
    case ERROR:
      output->Write("<<ERROR>>");
      break;
  }
}

JSON::Object *JSON::Object::AddObject(const string &key) {
  JSON::Object *o = new JSON::Object();
  items_.emplace_back(key, JSON(o));
  return o;
}

JSON::Array *JSON::Object::AddArray(const string &key) {
  JSON::Array *a = new JSON::Array();
  items_.emplace_back(key, JSON(a));
  return a;
}

void JSON::Object::Write(IOBuffer *output) const {
  output->Write('{');
  for (int i = 0; i < items_.size(); ++i) {
    if (i > 0) output->Write(",");
    const auto &e = items_[i];
    OutputString(output, e.first);
    output->Write(':');
    e.second.Write(output);
  }
  output->Write('}');
}

std::string JSON::Object::AsString() const {
  IOBuffer buffer;
  Write(&buffer);
  return std::string(buffer.begin(), buffer.end());
}

const JSON &JSON::Object::operator [](const string &key) const {
  for (int i = 0; i < items_.size(); ++i) {
    if (items_[i].first == key) return items_[i].second;
  }
  return JSON::ERROR_VALUE;
}

const JSON &JSON::Object::operator [](const char *key) const {
  for (int i = 0; i < items_.size(); ++i) {
    if (items_[i].first == key) return items_[i].second;
  }
  return JSON::ERROR_VALUE;
}

JSON::Object *JSON::Array::AddObject() {
  JSON::Object *o = new JSON::Object();
  elements_.emplace_back(JSON(o));
  return o;
}

JSON::Array *JSON::Array::AddArray() {
  JSON::Array *a = new JSON::Array();
  elements_.emplace_back(JSON(a));
  return a;
}

void JSON::Array::Write(IOBuffer *output) const {
  output->Write('[');
  for (int i = 0; i < elements_.size(); ++i) {
    if (i > 0) output->Write(",");
    elements_[i].Write(output);
  }
  output->Write(']');
}

JSON JSON::Parser::Parse() {
  SkipWhitespace();
  if (current_ == -1) {
    return JSON(ERROR);
  } else if (current_ == '{') {
    return ParseObject();
  } else if (current_ == '[') {
    return ParseArray();
  } else if (current_ == '"') {
    if (ParseString()) {
      return JSON(token_);
    } else {
      return JSON(ERROR);
    }
  } if (is_digit(current_) || current_ == '-') {
    return ParseNumber();
  }

  token_.clear();
  while (current_ >= 'a' && current_ <= 'z') {
    token_.push_back(current_);
    next();
  }
  if (token_ == "true") return JSON(true);
  if (token_ == "false") return JSON(false);
  if (token_ == "null") return JSON(NIL);
  return JSON(ERROR);
}

void JSON::Parser::SkipWhitespace() {
  while (current_ != -1 && is_space(current_)) next();
}

JSON JSON::Parser::ParseObject() {
  // Skip start bracket.
  next();

  // Read items.
  Object *obj = new Object();
  string key;
  for (;;) {
    // Check for end of object.
    SkipWhitespace();
    if (current_ == -1) {
      delete obj;
      return JSON(ERROR);
    }
    if (current_ == '}') {
      next();
      break;
    }

    // Read key.
    if (!ParseString()) {
      delete obj;
      return JSON(ERROR);
    }
    key = token_;

    // Expect colon.
    SkipWhitespace();
    if (current_ != ':') {
      delete obj;
      return JSON(ERROR);
    }
    next();

    // Read value.
    JSON value = Parse();
    if (!value.valid()) {
      delete obj;
      return JSON(ERROR);
    }

    // Add item to object.
    obj->Add(key, value);

    // Skip comma.
    SkipWhitespace();
    if (current_ == ',') next();
  }

  return JSON(obj);
}

JSON JSON::Parser::ParseArray() {
  // Skip start bracket.
  next();

  // Read elements.
  Array *array = new Array();
  for (;;) {
    // Check for end of object.
    SkipWhitespace();
    if (current_ == -1) {
      delete array;
      return JSON(ERROR);
    }
    if (current_ == ']') {
      next();
      break;
    }

    // Read value.
    JSON value = Parse();
    if (!value.valid()) {
      delete array;
      return JSON(ERROR);
    }

    // Add element to array.
    array->Add(value);

    // Skip comma.
    SkipWhitespace();
    if (current_ == ',') next();
  }

  return JSON(array);
}

bool JSON::Parser::ParseString() {
  // Skip start quotes.
  next();

  // Read until end quote.
  token_.clear();
  while (current_ != '"') {
    if (current_ == -1) return false;
    if (current_ == '\\') {
      next();
      switch (current_) {
        case '"': token_.push_back('"'); next(); break;
        case '\\': token_.push_back('\\'); next(); break;
        case '/': token_.push_back('/'); next(); break;
        case 'b': token_.push_back('\b'); next(); break;
        case 'f': token_.push_back('\f'); next(); break;
        case 'n': token_.push_back('\n'); next(); break;
        case 'r': token_.push_back('\r'); next(); break;
        case 't': token_.push_back('\t'); next(); break;
        case 'u': {
          // Parse Unicode escape.
          next();
          int code = 0;
          for (int i = 0; i < 4; ++i) {
            char digit = hex_to_digit(current_);
            if (digit < 0) return false;
            code = (code << 4) + digit;
            if (code > 0x10ffff) return false;
            next();
          }

          // Convert code point to UTF-8.
          if (code <= 0x7f) {
            // One character sequence.
            token_.push_back(code);
          } else if (code <= 0x7ff) {
            // Two character sequence.
            token_.push_back(0xc0 | (code >> 6));
            token_.push_back(0x80 | (code & 0x3f));
          } else if (code <= 0xffff) {
            // Three character sequence.
            token_.push_back(0xe0 | (code >> 12));
            token_.push_back(0x80 | ((code >> 6) & 0x3f));
            token_.push_back(0x80 | (code & 0x3f));
          } else {
            // Four character sequence.
            token_.push_back(0xf0 | (code >> 18));
            token_.push_back(0x80 | ((code >> 12) & 0x3f));
            token_.push_back(0x80 | ((code >> 6) & 0x3f));
            token_.push_back(0x80 | (code & 0x3f));
          }
          break;
        }
        default: return false;
      }
    } else {
      token_.push_back(current_);
      next();
    }
  }
  next();
  return true;
}

JSON JSON::Parser::ParseNumber() {
  token_.clear();

  // Parse sign.
  if (current_ == '-') {
    token_.push_back('-');
    next();
  }

  // Parse integer part.
  while (is_digit(current_)) {
    token_.push_back(current_);
    next();
  }

  // Parse decimal part.
  bool integer = true;
  if (current_ == '.') {
    integer = false;
    token_.push_back('.');
    next();
    while (is_digit(current_)) {
    token_.push_back(current_);
      next();
    }
  }

  // Parse exponent.
  if (current_ == 'e' || current_ == 'E') {
    token_.push_back('e');
    next();
    integer = false;
    if (current_ == '-' || current_ == '+') {
      token_.push_back(current_);
      next();
    }
    while (is_digit(current_)) {
      token_.push_back(current_);
      next();
    }
  }

  // Convert to number.
  if (integer) {
    int64 num = std::stoll(token_);
    return JSON(num);
  } else {
    double num = std::stod(token_);
    return JSON(num);
  }
}

string JSON::AsString() const {
  IOBuffer buffer;
  Write(&buffer);
  return string(buffer.begin(), buffer.end());
}

JSON JSON::Read(IOBuffer *input) {
  JSON::Parser parser(input);
  return parser.Parse();
}

JSON JSON::Read(const string &json) {
  IOBuffer input;
  input.Write(json);
  return Read(&input);
}

}  // namespace sling
