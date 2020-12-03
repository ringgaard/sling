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

#ifndef SLING_BASE_STACKTRACE_H_
#define SLING_BASE_STACKTRACE_H_

#include "sling/base/types.h"

namespace sling {

// A thread context keeps track of the current element being processed by
// the thread.
struct ThreadContext {
  ThreadContext(const char *type, const char *context, size_t size);
  ~ThreadContext();

  const char *type;
  const char *context;
  size_t size;
  ThreadContext *prev;
};

// Dump stack trace to output file.
void DumpStackTrace(int fd, void *address = nullptr);

// Install signal handlers to dump stack trace on crashes.
void InstallFailureSignalHandlers();

}  // namespace sling

#endif  // SLING_BASE_STACKTRACE_H_

