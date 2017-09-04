/* Copyright 2016 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef SYNTAXNET_UTILS_H_
#define SYNTAXNET_UTILS_H_

#include <string>

#include "syntaxnet/base.h"

namespace syntaxnet {
namespace utils {

bool ParseInt32(const char *c_str, int *value);
bool ParseInt64(const char *c_str, int64 *value);
bool ParseDouble(const char *c_str, double *value);

template <typename T>
T ParseUsing(const string &str, std::function<bool(const char *, T *)> func) {
  T value;
  CHECK(func(str.c_str(), &value)) << "Failed to convert: " << str;
  return value;
}

template <typename T>
T ParseUsing(const string &str, T defval,
             std::function<bool(const char *, T *)> func) {
  return str.empty() ? defval : ParseUsing<T>(str, func);
}

}  // namespace utils
}  // namespace syntaxnet

#endif  // SYNTAXNET_UTILS_H_
