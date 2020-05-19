import praw
import json
import traceback
import sys
import time
import sling.crawl.news as news
import sling.flags as flags

flags.define("--newssites",
             default="data/crawl/newssites.txt",
             help="domain names for news sites")

flags.define("--apikeys",
             default="local/keys/reddit.json",
             help="Reddit API key file")

flags.parse()

# Consider all submission to these subreddits as news articles.
news_reddits = [
  "AutoNewspaper", "nofeenews",
]

# Read known news sites.
newssites = set()
with open(flags.arg.newssites, "r") as f:
  for line in f.readlines():
    line = line.strip()
    fields = line.split(",")
    if len(fields) >= 1:
      site = fields[0]
      if len(site) > 0: newssites.add(site)

# Connect to Reddit.
with open(flags.arg.apikeys, "r") as f:
  apikeys = json.load(f)

reddit = praw.Reddit(client_id=apikeys["client_id"],
                     client_secret=apikeys["client_secret"],
                     user_agent=apikeys["user_agent"],
                     check_for_updates=False)
reddit.read_only = True

# Monitor live Reddit submission stream for news articles.
crawler = news.Crawler("reddit")
while True:
  try:
    for submission in reddit.subreddit('all').stream.submissions():
      # Ignore self submissions.
      if submission.is_self: continue

      # Discard non-news sites.
      if submission.over_18: continue
      subreddit = str(submission.subreddit)
      url = submission.url
      if news.blocked(url): continue
      site = news.sitename(url)
      if subreddit not in news_reddits:
        if site not in newssites: continue

      # Crawl URL.
      domain = str(submission.domain)
      title = str(submission.title)
      print("---", subreddit, domain, "-", title)
      crawler.crawl(url)
      sys.stdout.flush()

    print("restart submission stream")
    time.sleep(20)

  except KeyboardInterrupt as error:
    print("Stopped")
    sys.exit()

  except:
    traceback.print_exc(file=sys.stdout)
    time.sleep(60)

