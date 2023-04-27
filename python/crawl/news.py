import calendar
import html
import http.cookiejar
import json
import os
import sys
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
             default="crawl",
             metavar="DB")

flags.define("--newssites",
             default="data/crawl/newssites.txt",
             help="list of news sites")

flags.define("--cookiedir",
             default="local/cookies",
             help="directory for site-specific cookies")

flags.define("--threads",
             help="number of thread for crawler worker pool",
             default=10,
             type=int,
             metavar="NUM")

flags.define("--qsize",
             help="crawl queue size",
             default=100000,
             type=int,
             metavar="NUM")

flags.define("--timeout",
             help="HTTP fetch timeout",
             default=60,
             type=int,
             metavar="SECS")

flags.define("--max_errors_per_site",
             help="maximum number of crawl errors for site",
             default=10,
             type=int,
             metavar="NUM")

flags.define("--max_article_size",
             help="maximum article size",
             default=8*1024*1024,
             type=int,
             metavar="SIZE")

# Connection pool for fetching news.
crawlsession = requests.Session()
crawldb = None

# Blocked sites.
blocked_urls = [
  "www.\w+.com.au/nocookies",
  "www.washingtonpost.com/gdpr-consent",
  "www.forbes.com/forbes/welcome",
  "consent.yahoo.com/",
  "choice.npr.org/",
  "www.bloomberg.com/tosv2.html",
  "www.\w+.com/_services/v1/client_captcha/",
  "www.zeit.de/zustimmung",
  "myprivacy.dpgmedia.net/",
  "www.tribpub.com/gdpr/",
  "tolonews.com/fa/",
  "www.theaustralian.com.au/subscribe/news/1",

  "pjmedia.com/instapundit/",
  "www.espn.com/espnradio/",
  "www.bbc.co.uk/news/video_and_audio/",
  "youtube.com/",
  "youtu.be/",
  "www.youtube.com/",
  "consent.youtube.com/",
  "news.google.com/",
  "www.tiktok.com/",
  "www.instagram.com/",
  "video.foxnews.com/",
  "www.facebook.com/",
  "www.twitter.com/",
  "www.pscp.tv/",
  "video.repubblica.it/",
  "www.foxtv.com/",
  "bangla.dhakatribune.com/",
]

# Sites that should never be ignored because of too many errors.
noignore_sites = set([
  "bit.ly",
  "buff.ly",
  "dlvr.it",
  "hill.cm",
  "hrld.us",
  "ift.tt",
  "j.mp",
  "ow.ly",
  "tinyurl.com",
  "trib.al",
  "u.afp.com",
  "zpr.io",
])

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
  "http://www.koreaherald.com/",
]

# Extensions for media file like images and videos.
media_extensions = [".jpg", ".gif", ".png", ".m4v", ".mp4", ".webm"]

blocked = [("https?://" + url).replace("/", "\\/") for url in blocked_urls]
blocked_pat = re.compile("|".join(blocked))
urls_with_query_pat = re.compile("|".join(urls_with_query))

canonical_tag_pat = re.compile(b'<link\s+[^>]*rel=\"canonical\"[^>]*>')
href_attr_pat = re.compile(b'\shref=\"([^\"]*)\"')
prefix_pat = re.compile('https?:\/\/[^\/]+\/?')
video_pat = re.compile('https?:\/\/.*\/videos?\/.*')

# HTTP headers.

default_headers = {
  "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 " \
                "(KHTML, like Gecko) Chrome/62.0.3202.94 Safari/537.36",
  "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
  "Accept-Language": "*",
}

curl_headers = {
  "User-Agent": "curl",
}

gbot_headers = {
  "User-Agent": "GoogleBot",
}

http_site_headers = {
  "bloomberg.com": gbot_headers,
  "engadget.com": curl_headers,
  "forbes.com": curl_headers,
  "news.yahoo.com": curl_headers,
  "npr.org": {
    "Cookie": "trackingChoice=true;choiceVersion=1"
  },
  "techcrunch.com": curl_headers,
  "usnews.com": {
    "User-Agent": "Mozilla/5.0",
    "Cookie": "gdpr_agreed=4;usprivacy=1YNY",
  },
  "volkskrant.nl": curl_headers,
  "washingtonpost.com":  {
    "Cookie": "wp_gdpr=1|1;wp_devicetype=0;wp_country=US"
  },
  "zeit.de": curl_headers,
  "yahoo.com": curl_headers,
  "yhoo.it": curl_headers,
}

