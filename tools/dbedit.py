#!/usr/bin/python3
# Copyright 2021 Ringgaard Research ApS
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

"""Edit records in SLINGDB."""

import os
import sys
import tempfile
import sling
import sling.flags as flags

flags.define("--db",
             help="database to edit",
             default=None,
             metavar="DB")

flags.define("--id",
             default=None,
             help="Record key for record to edit",
             metavar="KEY")

flags.define("--add",
             help="Add new record to database",
             default=False,
             action="store_true")

flags.define("--raw",
             help="Edit raw record value",
             default=False,
             action="store_true")

flags.define("--delete",
             help="Delete record from database",
             default=False,
             action="store_true")

flags.define("--move",
             help="Move content from this record",
             default=None,
             metavar="KEY")

flags.define("--append",
             help="Append frame to record",
             default=None,
             metavar="FRAME")

flags.parse()

# Check arguments.
if flags.arg.db is None:
  print("No database, specify with --db")
  sys.exit(1)

if flags.arg.id is None:
  print("No database key, specify with --id")
  sys.exit(1)

store = sling.Store()
db = sling.Database(flags.arg.db, "dbedit")

# Read record.
if flags.arg.add:
  if flags.arg.id in db:
    print("Record already in database")
    sys.exit(1)
  content = ""
  oldcontent = None
elif flags.arg.delete:
  del db[flags.arg.id]
  print("Record deleted");
  sys.exit(0)
elif flags.arg.move:
  if flags.arg.id in db:
    print("Record", flags.arg.id, "already exists")
  elif flags.arg.move not in db:
    print("Record", flags.arg.move, "does not exist")
  else:
    db[flags.arg.id] = db[flags.arg.move]
    del db[flags.arg.move]
    print("Record moved");
  sys.exit(0)
else:
  content = db[flags.arg.id]
  if content is None:
    print("Record not found")
    sys.exit(1)
  oldcontent = content
  if not flags.arg.raw:
    frame = store.parse(content)
    if flags.arg.append:
      for name, value in store.parse(flags.arg.append):
       frame.append(name, value)
    content = frame.data(pretty=True, utf8=True).encode()

# Write record to temp file.
fd, fn = tempfile.mkstemp()
f = os.fdopen(fd, "wb")
f.write(content)
f.close()

# Edit record.
editor = os.environ.get("EDITOR", "nano")
os.system("%s %s" % (editor, fn))

# Read edited record content.
f = open(fn, "rb")
newcontent = f.read()
f.close()
os.remove(fn)
if len(newcontent) == 0:
  print("Empty, no update")
  sys.exit(1)

if not flags.arg.raw:
  frame = store.parse(newcontent)
  store.coalesce()
  newcontent = frame.data(binary=True)

# Update database.
if newcontent == oldcontent:
  print("No changes")
elif flags.arg.add:
  db.add(flags.arg.id, newcontent)
  print("Record %s added" % flags.arg.id)
else:
  db.put(flags.arg.id, newcontent)
  print("Record %s updated" % flags.arg.id)

db.close()
