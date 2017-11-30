#ifndef SLING_WEB_URL_H_
#define SLING_WEB_URL_H_

#include <string>

#include "sling/base/types.h"

namespace sling {

// Uniform Resource Locator (URL) parser. A URL can have the following
// components:
//   scheme:[//[user:password@]host[:port]][/]path[?query][#fragment]
class URL {
 public:
  URL() {}
  URL(const string &url);

  // Parse URL.
  void Parse(const string &url);

  // Clear URL and all components.
  void Clear();

  // The whole URL.
  const string &url() const { return url_; };

  // URL components.
  const string &scheme() const { return scheme_; }
  const string &user() const { return user_; }
  const string &password() const { return password_; }
  const string &host() const { return host_; }
  const string &port() const { return port_; }
  const string &path() const { return path_; }
  const string &query() const { return query_; }
  const string &fragment() const { return fragment_; }

 private:
  // Split URL into components.
  void Split();

  // URL.
  string url_;

  // The URL components can be in either escaped or unescaped form.
  bool escaped_ = false;

  // URL components.
  string scheme_;
  string user_;
  string password_;
  string host_;
  string port_;
  string path_;
  string query_;
  string fragment_;
};

}  // namespace sling

#endif  // SLING_WEB_URL_H_
