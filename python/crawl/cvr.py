"""
Fetch the Danish Company Registry (CVR) and store the records in a database.
"""

import sys
import requests
import json
import sling
import sling.flags as flags

flags.define("--apikey",
             help="CVR API key file",
             default="local/keys/cvr.txt",
             metavar="FILE")

flags.define("--start",
             help="Start time for fetching CVR updates",
             default=None,
             metavar="YYYY-MM-DD")

flags.define("--end",
             help="End time for fetching CVR updates",
             default=None,
             metavar="YYYY-MM-DD")

flags.define("--cvrdb",
             help="database for storing CVR records",
             default="http://localhost:7070/cvr",
             metavar="DBURL")

flags.parse()

# Get API key.
with open(flags.arg.apikey) as f:
  apikey = f.read().strip().split(" ")
credentials = requests.auth.HTTPBasicAuth(apikey[0], apikey[1])
url = "http://distribution.virk.dk/"
dbsession = requests.Session()

# Set up query.
if flags.arg.start is None and flags.arg.end is None:
  query = {}
else:
  interval = {}
  if flags.arg.start != None: interval["gte"] = flags.arg.start
  if flags.arg.end != None: interval["lt"] = flags.arg.end
  query = {"query": {
    "bool": {
      "should": [
        {"range": {"Vrvirksomhed.sidstIndlaest": interval}},
        {"range": {"Vrdeltagerperson.sidstIndlaest": interval}},
        {"range": {"VrproduktionsEnhed.sidstIndlaest": interval}},
      ]
    }
  }}

# Execute query.
r = requests.post(url + "cvr-permanent/_search?scroll=1m",
                  auth=credentials,
                  json=query)
r.raise_for_status()

# Fetch records.
total_records = None
num_records = 0
num_updates = 0
while True:
  # Parse response.
  response = json.loads(r.text)
  if total_records == None: total_records = response["hits"]["total"]

  # Output records.
  hits = response["hits"]["hits"]
  if len(hits) == 0: break
  for hit in hits:
    for kind, rec in hit["_source"].items():
      if kind == "NewestRetrievedFileTimestamp": continue

      # The "enhedsNummer" field is used as the key and the "samtId" field
      # is used as the version number.
      cvrid = rec["enhedsNummer"]
      version = rec["samtId"]
      last_updated = rec["sidstIndlaest"]
      cvrnr = rec.get("cvrNummer")

      # Update company record in database. Only update record if the version
      # number is updated.
      r = dbsession.put(
        flags.arg.cvrdb + "/" + str(cvrid),
        json=rec,
        headers={
          "Version": str(version),
          "Mode": "newer",
        }
      )
      r.raise_for_status()
      result = r.headers["Result"]

      if result != "unchanged":
        print(num_records, "/", total_records,
              last_updated[:10], kind[2], cvrnr if cvrnr != None else cvrid,
              version, result)
        sys.stdout.flush()
        num_updates += 1

    num_records += 1

  # Fetch next batch.
  scroll_id = response.get("_scroll_id")
  if scroll_id == None: break
  r = requests.get(url + "/_search/scroll?scroll=1m",
                   auth=credentials,
                   json={"scroll": "1m", "scroll_id": scroll_id})
  r.raise_for_status()

print("Done,", num_records, "records,", num_updates, "updates.")

