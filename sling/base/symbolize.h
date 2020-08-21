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

#ifndef SLING_BASE_SYMBOLIZE_H_
#define SLING_BASE_SYMBOLIZE_H_

#include "sling/base/types.h"

#include <elf.h>

namespace sling {

// A symbolizer is used for converting instruction pointer address to
// human-readable names. This is used by the signal failure handler to
// output stack traces on crashes. This code is called from signal handlers,
// so it has been designed to be async-safe.
class Symbolizer {
 public:
  // Symbolic location in process of address.
  struct Location {
    const void *address;   // memory location in process
    const char *symbol;    // name of symbol covering location
    const char *file;      // object file containing symbol
    ptrdiff_t offset;      // offset of address from symbol
  };

  // Initialize symbolizer.
  Symbolizer();

  // Free resources used by symbolizer.
  ~Symbolizer();

  // Find symbolic location for address.
  bool FindSymbol(const void *address, Location *loc);

 private:
  // Async-safe memory allocator.
  class Allocator {
   public:
    // Free allocated memory regions.
    ~Allocator();

    // Allocate memory block from private heap.
    char *Alloc(size_t size);

    // Allocate memory for object.
    template<class T> T *AllocObject() {
      return reinterpret_cast<T *>(Alloc(sizeof(T)));
    }

    // Duplicate string.
    char *Dup(char *str);

   private:
    // Memory region for allocating new memory blocks.
    struct Region {
      Region *prev;   // pointer to previous memory region
      size_t size;    // size of this memory region

      char *begin() { return reinterpret_cast<char *>(this + 1); }
      char *end() { return reinterpret_cast<char *>(this) + size; }
    };

    // Current region for memory allocator.
    Region *heap_ = nullptr;

    // Free range of memory in current region.
    char *heap_next_ = nullptr;
    char *heap_end_ = nullptr;
  };

  // Object file loaded into the process address space.
  struct ObjectFile {
    const void *start;      // start address of object file
    const void *end;        // end address of object file
    const char *filename;   // object file name
    ObjectFile *next;       // next object file in list
    int fd;                 // file handle for object file
    ptrdiff_t relocation;   // relocation offset for relocated object

    // Return base file name without path.
    const char *Name() const;

    // Compute address of offset in object file.
    const char *Address(Elf64_Addr addr, ptrdiff_t offset = 0) {
      return reinterpret_cast<const char *>(addr) + relocation + offset;
    }

    // Read data from object file.
    bool Read(void *buf, size_t size, off_t offset);
  };

  // Read address map for process.
  void ReadAddressMap();

  // Find object file which contains the address.
  ObjectFile *FindObjectFile(const void *address);

  // Internal memory allocator.
  Allocator alloc_;

  // Scratch buffer for temporary storage.
  char *buffer_;
  size_t buffer_size_;

  // List of object files mapped into process address space.
  ObjectFile *mappings_ = nullptr;
};

}  // namespace sling

#endif  // SLING_BASE_SYMBOLIZE_H_

