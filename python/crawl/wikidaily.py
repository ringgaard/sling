import gzip
import requests
import sling
import sling.flags as flags
import xml.etree.ElementTree as ET

# https://dumps.wikimedia.org/other/incr/wikidatawiki/20200330/wikidatawiki-20200330-stubs-meta-hist-incr.xml.gz

flags.define("--file",
             help="daily stub xml file from wikidata",
             metavar="FILE")

flags.define("--date",
             help="date for daily stub xml file from wikidata",
             metavar="YYYYMMDD")

flags.define("--minrev",
             help="minimum revision id",
             default=0,
             type=int,
             metavar="NUM")

flags.define("--maxrev",
             help="maximum revision id",
             default=999999999999,
             type=int,
             metavar="NUM")

flags.define("--minqid",
             help="minimum qid id",
             default=0,
             type=int,
             metavar="NUM")

flags.define("--maxqid",
             help="maximum qid id",
             default=999999999999,
             type=int,
             metavar="NUM")

flags.define("--dailyurl",
             help="url for fetching daily incremental dump",
             default="https://dumps.wikimedia.org/other/incr/wikidatawiki/" +
                     "DATE/wikidatawiki-DATE-stubs-meta-hist-incr.xml.gz",
             metavar="URL")

flags.define("--wiki_fetch_url",
             help="url for fetching items from wikidata",
             default="https://www.wikidata.org/wiki/Special:EntityData",
             metavar="URL")

flags.define("--dburl",
             help="wiki database url for collecting changes",
             default="http://localhost:7070/wikilive",
             metavar="URL")

flags.define("--check_revision",
             help="check revision against database before fetching item",
             default=False,
             action="store_true")

flags.parse()

ns = {"wd": "http://www.mediawiki.org/xml/export-0.10/"}

def gettag(elem, tag):
  child = elem.find(tag, ns)
  if child is None: return None
  return child.text

# URL sessions for Wikidata and database.
dbsession = requests.Session()
wdsession = requests.Session()

# Commons store for Wikidata converter.
commons = sling.Store()
wikiconv = sling.WikiConverter(commons)
commons.freeze()

# Either open file or retrieve from url.
if flags.arg.file is not None:
  zipfile = gzip.open(flags.arg.file, "rb")
else:
  url = flags.arg.dailyurl.replace("DATE", flags.arg.date)
  r = requests.get(url, stream=True)
  zipfile = gzip.open(r.raw, "rb")

# Run over all pages in the daily stub dump.
tree = ET.parse(zipfile)
for page in tree.getroot().iterfind("wd:page", ns):
  # Get QID.
  qid = gettag(page, "wd:title")
  if not qid.startswith("Q"): continue

  # Check QID range
  idnum = int(qid[1:])
  if idnum < flags.arg.minqid: continue
  if idnum > flags.arg.maxqid: continue

  # Get latests revision (in range).
  revision = None
  for rev in page.iterfind("wd:revision", ns):
    revid = int(gettag(rev, "wd:id"))
    if revid < flags.arg.minrev: continue
    if revid > flags.arg.maxrev: continue
    if revision is not None and revision > revid: continue
    revision = revid
  if revision is None: continue

  # Check for redirect.
  redir = None
  redirect = page.find("wd:redirect", ns)
  if redirect is not None: redir = redirect.get("title")

  # Check current revision.
  if flags.arg.check_revision:
    r = dbsession.head(flags.arg.dburl + "/" + qid)
    if r.status_code == 204:
      currev = int(r.headers["Version"])
      if currev >= revision: continue

  # Fetch item.
  store = sling.Store(commons)
  item = None
  if redir != None:
    # Handle redirects by adding {=Q<old> +Q<new>} frames.
    item = store.parse("{id: %s +%s}" % (qid, redir))
  else:
    # Fetch item revision from Wikidata.
    url = "%s?id=%s&revision=%d&format=json" % (
      flags.arg.wiki_fetch_url, qid, revision)
    r = wdsession.get(url)
    if r.status_code == 404:
      print("%d %s NOT FOUND" % (revision, qid))
      continue

    # Convert item to SLING format.
    item, _ = wikiconv.convert_wikidata(store, r.text)

  # Save item in database.
  r = dbsession.put(
    flags.arg.dburl + "/" + qid,
    data=item.data(binary=True),
    headers={
      "Version": str(revision),
      "Mode": "ordered",
    }
  )
  r.raise_for_status()
  result = r.headers["Result"]

  if redir is None:
    print("%d %s %s (%s)" % (revision, item["id"], item["name"], result))
  else:
    print("%d %s REDIRECT %s" % (revision, qid, redir))

zipfile.close()

