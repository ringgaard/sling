import datetime
import requests
import sys
import collections
import xml.etree.ElementTree as ET
import sling.flags as flags
import sling.crawl.dnscache
import sling.crawl.news as news

flags.define("--daily",
             default=False,
             action="store_true",
             help="fetch daily news feed from newslookup.com")

flags.define("--hourly",
             default=False,
             action="store_true",
             help="fetch hourly news feed from newslookup.com")

flags.define("--newsites",
             default=False,
             action="store_true",
             help="output new unknown news sites")

flags.define("--file",
             default=None,
             help="fetch news articles from newslookup file")

flags.define("--backupdir",
             default=None,
             help="backup directory for newslookup files")

flags.parse()

# Intialize.
news.init_cookies()

# Read news feed.
if flags.arg.daily:
  r = requests.get("http://downloads.newslookup.com/daily.xml")
  r.raise_for_status()
  feed = r.content
elif flags.arg.hourly:
  r = requests.get("http://downloads.newslookup.com/hourly_all.xml")
  r.raise_for_status()
  feed = r.content
elif flags.arg.file:
  f = open(flags.arg.file, "rb")
  feed = f.read()
  f.close()
else:
  print("No input specified, use --daily, --hourly or --file FILE")
  sys.exit(1)

if len(feed) == 0:
  print("Empty news feed");
  sys.exit(1)

# Back up news feed to disk.
if flags.arg.backupdir:
  if flags.arg.hourly:
    now = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M")
    backup_fn = flags.arg.backupdir + "/hourly-" + now + ".xml"
  else:
    now = str(datetime.date.today())
    backup_fn = flags.arg.backupdir + "/daily-" + now + ".xml"
  f = open(backup_fn, "wb")
  f.write(feed)
  f.close()

# Fix errors.
if b" & " in feed:
  feed.replace(b" & ", b" &amp; ")
if b"<response>" in feed and b"</response>" not in feed:
  feed += b"</response>"

# Parse XML news feed.
try:
  root = ET.fromstring(feed)
except Exception as e:
  print("*** XML parse error:", e, "in parsing news feed")
  sys.exit(1)

if flags.arg.newsites:
  # Check for unknown news sites.
  news.init()
  newsites = collections.defaultdict(int)
  for item in root.iter("item"):
    child = item.find("link")
    if child is None: continue
    url = child.text
    if url == "https://newslookup.com/": continue
    site = news.sitename(url)
    if site not in news.sites: newsites[site] += 1
  for site in sorted(newsites, key=newsites.get, reverse=True):
    print(newsites[site], site)
else:
  # Fetch articles.
  crawler = news.Crawler("newslookup")
  for item in root.iter("item"):
    child = item.find("link")
    if child is None: continue
    url = child.text
    if url == "https://newslookup.com/": continue
    crawler.crawl(url)

  crawler.wait()
  crawler.dumpstats()

