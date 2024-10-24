# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Simple logging."""

import inspect
import os
import sling.pysling as api

INFO = 0
WARNING = 1
ERROR = 2
FATAL = 3

def _log_message(severity, msg):
  caller = inspect.stack()[2]
  fn = os.path.basename(caller[1])
  line = caller[2]
  api.log_message(severity, fn, line, msg)

def _message(args):
  msgs = []
  for arg in args: msgs.append(str(arg))
  return ' '.join(msgs)

def info(*args):
  _log_message(INFO, _message(args))

def warning(*args):
  _log_message(WARNING, _message(args))

def error(*args):
  _log_message(ERROR, _message(args))

def fatal(*args):
  _log_message(FATAL, _message(args))

