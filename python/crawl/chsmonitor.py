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
             default="chs",
             metavar="DB")

flags.define("--checkpoint",
             help="File with latest checkpoint",
             default=None,
             metavar="FILE")

flags.define("--checkpoint_interval",
             help="How often checkpoint is written to disk (seconds)",
             default=60,
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

flags.define("--confirmations",
             help="Only update when confirmation date is changed",
             default=False,
             action="store_true")

flags.parse()

chs.init(flags.arg.chskeys)
chsdb = sling.Database(flags.arg.chsdb)
num_changes = 0
checkpoint = None

# Determine timepoint for restart.
timepoint = flags.arg.timepoint
if timepoint is None and flags.arg.checkpoint is not None:
  try:
    with open(flags.arg.checkpoint, 'r') as ckpt:
      timepoint = int(ckpt.read())
  except:
    print("No checkpoint file:", flags.arg.checkpoint)

# Convert date from YYYY-MM-DD to SLING format.
def get_date(s):
  if len(s) == 0: return None
  year = int(s[0:4])
  month = int(s[5:7])
  day = int(s[8:10])
  return year * 10000 + month * 100 + day

# Get confirmation date for company.
def get_confirmation(company):
  if company is None: return None
  if "confirmation_statement" not in company: return None
  confstmt = company["confirmation_statement"]
  if "last_made_up_to" not in confstmt: return None
  return get_date(confstmt["last_made_up_to"])

# Look up company in database.
def lookup_company(company_no):
  data = chsdb[company_no]
  if data is None: return None
  return json.loads(data)

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

  # Check if confirmation date has changed in confirmation mode.
  skip = False
  if flags.arg.confirmations:
    current = lookup_company(company_no)
    latest_confirmation = get_confirmation(company)
    current_confirmation = get_confirmation(current)
    if latest_confirmation != None and current_confirmation != None:
      if current_confirmation >= latest_confirmation:
        skip = True
        result = "skip"

  # Fetch company profile from Companies House.
  if not skip:
    # Fetch officers and owners.
    chs.retrieve_officers(company)
    chs.retrieve_owners(company)

    # Write company record to database.
    result = chsdb.put(company_no, json.dumps(company),
                       version=version, mode=sling.DBORDERED)

  print(timepoint, ts, company_no, company_name, result, chs.quota_left)
  sys.stdout.flush()

  global checkpoint
  checkpoint = timepoint

# Set up event queue.
def worker():
  while True:
    msg = queue.get()
    try:
      process_message(msg)
    except Exception as e:
      print("Error processing message:", msg)
      traceback.print_exc(file=sys.stdout)
    finally:
      queue.task_done()

queue = Queue(flags.arg.qsize)
t = Thread(target=worker)
t.daemon = True
t.start()

# Checkpoint thread.
def checkpointer():
  global checkpoint
  while True:
    if checkpoint != None:
      print("CHECKPOINT", checkpoint, "QUEUE:", queue.qsize())
      with open(flags.arg.checkpoint, 'w') as ckpt:
        ckpt.write(str(checkpoint))
        checkpoint = None
    sys.stdout.flush()
    time.sleep(flags.arg.checkpoint_interval)

if flags.arg.checkpoint != None:
  t = Thread(target=checkpointer)
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
    sys.stdout.flush()
    if flags.arg.confirmations:
      time.sleep(60)
    else:
      time.sleep(60 + queue.qsize())

  except Exception as e:
    print("Event error", type(e), ":", e)
    traceback.print_exc(file=sys.stdout)
    sys.stdout.flush()
    time.sleep(60)
