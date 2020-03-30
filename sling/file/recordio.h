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

#ifndef SLING_FILE_RECORDIO_H_
#define SLING_FILE_RECORDIO_H_

#include <vector>

#include "sling/base/slice.h"
#include "sling/base/status.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/util/iobuffer.h"

namespace sling {

// Record types.
enum RecordType {
  DATA_RECORD   = 1,     // data record with key and value
  FILLER_RECORD = 2,     // filler record to avoid records crossing chunks
  INDEX_RECORD  = 3,     // index page
  VDATA_RECORD = 4,      // versioned data record
};

// Record with key and value.
struct Record {
  Record() {}
  Record(const Slice &key, const Slice &value)
      : key(key), value(value) {}
  Record(const Slice &key, uint64 version, const Slice &value)
      : key(key), value(value), version(version) {}

  RecordType type = DATA_RECORD;
  Slice key;
  Slice value;
  uint64 version = 0;
  int64 position = -1;
};

class RecordFile {
 public:
  // Maximum record header length.
  static const int MAX_HEADER_LEN = 31;

  // Maximum skip record length.
  static const int MAX_SKIP_LEN = 12;

  // Magic numbers for identifying record files.
  static const uint32 MAGIC1 = 0x46434552;  // RECF
  static const uint32 MAGIC2 = 0x44434552;  // RECD

  // Compression types.
  enum CompressionType {
    UNCOMPRESSED = 0,
    SNAPPY = 1,
  };

  // File header information.
  struct FileHeader {
    uint32 magic;
    uint8 hdrlen;
    uint8 compression;
    uint16 flags;
    uint64 index_root;
    uint64 chunk_size;
    uint64 index_start;
    uint32 index_page_size;
    uint32 index_depth;
  };

  // Record header information.
  struct Header {
    RecordType record_type;
    uint64 record_size;
    uint64 key_size;
    uint64 version;
  };

  // An index record consists of a list of index entries containing the key
  // fingerprint of the record and the position of the record in the record
  // file, or the fingerprint of the first record in the subtree for non-leaf
  // index records.
  struct IndexEntry {
    IndexEntry(uint64 fp, uint64 pos) : fingerprint(fp), position(pos) {}
    uint64 fingerprint;
    uint64 position;
  };
  typedef std::vector<IndexEntry> Index;

  // One page in a record file index.
  struct IndexPage {
    IndexPage(uint64 pos, const Slice &data);
    ~IndexPage();

    // Find index of last entry in page that is less than fp.
    int Find(uint64 fp) const;

    uint64 position;
    int size;
    IndexEntry *entries;
    uint64 lru;
  };

  // Parse header from data. Returns the number of bytes read or -1 on error.
  static size_t ReadHeader(const char *data, Header *header);

  // Write header to data. Returns number of bytes written.
  static size_t WriteHeader(const Header &header, char *data);
};

// Configuration options for record file.
struct RecordFileOptions {
  // Input/output buffer size.
  int buffer_size = 4096;

  // Chunk size. Records never overlap chunk boundaries.
  int chunk_size = 64 * (1 << 20);

  // Record compression.
  RecordFile::CompressionType compression = RecordFile::SNAPPY;

  // Record files can be indexed for fast retrieval by key.
  bool indexed = false;

  // Open record writer in append mode.
  bool append = false;

  // Number of entries in each index record.
  int index_page_size = 1024;

  // Number of pages in index page cache.
  int index_cache_size = 256;
};

// Reader for reading records from a record file.
class RecordReader : public RecordFile {
 public:
  // Open record file for reading.
  RecordReader(File *file, const RecordFileOptions &options, bool owned = true);
  RecordReader(const string &filename, const RecordFileOptions &options);
  explicit RecordReader(File *file);
  explicit RecordReader(const string &filename);
  ~RecordReader();

  // Close record file.
  Status Close();

  // Return true if we have read all records in the file.
  bool Done() { return position_ >= size_; }

  // Read next record from record file.
  Status Read(Record *record);

  // Return current position in record file.
  uint64 Tell() { return position_; }

  // Seek to new position in record file.
  Status Seek(uint64 pos);

  // Seek to first record in record file.
  Status Rewind() { return Seek(info_.hdrlen); }

  // Skip bytes in input. The offset can be negative.
  Status Skip(int64 n) { return Seek(position_ + n); }

  // Read index page. Ownership of the index page is transferred to the caller.
  IndexPage *ReadIndexPage(uint64 position);

  // Record file header information.
  const FileHeader &info() const { return info_; }

  // Underlying file object for reader.
  File *file() const { return file_; }

  // File size.
  uint64 size() const { return size_; }

 private:
  // Fill input buffer.
  Status Fill(uint64 needed);

