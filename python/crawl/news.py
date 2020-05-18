import calendar
import html
import re
import requests
import time
import traceback
import urllib.parse
from threading import Thread
from queue import Queue
import sling
import sling.flags as flags

flags.define("--crawldb",
             help="database for crawled news articles",
             default="http://localhost:7070/crawl",
             metavar="URL")

flags.define("--threads",
             help="number of thread for crawler worker pool",
             default=10,
             type=int,
             metavar="NUM")

flags.define("--qsize",
             help="crawl queue size",
             default=1024,
             type=int,
             metavar="NUM")

flags.define("--timeout",
             help="HTTP fetch timeout",
             default=60,
             type=int,
             metavar="SECS")

flags.define("--max_errors_per_site",
             help="maximum number of crawl errors for site",
             default=5,
             type=int,
             metavar="NUM")

flags.define("--max_article_size",
             help="maximum article size",
             default=8*1024*1024,
             type=int,
             metavar="SIZE")

# Blocked sites.
blocked_urls = [
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
  "https://www.bbc.co.uk/news/video_and_audio/",
]

# Sites where the URL query is part of the unique identifier.
urls_with_query = [
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
]

# Extensions for media file like images and videos.
media_extensions = [".jpg", ".gif", ".png", ".m4v", ".mp4"]

blocked = [url.replace("/", "\\/") for url in blocked_urls]
blocked_pat = re.compile("|".join(blocked))
urls_with_query_pat = re.compile("|".join(urls_with_query))

canonical_tag_pat = re.compile(b'<link [^>]*rel=\"canonical\"[^>]*>')
href_attr_pat = re.compile(b' href=\"([^\"]*)\"')
prefix_pat = re.compile('https?:\/\/[^\/]+\/?')

http_req_headers = {
  "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 " \
                "(KHTML, like Gecko) Chrome/62.0.3202.94 Safari/537.36",
  "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
}

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

def sitename(url):
  """Return trimmed domain name for URL."""
  site = url
  if site.find("://") != -1: site = site[site.find("://") + 3:]
  if site.find(":/") != -1: site = site[site.find(":/") + 2:]
  if site.startswith("www."): site = site[4:]
  if site.find("/") != -1: site = site[:site.find("/")]
  return site

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
  if blocked_pat.match(url) is not None: return True
  for ext in media_extensions:
    if url.endswith(ext): return True
  return False

def iso2ts(date):
  """Convert ISO 8601 date to timestamp, i.e. seconds since epoch."""
  if date is None: return 0
  return calendar.timegm(time.strptime(date, "%Y-%m-%dT%H:%M:%SZ"))

dbsession = requests.Session()
crawlsession = requests.Session()

def store(url, date, content):
  """Store article in database."""
  if type(date) is str: date = iso2ts(date)
  r = dbsession.put(
    flags.arg.crawldb + "/" + urllib.parse.quote(url),
    headers={
      "Version": str(date),
      "Mode": "add",
    },
    data=content
  )
  r.raise_for_status()
  return r.headers["Result"]

def redirect(url, canonical):
  """Add redirect from url to canonical url to database."""
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

def known(url):
  """Check if article is already in database."""
  r = dbsession.head(
    flags.arg.crawldb + "/" + urllib.parse.quote(url))
  if r.status_code == 200 or r.status_code == 204: return True
  if r.status_code == 404: return False
  r.raise_for_status()
  return False

class Worker(Thread):
  """Worker thread for fetching articles."""
  def __init__(self, crawler):
    """Initialize worker thread."""
    Thread.__init__(self)
    self.crawler = crawler
    self.daemon = True
    self.start()

  def run(self):
    """Run worker fetching urls from the task queue."""
    while True:
      url = self.crawler.queue.get();
      try:
        self.crawler.fetch(url)
      except Exception as e:
        print("Error fetching", url, ":", e)
        traceback.print_exc()
      finally:
        self.crawler.queue.task_done()

class Crawler:
  """News crawler for fetching articles and storing them in the database."""
  def __init__(self, name):
    """Initialize crawler."""
    # Initialize queue and workers.
    self.name = name
    self.queue = Queue(flags.arg.qsize)
    for _ in range(flags.arg.threads): Worker(self)

    # Statistics.
    self.num_crawled = 0
    self.num_known = 0
    self.num_retrieved = 0
    self.num_failed = 0
    self.num_ignored = 0
    self.num_blocked = 0
    self.num_banned = 0
    self.num_redirects = 0
    self.num_big = 0

    # Per-site retrieval errors.
    self.site_errors = {}

  def wait(self):
    """Wait for crawler workers to complete."""
    self.queue.join()

  def crawl(self, url):
    """Add url to crawler queue."""
    self.num_crawled += 1
    self.queue.put(url)

  def fetch(self, url):
    """Fetch article and store it in the database."""
    # Check if url is blocked.
    if blocked(url):
      self.num_blocked += 1
      return

    # Check if error threshold for site has been reached.
    site = sitename(url)
    if self.site_errors.get(site, 0) >= flags.arg.max_errors_per_site:
      print("*** Ignore:", url)
      self.num_ignored += 1
      return

    # Check if article is already in database.
    trimmed_url = trim_url(url)
    if known(trimmed_url):
      self.num_known += 1
      return

    # Fetch news article from site.
    try:
      r = crawlsession.get(url, headers=http_req_headers,
                           timeout=flags.arg.timeout)
      if r.status_code == 451:
        print("*** Banned:", url)
        self.num_banned += 1
        return

      r.raise_for_status()

      # Build HTML header.
      h = ["HTTP/1.0 200 OK\r\n"]
      for key, value in r.headers.items():
        h.append(key)
        h.append(": ")
        h.append(value)
        h.append("\r\n")
      h.append("X-Domain: " + site + "\r\n")
      h.append("X-Crawler: " + self.name + "\r\n")
      h.append("\r\n")
      headers = "".join(h).encode("utf8")
      content = r.content
    except Exception as e:
      print("*** Article error:", e)
      self.num_failed += 1
      self.site_errors[site] = self.site_errors.get(site, 0) + 1
      return

    # Discard large articles.
    if len(content) > flags.arg.max_article_size:
      print("Article too big:", url, ",", len(content), "bytes")
      self.num_toobig += 1
      return

    # Get canonical url.
    canonical_url = get_canonical_url(trimmed_url, content)
    if canonical_url is None: canonical_url = trimmed_url

    # Save article in database.
    now = int(time.time())
    result = store(canonical_url, now, headers + content)
    if result == "new":
      self.num_retrieved += 1
    else:
      self.num_known += 1
      return

    # Add redirect if original url is different from the canonical url.
    if trimmed_url != canonical_url:
      result = redirect(trimmed_url, canonical_url)
      if result == "new":
        self.num_redirects += 1

    print(self.num_retrieved, url)

  def dumpstats(self):
    stats = [
      (self.num_crawled, "crawled"),
      (self.num_known, "known"),
      (self.num_retrieved, "retrieved"),
      (self.num_failed, "failed"),
      (self.num_ignored, "ignored"),
      (self.num_blocked, "blocked"),
      (self.num_banned, "banned"),
      (self.num_redirects, "redirects"),
      (self.num_big, "big"),
    ]
    print("SUMMARY:", ", ".join([str(s[0]) + " " + s[1] for s in stats]))

