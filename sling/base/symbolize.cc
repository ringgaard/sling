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

#include "sling/base/symbolize.h"

#include <cxxabi.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

namespace sling {

namespace {

// Read file line-by-line using safe low-level I/O function.
class LineReader {
 public:
  // Initialize line reader with file and input buffer.
  LineReader(int fd, char *buffer, int size)
      : fd_(fd),
        buffer_(buffer), limit_(buffer + size),
        ptr_(buffer), end_(buffer) {}

  // Read next line from file and return it as a nul-terminated string.
  char *ReadLine() {
    while (1) {
      // Find next linefeed.
      char *p = ptr_;
      while (p < end_ && *p != '\n') p++;

      // Return line if newline found.
      if (*p == '\n') {
        char *line = ptr_;
        *p = 0;
        ptr_ = p + 1;
        return line;
      }

      // Move remaining data to beginning of buffer.
      if (ptr_ < end_) {
        size_t remaining = end_ - ptr_;
        memmove(buffer_, ptr_, remaining);
        ptr_ = buffer_;
        end_ = buffer_ + remaining;
      } else {
        ptr_ = end_ = buffer_;
      }

      // Stop on overflow.
      int left = limit_ - end_;
      if (left == 0) return nullptr;

      // Read more data into buffer.
      int bytes = read(fd_, end_, left - 1);
      if (bytes <= 0) return nullptr;
      end_ += bytes;
      ptr_ = buffer_;
    }
  }

