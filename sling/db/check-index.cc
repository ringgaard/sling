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

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/db/dbindex.h"

using namespace sling;

DEFINE_string(index, "", "Index file to check");
DEFINE_bool(fix, false, "Fix index errors");

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Load index.
  CHECK(!FLAGS_index.empty());
  DatabaseIndex index;
  CHECK(index.Open(FLAGS_index));

  VLOG(1) << "epoch: " << index.epoch();
  VLOG(1) << "capacity: " << index.capacity();
  VLOG(1) << "limit: " << index.limit();
  VLOG(1) << "records: " << index.num_records();
  VLOG(1) << "deleted: " << index.num_deleted();

  // Check index integrity.
  index.Check(FLAGS_fix);

  // Close index.
  CHECK(index.Close());

  return 0;
}