  // Input file.
  File *file_;

  // Flag to indicate that file object is owned by reader.
  bool owned_;

  // File size.
  uint64 size_;

  // Current position in record file.
  uint64 position_;

  // In readahead mode the input buffer is filled to prefetch the next records.
  // The readahead flag is cleared when seeking to a new position in the file.
  bool readahead_ = true;

  // Record file meta information.
  FileHeader info_;

  // Input buffer.
  IOBuffer input_;

  // Buffer for decompressed record data.
  IOBuffer decompressed_data_;

  friend class RecordWriter;
};

// Index for looking up records in an indexed record file.
class RecordIndex : public RecordFile {
 public:
  RecordIndex(RecordReader *reader, const RecordFileOptions &options);
  ~RecordIndex();

  // Look up record by key. Returns false if no matching record is found.
  bool Lookup(const Slice &key, Record *record, uint64 fp);
  bool Lookup(const Slice &key, Record *record);

  // Return record reader.
  RecordReader *reader() const { return reader_; }

 private:
  // Get index page at position.
  IndexPage *GetIndexPage(uint64 position);

  // Mark index page as accessed.
  IndexPage *access(IndexPage *page) {
    page->lru = epoch_++;
    return page;
  }

  // Record file with index (not owned).
  RecordReader *reader_;

  // Root index page.
  IndexPage *root_;

  // Maximum index page cache size.
  int cache_size_;

  // Epoch for LRU cache eviction.
  uint64 epoch_ = 0;

  // Index page cache.
  std::vector<RecordFile::IndexPage *> cache_;
};

// A record database is a sharded set of indexed record files where records can
// be looked up by key. The records must be sharded by key fingerprint.
class RecordDatabase {
 public:
  // Open record database.
  RecordDatabase(const string &filepattern, const RecordFileOptions &options);
  RecordDatabase(const std::vector<string> &filenames,
                 const RecordFileOptions &options);
  ~RecordDatabase();

  // Read record from shard at some position.
  bool Read(int shard, int64 position, Record *record);

  // Look up record by key. Returns false if no matching record is found.
  bool Lookup(const Slice &key, Record *record);

  // Retrieve the next record from the current shard.
  bool Next(Record *record);

  // Return true if we have read all records in the database.
  bool Done() { return current_shard_ >= shards_.size(); }

  // Go to first record in the first shard.
  Status Rewind();

  // Current shard.
  int current_shard() const { return current_shard_; }

 private:
  // Skip forward until next shard that is not empty.
  void Forward();

  // Shards in record database.
  std::vector<RecordIndex *> shards_;

  // Current shard for retrieving the next document.
  int current_shard_ = 0;
};

// Writer for writing records to record file.
class RecordWriter : public RecordFile {
 public:
  // Open record file for writing.
  RecordWriter(File *file, const RecordFileOptions &options);
  RecordWriter(const string &filename, const RecordFileOptions &options);
  explicit RecordWriter(File *file);
  explicit RecordWriter(const string &filename);
  ~RecordWriter();

  // Open record file for shared reading and writing.
  RecordWriter(RecordReader *reader, const RecordFileOptions &options);

  // Close record file.
  Status Close();

  // Flush output buffer to disk.
  Status Flush();

  // Write record to record file. If position is not null, it is set to the
  // position of the new record.
  Status Write(const Record &record, uint64 *position = nullptr);

  // Write key/value pair to file.
  Status Write(const Slice &key, const Slice &value) {
    return Write(Record(key, value));
  }

  // Write key/version/value triple to file.
  Status Write(const Slice &key, uint64 version, const Slice &value) {
    return Write(Record(key, version, value));
  }

  // Write record with empty key.
  Status Write(const Slice &value) {
    return Write(Record(Slice(), value));
  }

  // Return current position in record file.
  uint64 Tell() const { return position_; }

  // Sync a record reader to this writer.
  void Sync(RecordReader *reader) const {
    reader->size_ = position_;
  }

  // Add index to existing record file.
  static Status AddIndex(const string &filename,
                         const RecordFileOptions &options);

 private:
  // Write index to disk.
  Status WriteIndex();

  // Write one level of the index to file.
  Status WriteIndexLevel(const Index &level, Index *parent, int page_size);

  // Output file.
  File *file_;

  // Current position in record file.
  uint64 position_;

  // Record file meta information.
  FileHeader info_;

  // Output buffer.
  IOBuffer output_;

  // Buffer for compressed record data.
  IOBuffer compressed_data_;

  // Index entries for building index.
  Index index_;

  // Record reader with shared file.
  RecordReader *reader_ = nullptr;
};

}  // namespace sling

#endif  // SLING_FILE_RECORDIO_H_

