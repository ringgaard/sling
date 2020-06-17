import praw
import json
import traceback
import sys
import time
import sling.crawl.news as news
import sling.flags as flags

flags.define("--apikeys",
             default="local/keys/reddit.json",
             help="Reddit API key file")

flags.parse()

# Consider all submission to these subreddits as news articles.
news_reddits = [
  "AutoNewspaper", "nofeenews", "newsdk", "news", "Full_news",
  "qualitynews", "worldnews", "worldevents",
]

# Ignored subreddits.
ignored_reddits = [
  "u_toronto_news",
  "newsokur",
]

# Load news site list.
news.init()

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
        if subreddit in ignored_reddits: continue
        if site not in news.sites: continue

      # Crawl URL.
      domain = str(submission.domain)
      title = str(submission.title)
      print("---", domain, subreddit, "-", title)
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

