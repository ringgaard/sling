import calendar
import html
import re
import requests
import time
import urllib.parse
import sling
import sling.flags as flags

flags.define("--crawldb",
             help="database for crawled news articles",
             default="http://localhost:7070/crawl",
             metavar="URL")

# Blocked sites.
blocked_urls = set([
  "https://www.theaustralian.com.au/nocookies",
  "https://www.couriermail.com.au/nocookies",
  "http://www.couriermail.com.au/nocookies",
  "https://www.heraldsun.com.au/nocookies",
  "https://www.washingtonpost.com/gdpr-consent",
  "https://www.forbes.com/forbes/welcome",

  "https://www.news.com.au/video/",
  "https://video.foxnews.com/",
  "http://www.espn.com/video/",
  "https://www.ctvnews.ca/video",
  "http://www.espn.com/espnradio/",
  "https://uk.reuters.com/video/",
  "https://www.reuters.com/video/",
])

# Sites where the URL query is part of the unique identifier.
urls_with_query = set([
  "https?://abcnews.go.com/",
  "https?://www.nzherald.co.nz/",
  "https://sana.sy/",
  "http://koreajoongangdaily.joins.com/",
  "https://chicago.suntimes.com/",
  "https://www.okgazette.com/",
  "https://www.newsfactor.com/",
  "https://en.delfi.lt/",
  "https://www.japantimes.co.jp/",
  "https://www.espn.com/",
])

blocked = [url.replace("/", "\\/") for url in blocked_urls]
blocked_pat = re.compile("|".join(blocked))
urls_with_query_pat = re.compile("|".join(urls_with_query))

canonical_tag_pat = re.compile(b'<link [^>]*rel=\"canonical\"[^>]*>')
href_attr_pat = re.compile(b' href=\"([^\"]*)\"')
prefix_pat = re.compile('https?:\/\/[^\/]+\/?')

def trim_url(url):
  """Trim parts of news url that are not needed for uniqueness."""
  # Remove URL fragment.
  h = url.find("#")
  if h != -1: url = url[:h]

  # Remove query parameters unless there is an exception.
  h = url.find("?")
  if h != -1 and urls_with_query_pat.search(url) == None:
    url = url[:h]

  # Remove trailing /.
  if url.endswith("/"): url = url[:-1]

  # Remove trailing /amp.
  if url.endswith("/amp"): url = url[:-4]

  return url

def get_canonical_url(uri, page):
  """Get canonical url from page."""

  # Try to find <link rel="canonical" href="<url>">.
  m = canonical_tag_pat.search(page)
  if m is None: return None
  link = m.group(0)

  # Get href attribute.
  a = href_attr_pat.search(link)
  if a is None: return None
  url = a.group(1).decode("utf8")

  # Resolve relative URLs.
  if url.startswith("/"): url = urllib.parse.urljoin(uri, url)

  # Remove trailing ?.
  if url.endswith("?"): url = url[:-1]

  # Remove trailing /.
  if url.endswith("/"): url = url[:-1]

  # Discard if canonical url if it is just the front page.
  if prefix_pat.fullmatch(url) != None: return None

  # Discard if canonical URL is empty.
  if len(url.strip()) == 0: return None

  # Unescape HTML entities.
  return html.unescape(url)

def blocked(url):
  """Check if site is blocked."""
  return blocked_pat.match(url)

def iso2ts(date):
  """Convert ISO 8601 date to timestamp, i.e. seconds since epoch."""
  if date is None: return 0
  return calendar.timegm(time.strptime(date, "%Y-%m-%dT%H:%M:%SZ"))

dbsession = requests.Session()

def store(url, date, content):
  r = dbsession.put(
    flags.arg.crawldb + "/" + urllib.parse.quote(url),
    headers={
      "Version": str(iso2ts(date)),
      "Mode": "add",
    },
    data=content
  )
  r.raise_for_status()
  return r.headers["Result"]

def redirect(url, canonical):
  content = "#REDIRECT " + canonical
  r = dbsession.put(
    flags.arg.crawldb + "/" + urllib.parse.quote(url),
    headers={
      "Version": "0",
      "Mode": "add",
    },
    data=content.encode("utf8")
  )
  if r.status_code == 400: print(r.text)
  r.raise_for_status()
  return r.headers["Result"]

