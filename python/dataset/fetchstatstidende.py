import requests
import json
import time
import datetime
import sys
import sling
import sling.flags as flags

flags.define("--db",
             default="statstidende",
             help="message database")

flags.define("--year",
             default=None,
             help="fetch messages for year",
             type=int)

flags.define("--month",
             default=None,
             help="fetch messages for month",
             type=int)

flags.define("--first",
             default=1,
             help="first day of month to fatch",
             type=int)

flags.define("--delay",
             default=1,
             help="delay in seconds between requests",
             type=int)

flags.parse()

db = sling.Database(flags.arg.db)

def get_field(fields, name):
  for field in fields:
    if field["name"] == name: return field["value"]

ua = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"

date = datetime.date(flags.arg.year, flags.arg.month, flags.arg.first)
num_fetched = 0
while date.month == flags.arg.month:
  print(date)

  page = 0
  pagecount = 1
  msgids = []
  while page < pagecount:
    url = "https://www.statstidende.dk/api/messagesearch?fromDate=%sT00:00:00&toDate=%sT00:00:00&ps=100&page=%d&m=431a79afdf1f5c4ba7a4a20aadda8c0a" % (str(date), str(date), page)
    print(page, pagecount, url)
    r = requests.get(url, headers={"User-Agent": ua})
    r.raise_for_status()
    data = r.json();

    results = data["results"]
    for result in results:
      msgid = result["messageNumber"]
      summary = result["summary"]
      name = result["title"]
      cprnr = get_field(summary, "CPR-nr.")
      dod = get_field(summary, "Dødsdato")
      print(msgid, name, cprnr, dod)
      msgids.append(msgid)

    pagecount = data["pageCount"]
    page += 1

  n = 0
  for msgid in msgids:
    n += 1
    if msgid in db:
      print("skip", msgid)
      continue

    time.sleep(flags.arg.delay)

    url = "https://www.statstidende.dk/api/message/" +  msgid
    try:
      r = requests.get(url, headers={"User-Agent": ua}, timeout=60)
      r.raise_for_status()
    except requests.exceptions.ReadTimeout:
      print("timeout", msgid)
      continue

    data = r.json();
    document = json.loads(data["document"])
    data["document"] = document
    del data["document"]
    for k, v in document.items(): data[k] = v
    outcome = db.put(msgid, json.dumps(data), mode=sling.DBADD)

    print("%d/%d" % (n, len(msgids)), msgid, outcome)
    sys.stdout.flush()
    num_fetched += 1

  date = date + datetime.timedelta(days=1)

db.close()
print(num_fetched, "fetched")
