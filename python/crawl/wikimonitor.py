import json
import re
import sys
import requests
import time
from threading import Thread
from queue import Queue
import sling
import sling.flags as flags
from sling.crawl.sse import SSEStream

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
             default="http://localhost:7070/wikidata",
             metavar="URL")

flags.define("--checkpoint",
             help="file with latest checkpoint",
             default=None,
             metavar="FILE")

flags.define("--checkpoint_interval",
             help="how often checkpoint is written to disk",
             default=1000,
             type=int,
             metavar="NUM")

flags.define("--threads",
             help="number of thread for worker pool",
             default=10,
             type=int,
             metavar="NUM")

flags.define("--qsize",
             help="queue size",
             default=1024,
             type=int,
             metavar="NUM")

flags.parse()

# Commons store for Wikidata converter.
commons = sling.Store()
wikiconv = sling.WikiConverter(commons)
commons.freeze()

# Global variables.
dbsession = requests.Session()
wdsession = requests.Session()
redir_pat = re.compile("\/\* wbcreateredirect:\d+\|\|(Q\d+)\|(Q\d+) \*\/")
num_changes = 0

# Fetch changed item and update database.
def process_change(change):
  qid = change["title"]
  if qid.startswith("Property:"): qid = qid[9:]
  ts = change["timestamp"]
  kind = change["type"]

  if kind == "log" and change["log_action"] == "delete":
    # Delete item/property from database.
    try:
      print("[%d] %s DELETE" % (queue.qsize(), qid))
      reply = dbsession.delete(flags.arg.dburl + "/" + qid)
      reply.raise_for_status()
    except Exception as e:
      print("DB delete error:", e)
  elif kind == "edit":
    revision = change["revision"]["new"]
    store = sling.Store(commons)
    item = None

    m = redir_pat.fullmatch(change["comment"])
    redir = None
    if m != None:
      # Handle redirects by adding {=Q<old> +Q<new>} frames.
      redir = m.group(2)
      item = store.parse("{id: %s +%s}" % (qid, redir))
    else:
      # Fetch item.
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
          reply.raise_for_status()
        except Exception as e:
          print("Error fetching item:", e, ":", change)
          return

      # Convert item to SLING format.
      try:
        item, _ = wikiconv.convert_wikidata(store, reply.text)
      except Exception as e:
        print("Error converting item:", e, reply.text)
        return

    # Save item in database.
    saved = False
    while not saved:
      try:
        reply = None
        reply = dbsession.put(
          flags.arg.dburl + "/" + qid,
          data=item.data(binary=True),
          headers={
            "Version": str(revision),
            "Mode": "ordered",
          }
        )
        reply.raise_for_status()
        result = reply.headers["Result"]
        saved = True
      except Exception as e:
        print("DB error:", e, ":", reply.text if reply != None else "")
        time.sleep(30)

    if redir:
      print("[%d] %s REDIR %s" % (queue.qsize(), qid, redir))
    else:
      print("[%d] %d %s %s (%s)" % (queue.qsize(), revision, item["id"],
            item["name"], result))

  # Update checkpoint.
  global num_changes
  num_changes += 1
  if flags.arg.checkpoint != None:
    if num_changes % flags.arg.checkpoint_interval == 0:
      dt = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ts))
      print("CHECKPOINT", ts, dt)
      with open(flags.arg.checkpoint, 'w') as ckpt:
        ckpt.write(str(ts))

  sys.stdout.flush()

# Thread worker pool for process changes in the background.
queue = Queue(flags.arg.qsize)

def worker():
  while True:
    event = queue.get()
    try:
      process_change(event)
    except:
      print("Error processing event:", sys.exc_info()[0])
    finally:
      queue.task_done()

for i in range(flags.arg.threads):
  t = Thread(target=worker)
  t.daemon = True
  t.start()

# Determine checkpoint for restart.
since = flags.arg.since
if since is None and flags.arg.checkpoint is not None:
  try:
    with open(flags.arg.checkpoint, 'r') as ckpt:
      ts = int(ckpt.read())
      since = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ts))
  except:
    print("No checkpoint file:", flags.arg.checkpoint)

if since:
  print("Restart at", since)

# Event listener for receiving Wikidata updates.
stream = SSEStream(flags.arg.wiki_changes_stream, since=since)
while True:
  try:
    for event in stream:
      if event.event != b"message": continue
      if event.data is None: continue
      if b"wikidatawiki" not in event.data: continue

      try:
        change = json.loads(event.data.decode("utf8"))
      except Exception as e:
        # Ignore JSON parse errors.
        print("JSON error: ", e)
        continue

      # Filter messages.
      wiki = change["wiki"]
      if wiki != "wikidatawiki": continue
      title = change["title"]
      if not (title.startswith("Q") or title.startswith("Property:")): continue

      # Add event to queue.
      queue.put(change)

  except Exception as e:
    print("SSE error:", e)

