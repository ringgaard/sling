// Copyright 2025 Ringgaard Research ApS
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

// Companies House converter.

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/frame/reader.h"
#include "sling/stream/input.h"
#include "sling/stream/memory.h"
#include "sling/task/frames.h"
#include "sling/task/mapper.h"
#include "sling/task/reducer.h"
#include "sling/task/task.h"

namespace sling {

// Parse Companies House JSON message and output SLING companies and persons.
class CompaniesHouseMapper : public task::Mapper {
 public:
  ~CompaniesHouseMapper() override {
    delete commons_;
  }

  // Initialize Companies Hpuse importer.
  void Start(task::Task *task) override {
    Mapper::Start(task);

    // Initalize commons store.
    commons_ = new Store();
    names_.Bind(commons_);
    commons_->Freeze();

    // Statistics.
    num_companies_ = task->GetCounter("companies");
    num_officiers_ = task->GetCounter("officiers");
    num_pscs_ = task->GetCounter("pscs");
  }

  // Convert Companies House records from JSON to SLING.
  void Map(const task::MapInput &input) override {
    // Read Companies House records in JSON format into local SLING store.
    Store store(commons_);
    ArrayInputStream stream(input.value());
    Input in(&stream);
    Reader reader(&store, &in);
    reader.set_json(true);
    Object obj = reader.Read();
    CHECK(obj.valid());
    CHECK(obj.IsFrame()) << input.value();
  }

  // Task complete.
  void Done(task::Task *task) override {
    delete commons_;
    commons_ = nullptr;
    Mapper::Done(task);
  }

 private:
  // Commons store.
  Store *commons_ = nullptr;

  // Statistics.
  task::Counter *num_companies_ = nullptr;
  task::Counter *num_officiers_ = nullptr;
  task::Counter *num_pscs_ = nullptr;

  // Symbols.
  Names names_;
  Name n_name_{names_, "name"};

  Name s_company_name_{names_, "company_name"};
  Name s_company_number_{names_, "company_number"};
};

REGISTER_TASK_PROCESSOR("companies-house-mapper", CompaniesHouseMapper);

}  // namespace sling
