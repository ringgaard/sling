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

#include "sling/file/buffered.h"

#include "sling/base/logging.h"

namespace sling {

InputBuffer::InputBuffer(File *file, int buffer_size)
  : file_(file), buffer_size_(buffer_size) {
  // Allocate input buffer.
  buffer_ = static_cast<char *>(malloc(buffer_size));
  next_ = end_ = buffer_;
}

InputBuffer::~InputBuffer() {
  // Deallocate input buffer.
  free(buffer_);
}

void InputBuffer::Read(void *data, size_t size) {
  // Handle simple case where we have all the data in the buffer.
  if (size <= end_ - next_) {
    memcpy(data, next_, size);
    next_ += size;
    return;
  }

  char *p = static_cast<char *>(data);
  while (size > 0) {
    if (next_ == end_) {
      // Input buffer is empty.
      if (size > buffer_size_) {
        // Read data directly.
        file_->ReadOrDie(p, size);
        return;
      } else {
        // Fill input buffer.
        size_t bytes;
        CHECK(file_->Read(buffer_, buffer_size_, &bytes));
        next_ = buffer_;
        end_ = buffer_ + bytes;
      }
    } else {
      // Get data from the input buffer.
      size_t bytes = end_ - next_;
      if (bytes > size) bytes = size;
      memcpy(p, next_, bytes);
      next_ += bytes;
      p += bytes;
      size -= bytes;
    }
  }
}

OutputBuffer::OutputBuffer(File *file, int buffer_size) : file_(file) {
  // Allocate output buffer.
  buffer_ = static_cast<char *>(malloc(buffer_size));
  next_ = buffer_;
  end_ =  buffer_ + buffer_size;
}

OutputBuffer::~OutputBuffer() {
  // Flush output buffer.
  Flush();

  // Deallocate input buffer.
  free(buffer_);
}

void OutputBuffer::Flush() {
  if (next_ > buffer_) {
    file_->WriteOrDie(buffer_, next_ - buffer_);
    next_ = buffer_;
  }
}

void OutputBuffer::Write(const void *data, size_t size) {
  // Flush buffer if there is not enough room for data.
  if (size > end_ - next_) Flush();

  // Add data to buffer or directly to file if output buffer is too small.
  if (size <= end_ - next_) {
    memcpy(next_, data, size);
    next_ += size;
  } else {
    file_->WriteOrDie(data, size);
  }
}

}  // namespace sling

