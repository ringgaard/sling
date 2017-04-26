#include "task/message.h"

#include <string.h>

namespace sling {
namespace task {

Buffer::Buffer(Slice source) {
  if (source.empty()) {
    data_ = nullptr;
    size_ = 0;
  } else {
    size_ = source.size();
    data_ = new char[size_];
    memcpy(data_, source.data(), size_);
  }
}

}  // namespace task
}  // namespace sling

