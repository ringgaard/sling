"""
Fetch the Companies House company registry using the streaming API.
"""

import json
import requests
import sling
import sling.flags as flags
import sling.crawl.chs as chs
import sys
import time
import traceback
from threading import Thread
from queue import Queue

flags.define("--chskeys",
             help="Companies House API key file",
             default="local/keys/chs.txt",
             metavar="FILE")

flags.define("--chsdb",
             help="database for storing Companies House records",
             default="http://localhost:7070/chs",
             metavar="DBURL")

flags.define("--checkpoint",
             help="File with latest checkpoint",
             default=None,
             metavar="FILE")

flags.define("--checkpoint_interval",
             help="How often checkpoint is written to disk",
             default=100,
             type=int,
             metavar="NUM")

flags.define("--timepoint",
             help="Retrieve events starting from a specific timepoint",
             default=None,
             type=int)

flags.define("--qsize",
             help="Event queue size",
             default=2000,
             type=int,
             metavar="NUM")

flags.parse()

chs.init(flags.arg.chskeys)
dbsession = requests.Session()
num_changes = 0

# Determine timepoint for restart.
timepoint = flags.arg.timepoint
if timepoint is None and flags.arg.checkpoint is not None:
  try:
    with open(flags.arg.checkpoint, 'r') as ckpt:
      timepoint = int(ckpt.read())
  except:
    print("No checkpoint file:", flags.arg.checkpoint)

# Event handler.
def process_message(msg):
  event = msg["event"]
  ts = event["published_at"]
  timepoint = int(event["timepoint"])

  if msg["resource_kind"] != "company-profile":
    print("***", json.dumps(msg, indent=2))
    return

  # Get company information.
  version = stream.timepoint
  company = msg["data"]
  company_no = company["company_number"]
  company_name = company["company_name"]

  # Fetch officers and owners.
  chs.retrieve_officers(company)
  chs.retrieve_owners(company)

  # Write company record to database.
  r = dbsession.put(
    flags.arg.chsdb + "/" + company_no,
    json=company,
    headers={
      "Version": str(version),
      "Mode": "ordered",
    }
  )
  r.raise_for_status()
  result = r.headers["Result"]
  print(timepoint, ts, company_no, company_name, result, chs.quota_left)

  # Update checkpoint.
  global num_changes
  num_changes += 1
  if flags.arg.checkpoint != None:
    if num_changes % flags.arg.checkpoint_interval == 0:
      print("CHECKPOINT", timepoint, "QUEUE:", queue.qsize())
      with open(flags.arg.checkpoint, 'w') as ckpt:
        ckpt.write(str(timepoint))

  sys.stdout.flush()

# Set up event queue.
def worker():
  while True:
    msg = queue.get()
    try:
      process_message(msg)
    finally:
      queue.task_done()

queue = Queue(flags.arg.qsize)
t = Thread(target=worker)
t.daemon = True
t.start()

# Receive events from Companies House streaming service.
stream = chs.CHSStream(timepoint)
while True:
  try:
    for msg in stream:
      queue.put(msg)

  except requests.exceptions.ChunkedEncodingError:
    print("Stream closed")
    time.sleep(60 + queue.qsize())

  except Exception as e:
    print("Event error", type(e), ":", e)
    traceback.print_exc(file=sys.stdout)
    time.sleep(60)