def init_cookies():
  if os.path.isdir(flags.arg.cookiedir):
    jar = http.cookiejar.CookieJar()
    crawlsession.cookies = jar
    for filename in os.listdir(flags.arg.cookiedir):
      with open(flags.arg.cookiedir + '/' + filename) as f:
        cookies = json.load(f)
      for cookie in cookies:
        jar.set_cookie(http.cookiejar.Cookie(
          version=0,
          name=cookie["name"],
          value=cookie["value"],
          port=None,
          port_specified=False,
          domain=cookie["domain"],
          domain_specified=True,
          domain_initial_dot=cookie["domain"].startswith('.'),
          path=cookie["path"],
          path_specified=True,
          secure=cookie["secure"],
          expires=None,
          discard=True,
          comment=None,
          comment_url=None,
          rest={'HttpOnly': None},
          rfc2109=False))

class NewsSite:
  def __init__(self, domain, qid, name, twitter=None, altdomain=None):
    self.domain = domain
    self.qid = qid
    self.name = name
    self.twitter = twitter
    self.altdomain = altdomain

sites = {}

def init_sites():
  # Load news site list.
  with open(flags.arg.newssites, "r") as f:
    for line in f.readlines():
      line = line.strip()
      if len(line) == 0 or line[0] == '#': continue
      fields = line.split(",")
      if len(fields) < 3:
        print("too few fields for news site:", line)
        continue
      domain = fields[0]
      qid = fields[1]
      name = fields[2]
      if not qid.startswith("Q"): print("warning:", qid)
      if "@" in name: print("warning:", name)

      twitter = None
      if len(fields) >= 4:
        twitter = fields[3]
        if len(twitter) == 0:
          twitter = None
        else:
          if not twitter.startswith("@"):
            print("illegal twitter id:", line)
            continue

      altdomain = None
      if len(fields) >= 5:
        altdomain = fields[4]

      if domain in sites:
        print("multiple news sites for domain", domain)
        continue
      sites[domain] = NewsSite(domain, qid, name, twitter, altdomain)
      if altdomain:
        if altdomain in sites:
          print("multiple news sites for domain", altdomain)
        else:
          sites[altdomain] = sites[domain]

def init():
  # Connect to crawl database.
  global crawldb
  crawldb = sling.Database(flags.arg.crawldb, sys.argv[0])

  # Load cookies.
  init_cookies()

  # Initialize news sites.
  init_sites()

def trim_url(url):
  """Trim parts of news url that are not needed for uniqueness."""
  # Strip leading and trailing whitespace.
  url = url.strip()

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

url_prefixes = ["www.", "eu.", "uk.", "rss.", "rssfeeds.", "m."]

def sitename(url):
  """Return trimmed domain name for URL."""
  site = url
  if site.find("://") != -1: site = site[site.find("://") + 3:]
  if site.find(":/") != -1: site = site[site.find(":/") + 2:]
  for prefix in url_prefixes:
    if site.startswith(prefix): site = site[len(prefix):]
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
  if url == "https://www.theaustralian.com.au/subscribe/news/1": return None

  # Discard if canonical URL is empty.
  if len(url.strip()) == 0: return None

  # Discard canonical URL if it is not valid.
  if not url.startswith("http"): return None

  # Trim URL and Unescape HTML entities.
  return html.unescape(trim_url(url))

def retrieve_article(url):
  # Get news site name.
  site = sitename(url)

  # Determine HTTP headers for crawling site.
  http_headers = default_headers
  if site in http_site_headers:
    http_headers = http_site_headers[site]

  # Fetch news article from site.
  r = crawlsession.get(url, headers=http_headers,
                       timeout=flags.arg.timeout)
  r.raise_for_status()

  # Get target for redirected URL.
  target_url = trim_url(r.url)
  if r.url != url: site = sitename(r.url)

  # Build HTML header.
  h = ["HTTP/1.0 200 OK\r\n"]
  for key, value in r.headers.items():
    h.append(key)
    h.append(": ")
    h.append(value)
    h.append("\r\n")
  h.append("X-Domain: " + site + "\r\n")
  h.append("\r\n")
  headers = "".join(h).encode("utf8")
  return headers + r.content

