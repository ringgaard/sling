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

#include "sling/net/http-utils.h"

#include <time.h>

#include "sling/base/logging.h"
#include "sling/string/ctype.h"
#include "sling/string/numbers.h"
#include "sling/string/text.h"

namespace sling {

// File extension to MIME type mapping.
struct MIMEMapping {
  const char *ext;
  const char *mime;
};

static const MIMEMapping mimetypes[] = {
  {"html", "text/html; charset=utf-8"},
  {"htm", "text/html; charset=utf-8"},
  {"xml", "text/xml; charset=utf-8"},
  {"jpeg", "image/jpeg"},
  {"jpg", "image/jpeg"},
  {"gif", "image/gif"},
  {"png", "image/png"},
  {"ico", "image/x-icon"},
  {"mp4", "video/mp4"},
  {"webm", "video/webm"},
  {"ttf", "font/ttf"},
  {"css", "text/css; charset=utf-8"},
  {"svg", "image/svg+xml; charset=utf-8"},
  {"js", "text/javascript; charset=utf-8"},
  {"zip", "application/zip"},
  {nullptr, nullptr},
};

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
  if (strptime(timestr, "%a, %d %b %Y %H:%M:%S %Z", &tm) != nullptr) {
    return timegm(&tm);
  } else {
    return -1;
  }
}

// Find MIME type from extension.
const char *GetMimeType(const char *ext) {
  if (ext == nullptr) return nullptr;
  for (const MIMEMapping *m = mimetypes; m->ext; ++m) {
    if (strcasecmp(ext, m->ext) == 0) return m->mime;
  }
  return nullptr;
}

// Get extension for file name.
const char *GetExtension(const char *filename) {
  const char *ext = nullptr;
  for (const char *p = filename; *p; ++p) {
    if (*p == '/') {
      ext = nullptr;
    } else if (*p == '.') {
      ext = p + 1;
    }
  }
  return ext;
}

}  // namespace sling

