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

#include "syntaxnet/utils.h"

namespace syntaxnet {
namespace utils {

bool ParseInt32(const char *c_str, int *value) {
  char *temp;
  *value = strtol(c_str, &temp, 0);  // NOLINT
  return (*temp == '\0');
}

bool ParseInt64(const char *c_str, int64 *value) {
  char *temp;
  *value = strtol(c_str, &temp, 0);  // NOLINT
  return (*temp == '\0');
}

bool ParseDouble(const char *c_str, double *value) {
  char *temp;
  *value = strtod(c_str, &temp);
  return (*temp == '\0');
}

}  // namespace utils
}  // namespace syntaxnet
