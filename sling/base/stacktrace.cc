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

#include "sling/base/stacktrace.h"

#include <execinfo.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "sling/base/symbolize.h"

namespace sling {

// Maximum number of stack frames in stack trace.
static const int MAX_STACK_FRAMES = 32;

// This structure mirrors the one found in /usr/include/asm/ucontext.h.
struct sig_ucontext_t {
  unsigned long uc_flags;
  ucontext_t *uc_link;
  stack_t uc_stack;
  sigcontext uc_mcontext;
  sigset_t uc_sigmask;
};

// Output string.
static int OutputString(int fd, const char *str) {
  return write(fd, str, strlen(str));
}

// Output number.
static void OutputNumber(int fd, uint64 number) {
  char str[32];
  char *p = str + sizeof(str) - 1;
  *p = 0;
  while (number > 0) {
    uint64 digit = number % 10;
    *--p = digit + '0';
    number /= 10;
  }
  OutputString(fd, p);
}

// Output number in hex format.
static void OutputHex(int fd, uint64 number, int width = 1) {
  char str[32];
  char *p = str + sizeof(str);
  *--p = 0;
  while (number > 0 || width > 0) {
    uint64 digit = number & 0xF;
    if (digit < 10) {
      *--p = digit + '0';
    } else {
      *--p = digit + 'a' - 10;
    }
    number >>= 4;
    width--;
  }
  *--p = 'x';
  *--p = '0';
  OutputString(fd, p);
}

// Output address in hex format.
static void OutputAddress(int fd, const void *address) {
  OutputHex(fd, reinterpret_cast<uint64>(address), 12);
}

void DumpStackTrace(int fd, void *address) {
  // Get stack addresses for backtrace.
  void *stack[MAX_STACK_FRAMES];
  int num_frames = backtrace(stack, MAX_STACK_FRAMES);

  // Find first frame to output.
  int first_frame = 1;
  if (address != nullptr) {
    for (int i = 1; i < num_frames; ++i) {
      if (stack[i] == address) {
        first_frame = i;
        break;
      }
    }
  }

  // Output symbolic names for each stack frame.
  Symbolizer symbolizer;
  for (int i = first_frame; i < num_frames; ++i) {
    // Try to get symbolic name for address.
    Symbolizer::Location loc;
    symbolizer.FindSymbol(stack[i], &loc);

    // Output stack frame.
    OutputString(fd, "  @ ");
    OutputAddress(fd, loc.address);
    OutputString(fd, " ");
    if (loc.symbol != nullptr) {
      OutputString(fd, loc.symbol);
      if (loc.offset != 0) {
        OutputString(fd, "+");
        OutputHex(fd, loc.offset);
      }
    } else if (loc.file != nullptr) {
      OutputString(fd, loc.file);
      if (loc.offset != 0) {
        OutputString(fd, "+");
        OutputHex(fd, loc.offset);
      }
    } else {
      OutputString(fd, "(unknown)");
    }
    OutputString(fd, "\n");
  }
}

static void FailureSignalHandler(int signum, siginfo_t *info, void *ucontext) {
  // Get the address at the time the signal was raised.
  sig_ucontext_t *uc = reinterpret_cast<sig_ucontext_t *>(ucontext);
  void *caller = reinterpret_cast<void *>(uc->uc_mcontext.rip);

  // Output signal report.
  int fd = STDERR_FILENO;
  OutputString(fd, "*** Signal ");
  OutputNumber(fd, signum);
  OutputString(fd, " (");
  OutputString(fd, strsignal(signum));
  OutputString(fd, ") at ");
  OutputAddress(fd, caller);
  if (info->si_addr != 0) {
    OutputString(fd, " for ");
    OutputAddress(fd, info->si_addr);
  }
  OutputString(fd, "\n");

  // Dump stack trace.
  DumpStackTrace(fd, caller);

  // Raise to default signal handler.
  signal(signum, SIG_DFL);
  raise(signum);
}

static void InstallSignalHandler(
    int signum,
    void (*handler)(int, siginfo_t *, void *)) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  sa.sa_flags |= SA_SIGINFO | SA_NODEFER;
  sa.sa_sigaction = handler;
  sigaction(signum, &sa, nullptr);
}

void InstallFailureSignalHandlers() {
  InstallSignalHandler(SIGSEGV, FailureSignalHandler);
  InstallSignalHandler(SIGILL, FailureSignalHandler);
  InstallSignalHandler(SIGFPE, FailureSignalHandler);
  InstallSignalHandler(SIGABRT, FailureSignalHandler);
  InstallSignalHandler(SIGBUS, FailureSignalHandler);
  InstallSignalHandler(SIGTRAP, FailureSignalHandler);
}

}  // namespace sling

