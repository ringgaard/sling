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

#include <signal.h>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/db/dbserver.h"

using namespace sling;

DEFINE_string(addr, "", "HTTP server address");
DEFINE_int32(port, 7070, "HTTP server port");
DEFINE_string(dbdir, "db", "Database directory");
DEFINE_int32(workers, 16, "Number of network worker threads");
DEFINE_bool(recover, false, "Recover databases when loading");
DEFINE_bool(auto_mount, false, "Automatically mount databases in db dir");

// HTTP server.
HTTPServer *httpd = nullptr;

// Database service.
DBService *dbservice = nullptr;

// Termination handler.
void terminate(int signum) {
  VLOG(1) << "Shutdown requested";
  if (httpd != nullptr) httpd->Shutdown();
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Initialize database service.
  dbservice = new DBService(FLAGS_dbdir);

  // Mount databases.
  if (FLAGS_auto_mount) {
    std::vector<string> dbdirs;
    File::Match(FLAGS_dbdir + "/*", &dbdirs);
    for (const string &db : dbdirs) {
      string name = db.substr(FLAGS_dbdir.size() + 1);
      CHECK(dbservice->MountDatabase(name, db, FLAGS_recover));
    }
  }

  // Install signal handlers to handle termination.
  signal(SIGTERM, terminate);
  signal(SIGINT, terminate);

  // Start HTTP server.
  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  SocketServerOptions sockopts;
  sockopts.num_workers = FLAGS_workers;
  httpd = new HTTPServer(sockopts, FLAGS_addr.c_str(), FLAGS_port);
  dbservice->Register(httpd);
  CHECK(httpd->Start());
  LOG(INFO) << "Database server running";
  httpd->Wait();

  // Shut down.
  LOG(INFO) << "Shutting down HTTP server";
  delete httpd;
  httpd = nullptr;

  LOG(INFO) << "Shutting down database sever";
  delete dbservice;
  dbservice = nullptr;

  LOG(INFO) << "Done";
  return 0;
}

