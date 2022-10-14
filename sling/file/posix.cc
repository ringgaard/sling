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

#include "sling/file/posix.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "sling/base/perf.h"
#include "sling/file/file.h"

namespace sling {

namespace {

Status IOError(const string &context, int error) {
  return Status(error, context.c_str(), strerror(error));
}

int OpenFlags(const char *mode) {
  int flags = 0;
  switch (*mode++) {
    case 'r': flags = O_RDONLY; break;
    case 'w': flags = O_WRONLY | O_CREAT | O_TRUNC; break;
    case 'a': flags = O_WRONLY | O_CREAT | O_APPEND; break;
  }

  if (*mode == '+') {
    flags &= ~(O_RDONLY | O_WRONLY);
    flags |= O_RDWR;
  }

  return flags;
}

const char *GetTempDir() {
  static const char *tmpdir = nullptr;
  if (tmpdir != nullptr) return tmpdir;
  tmpdir = getenv("TMPDIR");
  if (tmpdir == nullptr) tmpdir = getenv("TMP");
  if (tmpdir == nullptr) tmpdir = "/tmp";
  return tmpdir;
}

}  // namespace

// POSIX file interface.
class PosixFile : public File {
 public:
  PosixFile(int fd, const string &filename)
      : fd_(fd), filename_(filename) {}

  ~PosixFile() override {
    if (fd_ != -1) close(fd_);
  }

  Status PRead(uint64 pos, void *buffer, size_t size, uint64 *read) override {
    ssize_t rc = pread(fd_, buffer, size, pos);
    if (rc < 0) return IOError(filename_, errno);
    Perf::add_file_read(rc);
    if (read) *read = rc;
    return Status::OK;
  }

  Status Read(void *buffer, size_t size, uint64 *read) override {
    ssize_t rc = ::read(fd_, buffer, size);
    if (rc < 0) return IOError(filename_, errno);
    Perf::add_file_read(rc);
    if (read) *read = rc;
    return Status::OK;
  }

  Status PWrite(uint64 pos, const void *buffer, size_t size) override {
    ssize_t rc = pwrite(fd_, buffer, size, pos);
    if (rc < 0) return IOError(filename_, errno);
    Perf::add_file_write(rc);
    if (rc < size) return IOError(filename_, EIO);
    return Status::OK;
  }

  Status Write(const void *buffer, size_t size) override {
    // On Linux, write() will transfer at most 0x7ffff000 bytes.
    if (size <= 0x7ffff000) {
      ssize_t rc = write(fd_, buffer, size);
      if (rc < 0) return IOError(filename_, errno);
      Perf::add_file_write(rc);
      if (rc != size) return IOError(filename_, EIO);
    } else {
      while (size > 0) {
        size_t bytes = size;
        if (bytes > 0x7ffff000) bytes = 0x7ffff000;
        ssize_t rc = write(fd_, buffer, bytes);
        if (rc < 0) return IOError(filename_, errno);
        Perf::add_file_write(rc);
        size -= rc;
      }
    }
    return Status::OK;
  }

  void *MapMemory(uint64 pos, size_t size,
                  bool writable, bool preload) override {
    void *mapping = mmap(nullptr, size,
                         PROT_READ | (writable ? PROT_WRITE : 0),
                         (writable ? MAP_SHARED : MAP_PRIVATE) |
                         (preload ? MAP_POPULATE : 0),
                         fd_, pos);
    return mapping == MAP_FAILED ? nullptr : mapping;
  }

  Status Resize(uint64 size) override {
    if (ftruncate(fd_, size) == -1) return IOError(filename_, errno);
    return Status::OK;
  }

  Status Seek(uint64 pos) override {
    if (lseek(fd_, pos, SEEK_SET) == -1) return IOError(filename_, errno);
    return Status::OK;
  }

  Status Skip(uint64 n) override {
    if (lseek(fd_, n, SEEK_CUR) == -1) return IOError(filename_, errno);
    return Status::OK;
  }

  Status GetPosition(uint64 *pos) override {
    uint64 position = lseek(fd_, 0, SEEK_CUR);
    if (position == -1) return IOError(filename_, errno);
    *pos = position;
    return Status::OK;
  }

  Status GetSize(uint64 *size) override {
    struct stat st;
    if (fstat(fd_, &st) != 0) return IOError(filename_, errno);
    *size = st.st_size;
    return Status::OK;
  }

  Status Stat(FileStat *stat) override {
    struct stat st;
    if (fstat(fd_, &st) != 0) return IOError(filename_, errno);
    stat->size = st.st_size;
    stat->mtime = st.st_mtime;
    stat->is_file = S_ISREG(st.st_mode);
    stat->is_directory = S_ISDIR(st.st_mode);
    return Status::OK;
  }

