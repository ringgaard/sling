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

