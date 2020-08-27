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

#include "sling/util/iobuffer.h"

#include <string.h>

#include "sling/base/logging.h"

namespace sling {

void IOBuffer::Clear() {
  begin_ = end_ = floor_;
}

void IOBuffer::Reset(size_t size) {
  if (size != capacity()) {
    if (size == 0) {
      free(floor_);
      floor_ = ceil_ = begin_ = end_ = nullptr;
    } else {
      floor_ = static_cast<char *>(realloc(floor_, size));
      CHECK(floor_ != nullptr) << "Out of memory, " << size << " bytes";
      ceil_ = floor_ + size;
    }
  }
  begin_ = end_ = floor_;
}

void IOBuffer::Resize(size_t size) {
  if (size != capacity()) {
    size_t offset = begin_ - floor_;
    size_t used = end_ - begin_;
    floor_ = static_cast<char *>(realloc(floor_, size));
    CHECK(floor_ != nullptr) << "Out of memory, " << size << " bytes";
    ceil_ = floor_ + size;
    begin_ = floor_ + offset;
    end_ = begin_ + used;
  }
}

void IOBuffer::Flush() {
  if (begin_ > floor_) {
    size_t used = end_ - begin_;
    memmove(floor_, begin_, used);
    begin_ = floor_;
    end_ = begin_ + used;
  }
}

void IOBuffer::Ensure(size_t size) {
  if (remaining() < size) {
    size_t minsize = end_ - floor_ + size;
    size_t newsize = ceil_ - floor_;
    if (newsize == 0) newsize = 4096;
    while (newsize < minsize) newsize *= 2;
    Resize(newsize);
  }
}

char *IOBuffer::Append(size_t size) {
  Ensure(size);
  char *data = end_;
  end_ += size;
  return data;
}

char *IOBuffer::Consume(size_t size) {
  DCHECK_LE(size, available());
  char *data = begin_;
  begin_ += size;
  return data;
}

bool IOBuffer::Read(void *data, size_t size) {
  if (size > available()) return false;
  memcpy(data, begin_, size);
  begin_ += size;
  return true;
}

void IOBuffer::Write(const void *data, size_t size) {
  Ensure(size);
  memcpy(end_, data, size);
  end_ += size;
}

void IOBuffer::Unread(size_t size) {
  DCHECK_LE(size, consumed());
  begin_ -= size;
}

void IOBuffer::Unwrite(size_t size) {
  DCHECK_LE(size, available());
  end_ -= size;
}

}  // namespace sling

