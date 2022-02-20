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

#include "sling/base/init.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/base/stacktrace.h"
#include "sling/base/types.h"

DEFINE_string(pidfile, "", "PID file for identifying running daemon process");

namespace sling {

// Linked list of module initializers.
ModuleInitializer *ModuleInitializer::first = nullptr;
ModuleInitializer *ModuleInitializer::last = nullptr;

ModuleInitializer::ModuleInitializer(const char *n, Handler h)
    : name(n), handler(h) {
  if (first == nullptr) first = this;
  if (last != nullptr) last->next = this;
  last = this;
}

static void RunModuleInitializers() {
  ModuleInitializer *initializer = ModuleInitializer::first;
  while (initializer != nullptr) {
    VLOG(2) << "Initializing " << initializer->name << " module";
    initializer->handler();
    initializer = initializer->next;
  }
}

void InitProgram(int *argc, char ***argv) {
  // Install failure signal handlers.
  InstallFailureSignalHandlers();

  // Initialize command line flags.
  if (*argc > 0) {
    string usage;
    usage.append((*argv)[0]);
    usage.append(" [OPTIONS]\n");
    Flag::SetUsageMessage(usage);
    if (Flag::ParseCommandLineFlags(argc, *argv) != 0) exit(1);
  }

  // Write PID file if requested.
  CreatePIDFile();

  // Run module initializers.
  RunModuleInitializers();
}

void InitSharedLibrary() {
  // Install failure signal handlers.
  InstallFailureSignalHandlers();

  // Run module initializers.
  RunModuleInitializers();
}

int CreatePIDFile() {
  // Only create PID file if requested.
  if (FLAGS_pidfile.empty()) return 0;

  // Create PID file.
  const char *pidfn = FLAGS_pidfile.c_str();
  int fd = open(pidfn, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd == -1) {
    LOG(ERROR) << "Could not create PID file: " << FLAGS_pidfile;
    return -1;
  }

  // Truncate PID file to erase any existing content.
  if (ftruncate(fd, 0) == -1) {
    LOG(ERROR) << "Could not truncate PID file: " << FLAGS_pidfile;
    return -1;
  }

  // Write PID to file.
  char buf[32];
  snprintf(buf, sizeof(buf), "%ld\n", (long) getpid());
  if (write(fd, buf, strlen(buf)) != strlen(buf)) {
    LOG(ERROR) << "Error wrting to PID file: " << FLAGS_pidfile;
    return -1;
  }

  return fd;
}

}  // namespace sling

