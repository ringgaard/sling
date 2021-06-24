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

static void OutputString(IOBuffer *output, const string &s) {
  // TODO: escape strings.
  output->Write('"');
  output->Write(s.data(), s.size());
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
      output->Write("nil");
      break;
    case INT:
      output->Write(std::to_string(i_));
      break;
    case FLOAT:
      output->Write(std::to_string(f_));
      break;
    case BOOL:
      output->Write(b_ ? "true" : "false");
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
  }
}

void JSON::Object::Add(const string &key, int64 value) {
  items_.emplace_back(key, JSON(value));
}

void JSON::Object::Add(const string &key, double value) {
  items_.emplace_back(key, JSON(value));
}

void JSON::Object::Add(const string &key, bool value) {
  items_.emplace_back(key, JSON(value));
}

void JSON::Object::Add(const string &key, const string &value) {
  items_.emplace_back(key, JSON(value));
}

void JSON::Object::Add(const string &key, Object *value) {
  items_.emplace_back(key, JSON(value));
}

void JSON::Object::Add(const string &key, Array *value) {
  items_.emplace_back(key, JSON(value));
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

void JSON::Array::Add(int64 value) {
  elements_.emplace_back(JSON(value));
}

void JSON::Array::Add(double value) {
  elements_.emplace_back(JSON(value));
}

void JSON::Array::Add(bool value) {
  elements_.emplace_back(JSON(value));
}

void JSON::Array::Add(const string &value) {
  elements_.emplace_back(JSON(value));
}

void JSON::Array::Add(Object *value) {
  elements_.emplace_back(JSON(value));
}

void JSON::Array::Add(Array *value) {
  elements_.emplace_back(JSON(value));
}

JSON::Object *JSON::Array::AddObject(const string &key) {
  JSON::Object *o = new JSON::Object();
  elements_.emplace_back(JSON(o));
  return o;
}

JSON::Array *JSON::Array::AddArray(const string &key) {
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
  output->Write('}');
}

}  // namespace sling

