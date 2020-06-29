"""
Fetch news article and put it into news archive.
"""

import requests
import sling
import sling.flags as flags
import sling.crawl.news as news

flags.define("url",
             nargs="+",
             help="Article URLs to fetch",
             metavar="URL")

flags.parse()

news.init()
crawler = news.Crawler("fetch")

for url in flags.arg.url:
  crawler.crawl(url)

crawler.wait()
crawler.dumpstats()

