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

#include <unistd.h>
#include <iostream>
#include <string>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/db/dbclient.h"
#include "sling/file/file.h"
#include "sling/file/recordio.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/document/lex.h"
#include "sling/string/printf.h"
#include "sling/util/fingerprint.h"

DEFINE_bool(keys, false, "Only output keys");
DEFINE_bool(values, false, "Only output values");
DEFINE_bool(filenames, false, "Output file names");
DEFINE_bool(store, false, "Input is a SLING store");
DEFINE_bool(raw, false, "Output raw record");
DEFINE_bool(json, false, "Input is JSON object");
DEFINE_bool(lex, false, "Record values as lex encoded documents");
DEFINE_string(key, "", "Only display records with matching key");
DEFINE_int32(indent, 2, "Indentation for structured data");
DEFINE_int32(limit, 0, "Maximum number of records to output");
DEFINE_int32(batch, 128, "Batch size for fetching records from database");
DEFINE_bool(utf8, true, "Allow UTF8-encoded output");
DEFINE_bool(db, false, "Read input from database");
DEFINE_bool(version, false, "Output record version");
DEFINE_bool(follow, false, "Incrementally fetch new changes");
DEFINE_bool(shallow, false, "Output shallow frames");
DEFINE_int32(poll, 1000, "Poll interval (in ms) for incremental fetching");
DEFINE_string(field, "", "Only display a single field from frame");
DEFINE_bool(timestamp, false, "Output version as timestamp");
DEFINE_bool(position, false, "Output file position");
DEFINE_string(recout, "", "Output to record file");
DEFINE_string(dbout, "", "Output to database");
DEFINE_bool(bulk, false, "Database bulk loading");
DEFINE_bool(stream, false, "Fetch database records using stream");

using namespace sling;
using namespace sling::nlp;

RecordWriter *recout = nullptr;
DBClient *dbout = nullptr;
int records_output = 0;

void DisplayObject(const Object &object) {
  if (FLAGS_lex && object.IsFrame()) {
    Document document(object.AsFrame());
    std::cout << ToLex(document);
  } else {
    StringPrinter printer(object.store());
    printer.printer()->set_indent(FLAGS_indent);
    printer.printer()->set_shallow(FLAGS_shallow);
    printer.printer()->set_utf8(FLAGS_utf8);
    if (!FLAGS_field.empty() && object.IsFrame()) {
      Frame frame = object.AsFrame();
      Handle value = frame.GetHandle(FLAGS_field);
      printer.Print(value);
    } else {
      printer.Print(object);
    }
    std::cout << printer.text();
  }
}

void DisplayObject(const Slice &value) {
  Store store;
  Text encoded(value.data(), value.size());
  if (FLAGS_json) {
    StringReader reader(&store, encoded);
    reader.reader()->set_json(true);
    DisplayObject(reader.ReadAll());
  } else {
    StringDecoder decoder(&store, encoded);
    DisplayObject(decoder.DecodeAll());
  }
}

void DisplayRaw(const Slice &value) {
  std::cout.write(value.data(), value.size());
}

void DisplayRecord(const Slice &key, uint64 version, const Slice &value) {
  if (recout != nullptr) {
    // Output to record file.
    CHECK(recout->Write(key, version, value));
  } else if (dbout != nullptr) {
    DBRecord rec(key, value);
    rec.version = version;
    CHECK(dbout->Put(&rec));
  } else {
    // Display key.
    if (!FLAGS_values) {
      DisplayRaw(key);
    }

    // Display version.
    if (FLAGS_version && version != 0) {
      if (FLAGS_timestamp) {
        char datebuf[32];
        time_t time = version;
        strftime(datebuf, sizeof(datebuf), "%FT%TZ", gmtime(&time));
        std::cout << " [" << datebuf << "]";
      } else {
        std::cout << " [" << version << "]";
      }
    }

    // Display value.
    if (!FLAGS_keys) {
      if (!FLAGS_values && !key.empty()) std::cout << ": ";
      if (FLAGS_raw) {
        DisplayRaw(value);
      } else {
        DisplayObject(value);
      }
    }

    std::cout << "\n";
  }
  records_output++;
}

void DisplayStore(const string &filename) {
  Store store;
  FileInputStream stream(filename);
  InputParser parser(&store, &stream);
  while (!parser.done()) {
    DisplayObject(parser.Read());
  }
}

