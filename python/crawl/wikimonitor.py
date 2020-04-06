import json
import re
import sys
import requests
import time
from sseclient import SSEClient
from threading import Thread
from queue import Queue
import sling
import sling.flags as flags

flags.define("--wiki_changes_stream",
             help="stream for monitoring updates to wikidata",
             default="https://stream.wikimedia.org/v2/stream/recentchange",
             metavar="URL")

flags.define("--since",
             help="retrieve event starting from a specific time",
             default=None,
             metavar="YYYY-MM-DDThh:mm:ssZ")

flags.define("--wiki_fetch_url",
             help="url for fetching items from wikidata",
             default="https://www.wikidata.org/wiki/Special:EntityData",
             metavar="URL")

flags.define("--dburl",
             help="wiki database url for collecting changes",
             default="http://compute02.jbox.dk:7070/wikilive",
             metavar="URL")

flags.define("--checkpoints",
             help="log file for checkpoint records",
             default="",
             metavar="FILE")

flags.define("--threads",
             help="number of thread for worker pool",
             default=10,
             type=int,
             metavar="NUM")

flags.define("--qsize",
             help="queue size",
             default=1024 * 1024,
             type=int,
             metavar="NUM")

flags.parse()

# Commons store for Wikidata converter.
commons = sling.Store()
wikiconv = sling.WikiConverter(commons)
commons.freeze()

# URL sessions for Wikidata and database.
dbsession = requests.Session()
wdsession = requests.Session()

# Fetch changed item and update database.
def process_change(edit):
  # Get event data.
  qid = str(edit[0])
  revision = edit[1]
  redir = edit[2]

  # Fetch item.
  store = sling.Store(commons)
  item = None
  if redir != None:
    # Handle redirects by adding {=Q<old> +Q<new>} frames.
    item = store.parse("{id: %s +%s}" % (qid, redir))
  else:
    again = True
    while again:
      again = False
      try:
        # Fetch item revision from Wikidata.
        url = "%s?id=%s&revision=%d&format=json" % (
          flags.arg.wiki_fetch_url, qid, revision)
        reply = wdsession.get(url)
        if reply.status_code == 429:
          # Too many requests.
          print("throttle down...")
          time.sleep(30)
          again = True;
      except Exception as e:
        print("Error fetching item:", e, ":", edit[3])
        return

    try:
      # Convert item to SLING format.
      item, _ = wikiconv.convert_wikidata(store, reply.text)
    except Exception as e:
      print("Error converting item:", e, "status", reply.status_code, ":", reply.text)
      return

  # Save item in database.
  saved = False
  while not saved:
    try:
      db_reply = None
      db_reply = dbsession.put(
        flags.arg.dburl + "/" + qid,
        data=item.data(binary=True),
        headers={
          "Version": str(revision),
          "Mode": "ordered",
        }
      )
      db_reply.raise_for_status()
      outcome = db_reply.headers["Outcome"]
      saved = True
    except Exception as e:
      print("DB error:", e, ":", db_reply.text if db_reply != None else "")
      time.sleep(30)

  if redir != None:
    print("[%d] %d %s REDIRECT %s" % (queue.qsize(), revision, qid, redir))
  else:
    print("[%d] %d %s %s (%s)" % (queue.qsize(), revision,
                                  item["id"], item["name"], outcome))
  sys.stdout.flush()

# Thread worker pool for process changes in the background.
queue = Queue(flags.arg.qsize)

def worker():
  while True:
    edit = queue.get()
    try:
      process_change(edit)
    finally:
      queue.task_done()

for i in range(flags.arg.threads):
  t = Thread(target=worker)
  t.daemon = True
  t.start()

# Get current date.
tm = time.gmtime(time.time())
day = tm.tm_mday

# Pattern for redirect events.
redir_pat = re.compile("\/\* wbcreateredirect:\d+\|\|(Q\d+)\|(Q\d+) \*\/")

# Event listener for receiving Wikidata updates.
while True:
  try:
    url = flags.arg.wiki_changes_stream
    if flags.arg.since:
      url += "?since=" + flags.arg.since
      flags.arg.since = None
    for event in SSEClient(url):
      if event.event != "message" or event.data == "": continue
      try:
        change = json.loads(event.data)
      except Exception as e:
        # Ignore JSON parse errors.
        continue

      # Filter messages.
      if change["wiki"] != "wikidatawiki": continue
      qid = change["title"]
      if qid[0] != "Q": continue
      if change["type"] == "log": continue
      ts = change["timestamp"]
      revision = change["revision"]["new"]

      # Output checkpoint when a new day starts.
      if flags.arg.checkpoints != "":
        tm = time.gmtime(ts)
        if tm.tm_mday != day:
          day = tm.tm_mday

          # Get current epoch from database server.
          epoch = dbsession.options(flags.arg.dburl).headers["Epoch"]

          # Add entry to checkpoint file.
          with open(flags.arg.checkpoints, 'a') as ckpt:
            ckpt.write("%04d-%02d-%02d %s\n" % (year, month, day, epoch))

      # Detect redirects.
      redir = None
      m = redir_pat.fullmatch(change["comment"])
      if m != None: redir = m.group(2)

      # Add update to queue.
      queue.put((qid, revision, redir, event.data))

  except Exception as e:
    print("Error receiving event:", e)

