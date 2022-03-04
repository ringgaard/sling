#!/usr/bin/python3

# Copyright 2022 Ringgaard Research ApS
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

"""SLING system notifier"""

import time
import subprocess
import json
import requests

notified = set()
sysmon_down = False

def notify(message):
  subprocess.run(["notify-send", "SLING alert", message])

while True:
  try:
    r = requests.get("http://master:8888/status")
    r.raise_for_status()
    if sysmon_down:
      notify("SLING sysmon up")
      sysmon_down = False
    status = r.json()
    for alert in status["alerts"]:
      ack = alert.get("ack")
      if ack is None: continue
      if ack in notified: continue
      message = alert["alert"].replace("<br>", "\n")
      notify(message)
      notified.add(ack)

  except KeyboardInterrupt:
    break

  except:
    if not sysmon_down:
      notify("SLING sysmon down")
      sysmon_down = True

  time.sleep(5 * 60)