  Status Close() override {
    if (fd_ != -1) {
      if (close(fd_) != 0) {
        delete this;
        return IOError(filename_, errno);
      }
      fd_ = -1;
    }
    delete this;
    return Status::OK;
  }

  Status Flush() override {
#ifdef __APPLE__
    if (fcntl(fd_, F_FULLFSYNC) != 0) return IOError(filename_, errno);
#else
    if (fdatasync(fd_) != 0) return IOError(filename_, errno);
#endif
    return Status::OK;
  }

  string filename() const override { return filename_; }

 private:
  // File descriptor.
  int fd_;

  // File name.
  string filename_;
};

// POSIX file system interface.
class PosixFileSystem : public FileSystem {
 public:
  void Init() override {
  }

  bool IsDefaultFileSystem() override {
    // POSIX is the default file system.
    return true;
  }

  Status Open(const string &name, const char *mode, File **f) override {
    // Open file.
    int fd = open(name.c_str(), OpenFlags(mode), 0644);
    if (fd == -1) return IOError(name, errno);

    // Return new file object.
    *f = new PosixFile(fd, name);
    return Status::OK;
  }

  Status CreateTempFile(File **f) override {
    char tmpname[PATH_MAX];
    strcpy(tmpname, GetTempDir());
    strcat(tmpname, "/scratch.XXXXXX");
    int fd = mkstemp(tmpname);
    if (fd == -1) return IOError(tmpname, errno);
    *f = new PosixFile(fd, tmpname);
    return Status::OK;
  }

  Status CreateTempDir(string *dir) override {
    char tmpname[PATH_MAX];
    strcpy(tmpname, GetTempDir());
    strcat(tmpname, "/local.XXXXXX");
    if (mkdtemp(tmpname) == nullptr) {
      return Status(errno, "mkdtemp", strerror(errno));
    }
    *dir = tmpname;
    return Status::OK;
  }

  bool FileExists(const string &filename) override {
    return access(filename.c_str(), F_OK) == 0;
  }

  Status GetFileSize(const string &filename, uint64 *size) override {
    struct stat st;
    if (stat(filename.c_str(), &st) != 0) return IOError(filename, errno);
    *size = st.st_size;
    return Status::OK;
  }

  Status DeleteFile(const string &filename) override {
    if (unlink(filename.c_str()) != 0) return IOError(filename, errno);
    return Status::OK;
  }

  Status Stat(const string &filename, FileStat *stat) override {
    struct stat st;
    if (::stat(filename.c_str(), &st) != 0) return IOError(filename, errno);
    stat->size = st.st_size;
    stat->mtime = st.st_mtime;
    stat->is_file = S_ISREG(st.st_mode);
    stat->is_directory = S_ISDIR(st.st_mode);
    return Status::OK;
  }

  Status RenameFile(const string &source, const string &target) override {
    if (rename(source.c_str(), target.c_str()) != 0) {
      return IOError(source, errno);
    }
    return Status::OK;
  }

  Status CreateDir(const string &dirname) override {
    if (mkdir(dirname.c_str(), 0755) != 0) return IOError(dirname, errno);
    return Status::OK;
  }

  Status DeleteDir(const string &dirname) override {
    if (rmdir(dirname.c_str()) != 0) return IOError(dirname, errno);
    return Status::OK;
  }

  Status Match(const string &pattern,
               std::vector<string> *filenames) override {
    glob_t globbuf;
    if (glob(pattern.c_str(), 0, nullptr, &globbuf) != 0) {
      return IOError(pattern, ENOENT);
    }
    for (int i = 0; i < globbuf.gl_pathc; ++i) {
      filenames->push_back(globbuf.gl_pathv[i]);
    }
    globfree(&globbuf);
    return Status::OK;
  }

  Status FlushMappedMemory(void *data, size_t size) override {
    if (msync(data, size, MS_SYNC) != 0) return IOError("msync", errno);
    return Status::OK;
  }

  Status FreeMappedMemory(void *data, size_t size) override {
    if (munmap(data, size) != 0) return IOError("munmap", errno);
    return Status::OK;
  }
};

File *NewFileFromDescriptor(const string &name, int fd) {
  return new PosixFile(fd, name);
}

File *NewStdoutFile() {
  return NewFileFromDescriptor("stdout", dup(1));
}

REGISTER_FILE_SYSTEM_TYPE("posix", PosixFileSystem);

}  // namespace sling