 private:
  int fd_;          // file to read from
  char *buffer_;    // input buffer
  char *limit_;     // end of input buffer
  char *ptr_;       // current position in input buffer
  char *end_;       // end of data in input buffer
};

// Parse hexadecimal number and return pointer to first character after number.
char *ParseHex(char *str, uint64 *value) {
  uint64 num = 0;
  while (*str) {
    int c = *str;
    if (c >= '0' && c <= '9') {
      num = num * 16 + (c - '0');
    } else if (c >= 'a' && c <= 'f') {
      num = num * 16 + (c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      num = num * 16 + (c - 'A' + 10);
    } else {
      break;
    }
    str++;
  }
  *value = num;
  return str;
}

// Parse hexadecimal address and return pointer to first character after number.
char *ParseAddr(char *str, const void **addr) {
  uint64 value;
  str = ParseHex(str, &value);
  *addr = reinterpret_cast<void *>(value);
  return str;
}

// Select the best symbol returning 1 for sym1 and 2 for sym2.
int BestSymbol(const Elf64_Sym &sym1, const Elf64_Sym &sym2) {
  // If one of the symbols is weak and the other is not, pick the one
  // this is not a weak symbol.
  char bind1 = ELF64_ST_BIND(sym1.st_info);
  char bind2 = ELF64_ST_BIND(sym1.st_info);
  if (bind1 != STB_WEAK && bind2 == STB_WEAK) return 1;
  if (bind1 == STB_WEAK && bind2 != STB_WEAK) return 2;

  // If one of the symbols has zero size and the other is not, pick the
  // one that has non-zero size.
  if (sym1.st_size != 0 && sym2.st_size == 0) return 1;
  if (sym1.st_size == 0 && sym2.st_size != 0) return 2;

  // If one of the symbols has a type and the other has not, pick the
  // one that has a type.
  char type1 = ELF64_ST_TYPE(sym1.st_info);
  char type2 = ELF64_ST_TYPE(sym2.st_info);
  if (type1 != STT_NOTYPE && type2 == STT_NOTYPE) return 1;
  if (type1 == STT_NOTYPE && type2 != STT_NOTYPE) return 2;

  // Pick the first one, if we still cannot decide.
  return 1;
}

}  // namespace

Symbolizer::Allocator::~Allocator() {
  // Free all allocated memory regions.
  while (heap_ != nullptr) {
    Region *prev = heap_->prev;
    munmap(heap_, heap_->size);
    heap_ = prev;
  }
}

char *Symbolizer::Allocator::Alloc(size_t size) {
  if (heap_next_ + size > heap_end_) {
    // Allocate new memory region (1MB).
    size_t bytes = 1 << 20;
    if (bytes < size + sizeof(Region)) {
      bytes = size + sizeof(Region);
    }
    Region *region = reinterpret_cast<Region *>(
        mmap(0, bytes, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (region == nullptr) return nullptr;
    region->prev = heap_;
    region->size = bytes;
    heap_ = region;
    heap_next_ = heap_->begin();
    heap_end_ = heap_->end();
  }

  // Allocate memory from current memory region.
  char *mem = heap_next_;
  heap_next_ += size;
  return mem;
}

char *Symbolizer::Allocator::Dup(char *str) {
  size_t size = strlen(str) + 1;
  char *dup = Alloc(size);
  memcpy(dup, str, size);
  return dup;
}

const char *Symbolizer::ObjectFile::Name() const {
  const char *base = filename;
  for (const char *p = filename; *p != 0; ++p) {
    if (*p == '/') base = p + 1;
  }
  return base;
}

bool Symbolizer::ObjectFile::Read(void *buf, size_t size, off_t offset) {
  if (fd == -1) {
    fd = open(filename, O_RDONLY);
    if (fd < 0) return false;
  }
  off_t off = lseek(fd, offset, SEEK_SET);
  if (off != offset) return false;
  int bytes = read(fd, buf, size);
  return bytes == size;
}

Symbolizer::Symbolizer() {
  // Allocate scratch buffer.
  buffer_size_ = 1024;
  buffer_ = alloc_.Alloc(buffer_size_);

  // Read address map.
  ReadAddressMap();
}

Symbolizer::~Symbolizer() {
  // Close all object files.
  for (ObjectFile *obj = mappings_; obj != nullptr; obj = obj->next) {
    if (obj->fd != -1) close(obj->fd);
  }
}

void Symbolizer::ReadAddressMap() {
  // Open address map.
  int fd = open("/proc/self/maps", O_RDONLY);
  if (fd < 0) return;

  LineReader reader(fd, buffer_, buffer_size_);
  char *p;
  while ((p = reader.ReadLine()) != nullptr) {
    // parse line in /proc/self/maps, e.g.:
    //
    // 08048000-0804c000 r-xp 00000000 08:01 2142121    /bin/cat
    //
    // We need start address (08048000), end address (0804c000), flags
    // (r-xp), offset (00000000) and file name (/bin/cat).

    // Read start and end address.
    const void *start;
    const void *end;
    p = ParseAddr(p, &start);
    if (*p++ != '-') continue;
    p = ParseAddr(p, &end);
    if (*p++ != ' ') continue;

    // Read flags.  Skip flags until we encounter a space.
    bool executable = false;
    while (*p && *p != ' ') {
      if (*p == 'x') executable = true;
      p++;
    }
    if (*p++ != ' ') continue;
    if (!executable) continue;

    // Read offset.
    uint64 offset;
    p = ParseHex(p, &offset);
    if (*p++ != ' ') continue;

    // Skip to file name.
    int spaces = 0;
    while (*p) {
      if (*p == ' ') {
        spaces++;
      } else if (spaces >= 2) {
        break;
      }
      p++;
    }

    // Skip special mappings, e.g. [vdso] and [vsyscall].
    if (*p == '[') continue;

    // Add object file to mapping.
    ObjectFile *obj = alloc_.AllocObject<ObjectFile>();
    obj->start = start;
    obj->end = end;
    obj->filename = alloc_.Dup(p);
    obj->next = mappings_;
    obj->fd = -1;
    mappings_ = obj;

    // Adjust relocation for dynamic objects.
    const Elf64_Ehdr *ehdr = reinterpret_cast<const Elf64_Ehdr *>(start);
    if (ehdr->e_type == ET_DYN &&
        reinterpret_cast<uint64>(start) != offset) {
      obj->relocation = reinterpret_cast<ptrdiff_t>(start) - offset;
    } else {
      obj->relocation = 0;
    }
  }
  close(fd);
}

Symbolizer::ObjectFile *Symbolizer::FindObjectFile(const void *address) {
  for (ObjectFile *obj = mappings_; obj != nullptr; obj = obj->next) {
    if (obj->start <= address && address < obj->end) return obj;
  }
  return nullptr;
}

bool Symbolizer::FindSymbol(const void *address, Location *loc) {
  // Clear location.
  memset(loc, 0, sizeof(Location));
  loc->address = address;

  // Find object file containing address.
  ObjectFile *obj = FindObjectFile(address);
  if (obj == nullptr) return false;

  // Read ELF header.
  Elf64_Ehdr ehdr;
  if (!obj->Read(&ehdr, sizeof(Elf64_Ehdr), 0)) return false;

  // Try to find symbol in a regular symbol table, then fall back to the
  // dynamic symbol table.
  Elf64_Sym match;
  memset(&match, 0, sizeof(Elf64_Sym));
  bool found = false;
  for (auto symtype : {SHT_SYMTAB, SHT_DYNSYM}) {
    // Try to find section with the specified type.
    Elf64_Shdr symtab;
    bool syms_found = false;
    for (int i = 0; i < ehdr.e_shnum; ++i) {
      off_t pos = ehdr.e_shoff + i * sizeof(Elf64_Shdr);
      if (!obj->Read(&symtab, sizeof(Elf64_Shdr), pos)) return false;
      if (symtab.sh_type == symtype) {
        syms_found = true;
        break;
      }
    }
    if (!syms_found) continue;

    // Get symbol string table section.
    Elf64_Shdr strtab;
    off_t pos = ehdr.e_shoff + symtab.sh_link * sizeof(Elf64_Shdr);
    if (!obj->Read(&strtab, sizeof(Elf64_Shdr), pos)) continue;

    // Find best matching symbol.
    int num_symbols = symtab.sh_size / symtab.sh_entsize;
    for (int i = 0; i < num_symbols; ++i) {
      Elf64_Sym sym;
      off_t pos = symtab.sh_offset + i * symtab.sh_entsize;
      if (!obj->Read(&sym, sizeof(Elf64_Sym), pos)) break;

      // Skip null and undefined symbols.
      if (sym.st_value == 0 || sym.st_shndx == 0) continue;

      // Check if symbol contains address.
      const char *start_address = obj->Address(sym.st_value);
      const char *end_address = obj->Address(sym.st_value, sym.st_size);
      if (address < start_address) continue;
      if (address >= end_address) continue;

      // Pick best symbol.
      if (!found || BestSymbol(sym, match) == 1) {
        found = true;
        match = sym;
      }
    }

    // Get symbol name and location if match was found.
    if (found) {
      // Read symbol name.
      off_t pos = strtab.sh_offset + match.st_name;
      obj->Read(buffer_, buffer_size_ - 1, pos);
      loc->symbol = alloc_.Dup(buffer_);

      // Demangle C++ symbols.
      int status;
      size_t size = buffer_size_;
      char *demangled = abi::__cxa_demangle(loc->symbol,
                                            buffer_, &size,
                                            &status);
      if (demangled != nullptr) {
        loc->symbol = alloc_.Dup(demangled);
      }

      // Set offset from symbol and object file.
      loc->offset = reinterpret_cast<const char *>(address) -
                    obj->Address(match.st_value);
      loc->file = obj->Name();
      return true;
    }
  }

  // Only fill out object file and offset if no matching object file  is found.
  loc->offset = reinterpret_cast<const char *>(address) -
                reinterpret_cast<const char *>(obj->start);
  loc->file = obj->Name();
  return false;
}

}  // namespace sling

