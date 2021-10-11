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

namespace sling {

SchemaService::SchemaService(Store *kb) {
  // Initialize local store for schemas.
  Store store(kb);
  Handle n_role = store.Lookup("role");
  HandleSet property_fields;
  property_fields.insert(store.Lookup("name"));
  property_fields.insert(store.Lookup("alias"));
  property_fields.insert(store.Lookup("description"));
  property_fields.insert(store.Lookup("target"));

  // Collect properties.
  Handles properties(&store);
  for (const Slot &s : Frame(kb, kb->Lookup("/w/entity"))) {
    if (s.name != n_role) continue;
    Frame property(kb, s.value);

    // Build client property frame.
    Builder b(&store);
    b.AddId(property.Id());
    for (const Slot &s : property) {
      if (property_fields.has(s.name)) {
        b.Add(s.name, s.value);
      }
    }
    Frame p = b.Create();
    properties.push_back(p.handle());
  }
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
  timestamp_ = time(0);
  LOG(INFO) << "Pre-encoded schema size: " << encoded_schemas_.size();
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
  response->Set("Access-Control-Allow-Origin", "*"); // TODO: remove
  response->set_content_type("application/sling");

  // Do not return content if only headers were requested.
  if (strcmp(request->method(), "HEAD") == 0) return;

  // Return schemas.
  response->Append(encoded_schemas_);
}

}  // namespace sling