def blocked(url):
  """Check if site is blocked."""
  if blocked_pat.match(url) is not None: return True
  for ext in media_extensions:
    if url.endswith(ext): return True
  if video_pat.match(url): return True
  return False

def iso2ts(date):
  """Convert ISO 8601 date to timestamp, i.e. seconds since epoch."""
  if date is None: return 0
  return calendar.timegm(time.strptime(date, "%Y-%m-%dT%H:%M:%SZ"))

def store(url, date, content):
  """Store article in database."""
  if type(date) is str: date = iso2ts(date)
  return crawldb.add(url, content, version=date)

def redirect(url, canonical):
  """Add redirect from url to canonical url to database."""
  content = "#REDIRECT " + canonical
  return crawldb.add(url, content)

def known(url):
  """Check if article is already in database."""
  return url in crawldb

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
      url = self.crawler.queue.get()
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
    self.num_filtered = 0
    self.num_banned = 0
    self.num_redirects = 0
    self.num_big = 0

    # Per-site retrieval errors.
    self.max_errors = flags.arg.max_errors_per_site
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
      print("*** Blocked:", url)
      self.num_blocked += 1
      return

    # Check if error threshold for site has been reached.
    site = sitename(url)
    site_errors = self.site_errors.get(site, 0)
    if self.max_errors != 0 and site_errors >= self.max_errors:
      if site not in noignore_sites:
        print("*** Ignore:", url)
        self.num_ignored += 1
        return

    # Check if article is already in database.
    trimmed_url = trim_url(url)
    if known(trimmed_url):
      self.num_known += 1
      return

    # Determine HTTP headers for crawling site.
    http_headers = default_headers
    if site in http_site_headers:
      http_headers = http_site_headers[site]

    # Fetch news article from site.
    try:
      r = crawlsession.get(url, headers=http_headers,
                           timeout=flags.arg.timeout)
      if r.status_code == 451:
        print("*** Banned:", url)
        self.num_banned += 1
        self.site_errors[site] = self.site_errors.get(site, 0) + 1
        return

      r.raise_for_status()

      # Get target for redirected URL.
      target_url = trim_url(r.url)
      if r.url != url: site = sitename(r.url)

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
      self.num_big += 1
      return

    # Get canonical url.
    canonical_url = get_canonical_url(trimmed_url, content)
    if canonical_url is None: canonical_url = target_url

    # Check if canonical url is blocked.
    if blocked(canonical_url):
      print("*** Blocked:", canonical_url, "from", url)
      self.num_blocked += 1
      return

    # Check if canonical url site is white-listed.
    if len(sites) > 0 and sitename(canonical_url) not in sites:
      print("*** Filtered:", canonical_url, "from", url,
            "site:", sitename(canonical_url))
      self.num_filtered += 1
      return

    # Save article in database.
    now = int(time.time())
    result = store(canonical_url, now, headers + content)
    if result == sling.DBNEW:
      self.num_retrieved += 1
    else:
      self.num_known += 1
      return

    # Add redirect if original url is different from the canonical url.
    if trimmed_url != canonical_url:
      result = redirect(trimmed_url, canonical_url)
      if result == sling.DBNEW:
        self.num_redirects += 1

    # Clear site error counter.
    self.site_errors[site] = 0

    print("[%d] %d %s" % (self.queue.qsize(),
                          self.num_retrieved,
                          canonical_url))
    sys.stdout.flush()

  def dumpstats(self):
    stats = [
      (self.num_crawled, "crawled"),
      (self.num_known, "known"),
      (self.num_retrieved, "retrieved"),
      (self.num_failed, "failed"),
      (self.num_ignored, "ignored"),
      (self.num_blocked, "blocked"),
      (self.num_filtered, "filtered"),
      (self.num_banned, "banned"),
      (self.num_redirects, "redirects"),
      (self.num_big, "big"),
    ]
    print("SUMMARY:", ", ".join([str(s[0]) + " " + s[1] for s in stats]))

