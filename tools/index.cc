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

#include <iostream>
#include <string>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/file.h"
#include "sling/file/recordio.h"

DEFINE_int32(buffer_size, 4096, "Input/output buffer size");
DEFINE_int32(index_page_size, 2014, "Number of entries in each index record");

using namespace sling;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Get files to index.
  std::vector<string> files;
  for (int i = 1; i < argc; ++i) {
    File::Match(argv[i], &files);
  }

  // Set record file options.
  RecordFileOptions options;
  options.buffer_size = FLAGS_buffer_size;
  options.index_page_size = FLAGS_index_page_size;

  // Add index to files.
  for (const string &file : files) {
    std::cout << "Indexing " << file << "\n";
    CHECK(RecordWriter::AddIndex(file, options));
  }

  std::cout << "Done.\n";
  return 0;
}
