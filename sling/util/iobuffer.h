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

#ifndef SLING_UTIL_IOBUFFER_H_
#define SLING_UTIL_IOBUFFER_H_

#include "sling/base/types.h"
#include "sling/base/slice.h"

namespace sling {

// Memory buffer that owns a block of allocated memory. Data is written/appended
// to the unused portion of the buffer and is read/consumed from the used
// portion of the buffer.
//
//     +---------------------------------------------------------------+
//     |     consumed    |        used        |         unused         |
//     +---------------------------------------------------------------+
//     ^                 ^                     ^                        ^
//   floor             begin                  end                      ceil
//
//     <-- consumed() --><---- available() ---><----- remaining() ----->
//     <-------------------------- capacity() ------------------------->
//
class IOBuffer {
 public:
  ~IOBuffer() { free(floor_); }

  // Buffer capacity.
  size_t capacity() const { return ceil_ - floor_; }

  // Number of bytes consumed from buffer.
  size_t consumed() const { return begin_ - floor_; }

  // Number of bytes available in buffer.
  size_t available() const { return end_ - begin_; }

  // Number of bytes left in buffer.
  size_t remaining() const { return ceil_ - end_; }

  // Whether buffer is empty.
  bool empty() const { return begin_ == end_; }

  // Whether buffer is full.
  bool full() const { return end_ == ceil_; }

  // Beginning and end of used portion of the buffer.
  char *begin() const { return begin_; }
  char *end() const { return end_; }

  // Return used data.
  Slice data() const { return Slice(begin_, end_); }

  // Clear buffer.
  void Clear();

  // Clear buffer and allocate space.
  void Reset(size_t size);

  // Change buffer capacity keeping the used part.
  void Resize(size_t size);

  // Flush buffer by moving the used part to the beginning of the buffer.
  void Flush();

  // Make sure that at least 'size' bytes can be written/appended to buffer .
  void Ensure(size_t size);

  // Append data to buffer.
  char *Append(size_t size);
  template<typename T> T *append(size_t n = 1) {
    return reinterpret_cast<T *>(Append(n * sizeof(T)));
  }

  // Consume data from buffer.
  char *Consume(size_t size);
  template<typename T> T *consume(size_t n = 1) {
    return reinterpret_cast<T *>(Consume(n * sizeof(T)));
  }

  // Read data from buffer. Return false if not enough data is available.
  bool Read(void *data, size_t size);

  // Write data to buffer.
  void Write(const void *data, size_t size);
  void Write(const char *str) { if (str) Write(str, strlen(str)); }
  void Write(const string &str) { Write(str.data(), str.size()); }
  void Write(const Slice &slice) { Write(slice.data(), slice.size()); }
  void Write(char ch) { Write(&ch, 1); }
  void Write(unsigned char ch) { Write(&ch, 1); }

  // Copy data from another buffer.
  void Copy(IOBuffer *buffer, size_t size) {
    Write(buffer->Consume(size), size);
  }

  // Unread already read data.
  void Unread(size_t size);

  // Unwrite already written data.
  void Unwrite(size_t size);

 private:
  char *floor_ = nullptr;  // start of allocated memory
  char *ceil_ = nullptr;   // end of allocated memory
  char *begin_ = nullptr;  // beginning of used part of buffer
  char *end_ = nullptr;    // end of used part of buffer
};

}  // namespace sling

#endif  // SLING_UTIL_IOBUFFER_H_

