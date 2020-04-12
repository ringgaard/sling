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

#include "sling/http/http-utils.h"

#include <time.h>

#include "sling/base/logging.h"
#include "sling/string/ctype.h"
#include "sling/string/numbers.h"
#include "sling/string/text.h"

namespace sling {

// Returns value for ASCII hex digit.
static int HexDigit(int c) {
  return (c <= '9') ? c - '0' : (c & 7) + 9;
}

HTTPMethod GetHTTPMethod(const char *name) {
  if (name == nullptr) return HTTP_INVALID;
  switch (*name) {
    case 'G':
      if (strcmp(name, "GET") == 0) return HTTP_GET;
      return HTTP_INVALID;
    case 'P':
      if (strcmp(name, "PUT") == 0) return HTTP_PUT;
      if (strcmp(name, "POST") == 0) return HTTP_POST;
      if (strcmp(name, "PATCH") == 0) return HTTP_PATCH;
      return HTTP_INVALID;
    case 'H':
      if (strcmp(name, "HEAD") == 0) return HTTP_HEAD;
      return HTTP_INVALID;
    case 'D':
      if (strcmp(name, "DELETE") == 0) return HTTP_DELETE;
      return HTTP_INVALID;
    case 'C':
      if (strcmp(name, "CONNECT") == 0) return HTTP_CONNECT;
      return HTTP_INVALID;
    case 'O':
      if (strcmp(name, "OPTIONS") == 0) return HTTP_OPTIONS;
      return HTTP_INVALID;
    case 'T':
      if (strcmp(name, "TRACE") == 0) return HTTP_TRACE;
      return HTTP_INVALID;
    default:
      return HTTP_INVALID;
  }
}

bool DecodeURLComponent(const char *url, int length, string *output) {
  const char *end = url + length;
  while (url < end) {
    char c = *url++;
    if (c == '%') {
      if (url + 2 > end) return false;
      char x1 = *url++;
      if (!ascii_isxdigit(x1)) return false;
      char x2 = *url++;
      if (!ascii_isxdigit(x2)) return false;
      output->push_back((HexDigit(x1) << 4) + HexDigit(x2));
    } else if (c == '+') {
      output->push_back(' ');
    } else {
      output->push_back(c);
    }
  }

  return true;
}

bool DecodeURLComponent(const char *url, string *output) {
  if (url == nullptr) return true;
  return DecodeURLComponent(url, strlen(url), output);
}

string HTMLEscape(const char *text, int size) {
  string escaped;
  const char *p = text;
  const char *end = text + size;
  while (p < end) {
    char ch = *p++;
    switch (ch) {
      case '&':  escaped.append("&amp;"); break;
      case '<':  escaped.append("&lt;"); break;
      case '>':  escaped.append("&gt;"); break;
      case '"':  escaped.append("&quot;"); break;
      case '\'': escaped.append("&#39;");  break;
      default: escaped.push_back(ch);
    }
  }
  return escaped;
}

void HTTPBuffer::reset(int size) {
  if (size != capacity()) {
    if (size == 0) {
      free(floor);
      floor = ceil = start = end = nullptr;
    } else {
      floor = static_cast<char *>(realloc(floor, size));
      CHECK(floor != nullptr) << "Out of memory, " << size << " bytes";
      ceil = floor + size;
    }
  }
  start = end = floor;
}

void HTTPBuffer::flush() {
  if (start > floor) {
    int size = end - start;
    memcpy(floor, start, size);
    start = floor;
    end = start + size;
  }
}

void HTTPBuffer::ensure(int minfree) {
  // Check if there is enough free space in buffer.
  if (ceil - end >= minfree) return;

  // Compute new size of buffer.
  int size = ceil - floor;
  int minsize = end + minfree - floor;
  while (size < minsize) {
    if (size == 0) {
      size = 1024;
    } else {
      size *= 2;
    }
  }

  // Expand buffer.
  char *p = static_cast<char *>(realloc(floor, size));
  CHECK(p != nullptr) << "Out of memory, " << size << " bytes";

  // Adjust pointers.
  start += p - floor;
  end += p - floor;
  floor = p;
  ceil = p + size;
}

void HTTPBuffer::clear() {
  free(floor);
  floor = ceil = start = end = nullptr;
}

char *HTTPBuffer::gets() {
  char *line = start;
  char *s = line;
  while (s < end) {
    switch (*s) {
      case '\n':
        if (s + 1 < end && (s[1] == ' ' || s[1] == '\t')) {
          // Replace HTTP header continuation with space.
          *s++ = ' ';
        } else {
          //  End of line found. Strip trailing whitespace.
          *s = 0;
          start = s + 1;
          while (s > line) {
            s--;
            if (*s != ' ' && *s != '\t') break;
            *s = 0;
          }
          return line;
        }
        break;

      case '\r':
      case '\t':
        // Replace whitespace with space.
        *s++ = ' ';
        break;

      default:
        s++;
    }
  }

  return nullptr;
}

void HTTPBuffer::append(const char *data, int size) {
  ensure(size);
  memcpy(end, data, size);
  end += size;
}

URLQuery::URLQuery(const char *query) {
  if (query == nullptr) return;
  const char *q = query;

  // Split query string into ampersand-separated parts.
  std::vector<Text> parts;
  const char *p = q;
  while (*q) {
    if (*q == '&') {
      parts.emplace_back(p, q - p);
      q++;
      p = q;
    } else {
      q++;
    }
  }
  parts.emplace_back(p, q - p);

  // Each part is a parameter with a name and a value.
  string name;
  string value;
  for (const Text &part : parts) {
    name.clear();
    value.clear();
    int eq = part.find('=');
    if (eq != -1) {
      DecodeURLComponent(part.data(), eq, &name);
      DecodeURLComponent(part.data() + eq + 1, part.size() - eq - 1, &value);
    } else {
      DecodeURLComponent(part.data(), part.size(), &name);
    }
    parameters_.emplace_back(name, value);
  }
}

Text URLQuery::Get(Text name) const {
  for (auto &p : parameters_) {
    if (p.name == name) return p.value;
  }
  return Text();
}

int URLQuery::Get(Text name, int defval) const {
  Text value = Get(name);
  if (value.empty()) return defval;
  int number;
  if (!safe_strto32(value.data(), value.size(), &number)) return defval;
  return number;
}

bool URLQuery::Get(Text name, bool defval) const {
  for (auto &p : parameters_) {
    if (p.name == name) {
      if (p.value.empty()) return true;
      if (p.value == "0") return false;
      if (p.value == "1") return true;
      if (p.value == "false") return false;
      if (p.value == "true") return true;
      if (p.value == "no") return false;
      if (p.value == "yes") return true;
      return defval;
    }
  }
  return defval;
}

char *RFCTime(time_t t, char *buf) {
  struct tm tm;
  gmtime_r(&t, &tm);
  strftime(buf, RFCTIME_SIZE - 1, "%a, %d %b %Y %H:%M:%S GMT", &tm);
  return buf;
}

time_t ParseRFCTime(const char *timestr) {
  struct tm tm;
  if (strptime(timestr, "%a, %d %b %Y %H:%M:%S GMT", &tm) != nullptr) {
    return timegm(&tm);
  } else {
    return -1;
  }
}

}  // namespace sling

