"""
Fetch news articles and put them into news archive.
"""

import requests
import sling
import sling.flags as flags
import sling.crawl.news as news

flags.define("--urls",
             help="File with urls to fetch",
             default=None,
             metavar="FILE")

flags.define("url",
             nargs="*",
             help="Article URLs to fetch",
             metavar="URL")

flags.parse()

news.init()
crawler = news.Crawler("fetch")

for url in flags.arg.url:
  crawler.crawl(url)

if flags.arg.urls:
  with open(flags.arg.urls) as f:
    for url in f.readlines():
      crawler.crawl(url.strip())

crawler.wait()
crawler.dumpstats()

