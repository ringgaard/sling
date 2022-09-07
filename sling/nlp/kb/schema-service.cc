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

#include "sling/nlp/kb/schema-service.h"

#include <time.h>

#include "sling/frame/serialization.h"
#include "sling/net/http-utils.h"
#include "third_party/zlib/zlib.h"

namespace sling {

// Compress buffer using gzip.
static void gzip_compress(const string &input, string *output) {
  size_t bufsize = compressBound(input.size());
  output->resize(bufsize);
  const uint8 *src = reinterpret_cast<const uint8 *>(input.data());
  uint8 *dest = reinterpret_cast<uint8 *>(&(*output)[0]);
  compress(dest, &bufsize, src, input.size());
  output->resize(bufsize);
}

SchemaService::SchemaService(Store *kb) {
  // Initialize local store for pre-encoded schema.
  Store store(kb);
  Handle n_role = store.Lookup("role");
  Handle n_inverse_label_item = store.Lookup("P7087");

  // Build set of properties and inverse properties.
  HandleSet propset;
  for (const Slot &s : Frame(kb, kb->Lookup("/w/entity"))) {
    if (s.name != n_role) continue;
    propset.add(s.value);
    Frame property(kb, s.value);
    Handle inverse = property.GetHandle(n_inverse_label_item);
    if (!inverse.IsNil()) propset.add(inverse);
  }

  // Collect properties.
  Handles properties(&store);
  for (Handle prop : propset) properties.push_back(prop);
  Array property_list(&store, properties);

  // Build schema frame.
  Builder schema(&store);
  schema.Add("properties", property_list);
  Frame schemas = schema.Create();

  // Pre-encode schema.
  StringEncoder encoder(&store);
  for (Handle p : properties) {
    encoder.Encode(p);
  }
  encoder.Encode(schemas);
  encoded_schemas_ = encoder.buffer();
  gzip_compress(encoded_schemas_, &compressed_schemas_);
  timestamp_ = time(0);
  VLOG(1) << "Pre-encoded schema size: " << encoded_schemas_.size()
          << ", compressed " << compressed_schemas_.size();
}

void SchemaService::Register(HTTPServer *http) {
  http->Register("/schema", this, &SchemaService::HandleSchema);
}

void SchemaService::HandleSchema(HTTPRequest *request, HTTPResponse *response) {
  // Check if schema has changed.
  const char *cached = request->Get("If-modified-since");
  const char *control = request->Get("Cache-Control");
  bool refresh = control != nullptr && strcmp(control, "maxage=0") == 0;
  if (!refresh && cached) {
    if (ParseRFCTime(cached) == timestamp_) {
      response->set_status(304);
      response->set_content_length(0);
      return;
    }
  }

  // Set HTTP headers.
  char datebuf[RFCTIME_SIZE];
  response->Set("Last-Modified", RFCTime(timestamp_, datebuf));
  response->set_content_type("application/sling");

  // Do not return content if only headers were requested.
  if (strcmp(request->method(), "HEAD") == 0) return;

  // Return schemas.
  const char *accept = request->Get("Accept-Encoding");
  if (accept) {
    response->Set("Transfer-Encoding", "gzip");
    response->Append(compressed_schemas_);
  } else {
    response->Append(encoded_schemas_);
  }
}

}  // namespace sling

