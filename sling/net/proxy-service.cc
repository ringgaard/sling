// Copyright 2020 Ringgaard Research ApS
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

#include "sling/net/proxy-service.h"

#include <curl/curl.h>

#include "sling/base/flags.h"

DEFINE_string(proxy_dns, "8.8.8.8", "DNS servers for proxy");

namespace sling {

std::unordered_set<string> ProxyService::blocked_headers = {
  "Date",
  "Server",
  "Content-Length",
  "Transfer-Encoding",
  "Connection",
  "Keep-Alive",
};

ProxyService::ProxyService() {
  // Initialize curl library.
  curl_global_init(CURL_GLOBAL_ALL);
}

ProxyService::~ProxyService() {
  // Clean up curl library resources.
  curl_global_cleanup();
}

void ProxyService::Register(HTTPServer *http) {
  http->Register("/proxy", this, &ProxyService::Handle);
}

void ProxyService::Handle(HTTPRequest *request, HTTPResponse *response) {
  // Get parameters.
  const char *location = request->Get("Location");
  const char *user_agent = request->Get("User-Agent");
  if (location == nullptr) {
    response->SendError(400, "Bad Request", "Location missing");
    return;
  }
  VLOG(1) << "Proxy request: " << location;

  // Fetch page using curl. The callbacks populate the response object.
  CURL *curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, location);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &ProxyService::Data);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &ProxyService::Header);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, response);
  if (user_agent != nullptr) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
  }
  if (!FLAGS_proxy_dns.empty()) {
    curl_easy_setopt(curl, CURLOPT_DNS_SERVERS, FLAGS_proxy_dns.c_str());
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    LOG(ERROR) << "CURL error: " << curl_easy_strerror(res);
    response->SendError(503, "Service Not Available", curl_easy_strerror(res));
  } else {
    // Prevent proxy from accesing local network (10.x.x.x and 192.168.x.x)
    char *ipaddr = nullptr;
    curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &ipaddr);
    LOG(INFO) << "Proxy retrieved from IP " << ipaddr;
    Text ip(ipaddr);
    if (ip.empty() ||
        ip.starts_with("10.") ||
        ip.starts_with("192.168.") ||
        ip.starts_with("127.")) {
      response->SendError(403, "Forbidden", "Blocked address");
    } else {
      // Return HTTP status code.
      int status = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
      if (status != 0) response->set_status(status);
    }
  }
  curl_easy_cleanup(curl);
}

size_t ProxyService::Header(char *buffer, size_t size, size_t n,
                            void *userdata) {
  HTTPResponse *response = reinterpret_cast<HTTPResponse *>(userdata);
  size_t bytes = size * n;
  const char *data = static_cast<const char *>(buffer);
  const char *end = data + bytes;

  // Parse header field.
  const char *p = data;
  while (p < end && *p != ':') p++;
  if (p != end) {
    string name(data, p - data);
    if (blocked_headers.count(name) == 0) {
      p++;
      while (p < end && *p == ' ') p++;
      const char *value = p;
      while (p < end && *p != '\n') p++;
      size_t vlen = p - value;

      // Add header to response.
      response->Add(name.c_str(), value, vlen);
      VLOG(2) << "  Header: " << name << ": " << string(value, vlen);
    }
  }

  return bytes;
}

size_t ProxyService::Data(void *buffer, size_t size, size_t n, void *userdata) {
  // Get chunk.
  HTTPResponse *response = reinterpret_cast<HTTPResponse *>(userdata);
  const char *data = static_cast<const char *>(buffer);
  size_t bytes = size * n;

  // Write chunk to response.
  response->Append(data, bytes);

  return bytes;
}

}  // namespace sling

