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

"""Find IP adresses of bots masquerading as regular users."""

import gzip
import re
import datetime
import urllib.parse
from collections import defaultdict
import sling.flags as flags

flags.define("logfiles",
             nargs="*",
             help="NCSA log files in NCSA Combined format",
             metavar="FILE")

flags.parse()

ncsa_log_pattern = re.compile(
  r"(\d+\.\d+\.\d+\.\d+) - - "  # 1: IP address
  r"\[(.*?)\] "                 # 2: timestamp
  r"\"([A-Z]+) "                # 3: method
  r"(.*?) "                     # 4: path
  r"(.*?)\" "                   # 5: protocol
  r"(\d+) "                     # 6: HTTP status code
  r"(\d+) "                     # 7: size
  r"\"(.*?)\" "                 # 8: referrer
  r"\"(.*?)\""                  # 9: user agent
)

kb_pattern = re.compile(r"\/kb\/[PQt]")
item_pattern = re.compile(r"\/kb\/item\?fmt=cjson\&id=(.+)")

kb_ips = set()
item_ips = set()

for logfn in flags.arg.logfiles:
  if logfn.endswith(".gz"):
    logfile = gzip.open(logfn, "rt")
  else:
    logfile = open(logfn, "rt")
  for logline in logfile:
    # Parse log line.
    m = ncsa_log_pattern.match(logline)
    if m is None: continue

    # Get fields.
    ipaddr = m.group(1)
    timestamp = m.group(2)
    method = m.group(3)
    path = m.group(4)
    protocol = m.group(5)
    status = m.group(6)
    bytes = m.group(7)
    referrer = m.group(8)
    ua = m.group(9)
    ts = datetime.datetime.strptime(timestamp, "%d/%b/%Y:%H:%M:%S %z")
    day = ts.date()

    # Internal traffic.
    if ipaddr.startswith("10.1.") or ipaddr.startswith("127."): continue

    # Bots.
    if "bot" in ua: continue;

    # Check for cold and warm KB hits.
    if kb_pattern.match(path):
      kb_ips.add(ipaddr);
    elif item_pattern.match(path):
      item_ips.add(ipaddr);

  logfile.close()

for ipaddr in kb_ips:
  if ipaddr not in item_ips:
    print(ipaddr)
