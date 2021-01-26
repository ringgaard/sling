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

#ifndef SLING_FILE_BUFFERED_H_
#define SLING_FILE_BUFFERED_H_

#include "sling/file/file.h"

namespace sling {

// File input buffer.
class InputBuffer {
 public:
  InputBuffer(File *file, int buffer_size = 1 << 16);
  ~InputBuffer();

  // Read bytes from buffer. Fail on errors.
  void Read(void *data, size_t size);

 private:
  // The underlying file is not owned by the input buffer.
  File *file_ = nullptr;

  // Input buffer.
  int buffer_size_;
  char *buffer_;
  char *next_;
  char *end_;
};

// File output buffer.
class OutputBuffer {
 public:
  OutputBuffer(File *file, int buffer_size = 1 << 16);
  ~OutputBuffer();

  // Write data to output through output buffer. Fail on errors.
  void Write(const void *data, size_t size);

  // Flush output buffer.
  void Flush();

 private:
  // Output file.
  File *file_;

  // Output buffer.
  char *buffer_;
  char *next_;
  char *end_;
};

}  // namespace sling

#endif  // SLING_FILE_BUFFERED_H_

