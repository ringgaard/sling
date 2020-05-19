import json
import requests
import sys
import traceback
import xml.etree.ElementTree as et
import sling.crawl.news as news
import sling.flags as flags

flags.define("--feeds",
             default="data/crawl/rss-en.txt",
             help="file with list of RSS feeds")

flags.parse()

def get_rss_element(e, tag):
  child = e.find(tag)
  if child == None:
    child = e.find("{http://purl.org/rss/1.0/}" + tag)
  if child == None: return ""
  text = child.text
  if text == None: return ""
  return text.strip().replace("\n", " ")

def get_atom_element(e, tag):
  child = e.find(tag)
  if child == None:
    child = e.find("{http://www.w3.org/2005/Atom}" + tag)
  if child == None: return ""
  text = child.text
  if text == None: return ""
  return text.strip().replace("\n", " ")

# Initialize news crawler.
crawler = news.Crawler("rss")

# Read RSS news feeds.
feeds = {}
f = open(flags.arg.feeds, "r")
rsssession = requests.Session()
for line in f.readlines():
  line = line.strip()
  if len(line) == 0 or line[0] == "#": continue
  fields = line.split(" ")
  site = fields[0]
  rss = fields[1]
  print("=== RSS feed", rss)

  # Fetch RSS feed.
  try:
    r = requests.get(rss, timeout=10)
    r.raise_for_status()
  except Exception as e:
    print("*** RSS feed error:", rss, e)
    continue

  # Parse RSS feed.
  try:
    root = et.fromstring(r.content)
  except Exception as e:
    print("*** RSS XML error:", rss, e)
    continue

  # Get RSS channels and items.
  channels = []
  items = []
  for channel in root.iter("channel"):
    channels.append(channel)
  for channel in root.iter("{http://purl.org/rss/1.0/}channel"):
    channels.append(channel)

  for channel in channels:
    name = get_rss_element(channel, "title")
    for item in channel.iter("item"):
      items.append(item)
    for item in channel.iter("{http://purl.org/rss/1.0/}item"):
      items.append(item)

  for item in root.iter("item"):
    items.append(item)
  for item in root.iter("{http://purl.org/rss/1.0/}item"):
    items.append(item)

  # Get Atom entries.
  for entry in root.iter("entry"):
    items.append(entry)
  for entry in root.iter("{http://www.w3.org/2005/Atom}entry"):
    items.append(entry)

  # Crawl items.
  for item in items:
    url = get_rss_element(item, "link")
    if len(url) == 0:
      url = get_atom_element(item, "link")
    if not url.startswith("http://") and not url.startswith("https://"):
      url = "http://" + site + "/" + url
    url = url.strip()
    if len(url) == 0: continue

    crawler.crawl(url)

f.close()
crawler.wait()
crawler.dumpstats()

