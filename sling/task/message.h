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

#ifndef SLING_TASK_MESSAGE_H_
#define SLING_TASK_MESSAGE_H_

#include "sling/base/macros.h"
#include "sling/base/slice.h"

namespace sling {
namespace task {

// A task message has a key and a value data buffer which are owned by the
// message.
class Message {
 public:
  // A data buffer owns a block of memory.
  class Buffer {
   public:
    // Create empty buffer.
    Buffer() : data_(nullptr), size_(0) {}

    // Allocate buffer with n bytes.
    explicit Buffer(size_t n);

    // Allocate buffer and initialize it with data.
    explicit Buffer(Slice source);

    // Delete buffer.
    ~Buffer() { free(data_); }

    // Return buffer as slice.
    Slice slice() const { return Slice(data_, size_); }

    // Set new value for buffer.
    void set(Slice value) {
      free(data_);
      if (value.empty()) {
        data_ = nullptr;
        size_ = 0;
      } else {
        size_ = value.size();
        data_ = static_cast<char *>(malloc(size_));
        memcpy(data_, value.data(), size_);
      }
    }

    // Release buffer and transfer ownership to caller.
    char *release() {
      char *buffer = data_;
      data_ = nullptr;
      size_ = 0;
      return buffer;
    }

    // Swap data with another buffer.
    void swap(Buffer *other) {
      char *d = other->data_;
      size_t s = other->size_;
      other->data_ = data_;
      other->size_ = size_;
      data_ = d;
      size_ = s;
    }

    // Return pointer to buffer memory.
    char *data() { return data_; }
    const char *data() const { return data_; }

    // Return size of buffer.
    size_t size() const { return size_; }

   private:
    DISALLOW_COPY_AND_ASSIGN(Buffer);

    // Data buffer.
    char *data_;

    // Data buffer size.
    size_t size_;
  };

  // Create message from key and value data slices.
  Message(Slice key, Slice value) : key_(key), value_(value) {}
  Message(Slice key, uint64 serial, Slice value)
      : key_(key), serial_(serial), value_(value) {}
  Message(Slice value) : key_(), value_(value) {}

  // Create message with uninitialized content.
  Message(int key_size, int value_size) : key_(key_size), value_(value_size) {}
  Message() : key_(0), value_(0) {}

  // Return key buffer.
  Slice key() const { return key_.slice(); }

  // Return serial.
  uint64 serial() const { return serial_; }

  // Return value buffer.
  Slice value() const { return value_.slice(); }

  // Set key.
  void set_key(Slice key) { key_.set(key); }

  // Set serial.
  void set_serial(uint64 serial) { serial_ = serial; }

  // Set value.
  void set_value(Slice value) { value_.set(value); }

  // Release key buffer and transfer ownership to caller.
  char *release_key() { return key_.release(); }

  // Release value buffer and transfer ownership to caller.
  char *release_value() { return value_.release(); }

  // Swap key, serial, and value with another message.
  void swap(Message *other) {
    key_.swap(&other->key_);
    std::swap(serial_, other->serial_);
    value_.swap(&other->value_);
  }

  // Combined size of key and value.
  size_t size() const {
    return key_.size() + value_.size() + (serial_ ? sizeof(uint64) : 0);
  }

  // Return key buffer.
  Buffer *key_buffer() { return &key_; }

  // Return value buffer.
  Buffer *value_buffer() { return &value_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Message);

  // Key, serial, and value data buffer.
  Buffer key_;
  uint64 serial_ = 0;
  Buffer value_;
};

}  // namespace task
}  // namespace sling

#endif  // SLING_TASK_MESSAGE_H_

