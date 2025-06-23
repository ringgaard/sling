// Copyright 2025 Ringgaard Research ApS
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

#include "sling/nlp/search/search-client.h"

#include "sling/frame/serialization.h"
#include "sling/stream/memory.h"

namespace sling {

Status SearchClient::Connect(const string &server, const string &agent) {
  // Split hostname and port.
  string hostname = server;
  string portname = "7575";
  int colon = hostname.find(':');
  if (colon != -1) {
    portname = hostname.substr(colon + 1);
    hostname.resize(colon);
  }

  // Connect to database server.
  Status st = Client::Connect(hostname, portname, "search", agent);
  if (!st.ok()) return st;

  return Status::OK;
}

Status SearchClient::Search(const JSON::Object &query, JSON *result) const {
  IOBuffer request;
  query.Write(&request);

  MutexLock lock(&mu_);
  IOBuffer response;
  uint32 reply;
  Status st = Perform(SPSEARCH, &request, &reply, &response);
  if (!st.ok()) return st;
  if (reply == SPERROR) return Status(EIO, response.data());
  JSON json = JSON::Read(&response);
  if (!json.valid()) return Status(EINVAL, "Invalid JSON response");
  result->MoveFrom(json);
  return Status::OK;
}

Status SearchClient::Fetch(std::vector<Text> &ids,
                           Store *store, Handles *items) const {
  MutexLock lock(&mu_);
  IOBuffer request;
  IOBuffer response;
  for (auto &id : ids) {
    if (id.size() > 255) return Status(EINVAL, "id too long");
    uint8 len = id.size();
    request.Write(&len, 1);
    request.Write(id.data(), len);
  }
  uint32 reply;
  Status st = Perform(SPFETCH, &request, &reply, &response);
  if (!st.ok()) return st;
  if (reply == SPERROR) {
    return Status(EIO, "Error fetching items");
  }

  while (response.available() > 0) {
    uint32 size;
    if (!response.Read(&size, 4) || response.available() < size) {
      return Status(EIO, "invalid search reply");
    }
    char *data = response.Consume(size);

    ArrayInputStream stream(data, size);
    InputParser parser(store, &stream);
    Object item = parser.Read();
    if (item.IsError()) return Status(EIO, "invalid reply format");
    items->push_back(item.handle());
  }

  return Status::OK;
}

}  // namespace sling
