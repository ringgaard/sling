"""
Load web pages in a web archive (WARC file) into a database.
"""

import requests
import sling
import sling.flags as flags
#import sling.crawl.news as news
import news

flags.define("warc",
             nargs="+",
             help="WARC file(s) with news articles",
             metavar="FILE")

flags.parse()

for warc in flags.arg.warc:
  num_urls = 0
  num_blocked = 0
  num_saved = 0
  num_dups = 0
  num_redirects = 0
  for uri, date, content in sling.WebArchive(warc):
    num_urls += 1
    if num_urls % 1000 == 0: print(warc, ":", num_urls, "urls", end="\r")

    # Trim URL.
    try:
      url = news.trim_url(uri.decode("utf8"))
    except Exception as e:
      print("Invalid URI:", uri, e)
      continue

    # Discard blocked sites.
    if news.blocked(url):
      num_blocked += 1
      continue

    # Discard large articles.
    if len(content) > flags.arg.max_article_size:
      print("Article too big:", url, ",", len(content), "bytes")
      continue

    # Get canonical URL.
    canonical = news.get_canonical_url(url, content)
    if canonical is None: canonical = url

    # Store web page under canonical URL.
    result = news.store(canonical, date, content)
    #print("%s %d [%s] %s" % (date, num_urls, result, canonical))
    if result == "new":
      num_saved += 1
    else:
      num_dups += 1

    # Add redirect if original URL is different from the canonical URL.
    if url != canonical:
      result = news.redirect(url, canonical)
      if result == "new":
        #print("REDIRECT %s %s" % (url, canonical))
        num_redirects += 1

  print(warc, ":", num_urls, "urls,", num_blocked, "blocked,",
        num_saved, "saved,", num_dups, "dups,", num_redirects, "redirects")