void DisplayDatabase(const string &filename) {
  DBClient db;
  CHECK(db.Connect(filename, "codex"));
  if (FLAGS_key.empty()) {
    DBIterator iterator;
    iterator.batch = FLAGS_batch;
    iterator.novalue = FLAGS_keys;
    if (FLAGS_follow) CHECK(db.Epoch(&iterator.position));
    if (FLAGS_stream) {
      Status st = db.Stream(&iterator, [](const DBRecord &record) {
        DisplayRecord(record.key, record.version, record.value);
        return Status::OK;
      });
    } else {
      std::vector<DBRecord> records;
      for (;;) {
        Status st = db.Next(&iterator, &records);
        if (!st.ok()) {
          if (st.code() == ENOENT) {
            if (!FLAGS_follow) break;
            usleep(FLAGS_poll * 1000);
          } else {
            LOG(FATAL) << "Error reading from database "
                       << filename << ": " << st;
          }
        }
        for (auto &record : records) {
          DisplayRecord(record.key, record.version, record.value);
        }
      }
    }
  } else {
    DBRecord record;
    CHECK(db.Get(FLAGS_key, &record));
    if (!record.value.empty()) {
      DisplayRecord(record.key, record.version, record.value);
    }
  }
  CHECK(db.Close());
}

void DisplayRecordDatabase(const string &filename) {
  RecordFileOptions options;
  RecordDatabase db(filename, options);
  Record record;
  if (db.Lookup(FLAGS_key, &record)) {
    // Optionally output position.
    if (FLAGS_position) std::cout << "@" << record.position << " ";

    // Display record.
    DisplayRecord(record.key, record.version, record.value);
  }
}

void DisplayRecordFile(const string &filename) {
  RecordReader reader(filename);
  while (!reader.Done()) {
    // Read next record.
    Record record;
    CHECK(reader.Read(&record));

    // Check for key match.
    if (!FLAGS_key.empty() && record.key != FLAGS_key) continue;

    // Optionally output position.
    if (FLAGS_position) std::cout << "@" << record.position << " ";

    // Display record.
    DisplayRecord(record.key, record.version, record.value);

    // Check record limit.
    if (FLAGS_limit > 0 && records_output >= FLAGS_limit) break;
  }
  CHECK(reader.Close());
}

void DisplayFile(const string &filename) {
  if (FLAGS_filenames) std::cout << "File " << filename << ":\n";
  if (FLAGS_store) {
    DisplayStore(filename);
  } else if (FLAGS_db) {
    DisplayDatabase(filename);
  } else if (!FLAGS_key.empty()) {
    DisplayRecordDatabase(filename);
  } else {
    DisplayRecordFile(filename);
  }
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);
  if (argc < 2) {
    std::cerr << argv[0] << " [OPTIONS] [FILE] ...\n";
    return 1;
  }

  if (!FLAGS_recout.empty()) {
    recout = new RecordWriter(FLAGS_recout);
  } else if (!FLAGS_dbout.empty()) {
    dbout = new DBClient();
    CHECK(dbout->Connect(FLAGS_dbout, "codex"));
    if (FLAGS_bulk) dbout->Bulk(true);
  }

  std::vector<string> files;
  for (int i = 1; i < argc; ++i) {
    if (FLAGS_db) {
      files.push_back(argv[i]);
    } else {
      File::Match(argv[i], &files);
    }
  }
  if (files.empty()) {
    std::cerr << "No input files\n";
    return 1;
  }

  if (FLAGS_key.empty()) {
    for (const string &file : files) {
      DisplayFile(file);
      if (FLAGS_limit > 0 && records_output >= FLAGS_limit) break;
    }
  } else {
    uint64 fp = Fingerprint(FLAGS_key.data(), FLAGS_key.size());
    int shard = fp % files.size();
    DisplayFile(files[shard]);
  }

  if (recout != nullptr) {
    recout->Close();
    delete recout;
    std::cout << records_output << " records written to "
              << FLAGS_recout << "\n";
  } else if (dbout != nullptr) {
    if (FLAGS_bulk) dbout->Bulk(false);
    dbout->Close();
    delete dbout;
    std::cout << records_output << " records written to database "
              << FLAGS_dbout << "\n";
  }

  return 0;
}
