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

#include "sling/web/url.h"

#include <string>

namespace sling {

URL::URL(const string &url) : url_(url) {
  Split();
}

void URL::Parse(const string &url) {
  Clear();
  url_ = url;
  Split();
}

void URL::Clear() {
  url_.clear();
  scheme_.clear();
  user_.clear();
  password_.clear();
  host_.clear();
  port_.clear();
  path_.clear();
  query_.clear();
  fragment_.clear();
}

void URL::Split() {
  // Parse URL into components.
  escaped_ = false;
  const char *p = url_.data();
  const char *end = p + url_.size();

  // Parse scheme.
  const char *s = p;
  while (p < end && *p != ':') p++;
  scheme_.assign(s, p - s);
  if (p < end) p++;

  // Parse authority part (user name, password, host, and port).
  if (end - p >= 2 && p[0] == '/' && p[1] == '/') {
    // Parse host (or user).
    p += 2;
    s = p;
    while (p < end &&
           *p != ':' &&
           *p != '@' &&
           *p != '/' &&
           *p != '?' &&
           *p != '#') p++;
    host_.assign(s, p - s);

    // Parse port (or password).
    if (p < end && *p == ':') {
      p++;
      s = p;
      while (p < end && *p != '@' && *p != '/' && *p != '?' && *p != '#') p++;
      port_.assign(s, p - s);
    }

    // If URL contains a @, parse host and port.
    if (p < end && *p == '@') {
      user_.swap(host_);
      password_.swap(port_);

      // Parse host.
      p++;
      s = p;
      while (p < end && *p != ':' && *p != '/' && *p != '?' && *p != '#') p++;
      host_.assign(s, p - s);

      // Parse port.
      if (p < end && *p == ':') {
        p++;
        s = p;
        while (p < end && *p != '/' && *p != '?' && *p != '#') p++;
        port_.assign(s, p - s);
      }
    }
  }

  // Parse path.
  if (p < end && *p == '/') p++;
  s = p;
  while (p < end && *p != '?' && *p != '#') p++;
  path_.assign(s, p - s);

  // Parse query.
  if (p < end && *p == '?') {
    p++;
    s = p;
    while (p < end && *p != '#') p++;
    query_.assign(s, p - s);
  }

  // Parse fragment.
  if (p < end && *p == '#') {
    p++;
    fragment_.assign(p, end - p);
  }
}

}  // namespace sling

