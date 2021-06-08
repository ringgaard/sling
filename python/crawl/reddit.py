import praw
import json
import traceback
import requests
import sys
import time
import urllib
import sling
import sling.crawl.news as news
import sling.flags as flags

flags.define("--apikeys",
             default="local/keys/reddit.json",
             help="Reddit API key file")

flags.define("--redditdb",
             default="reddit",
             help="Reddit submissions database")

flags.define("--subreddits",
             default=None,
             help="File with subreddits that should be archived")

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
  "newsokuexp",
]

# Load news site list.
news.init()

# Read list of monitored subreddits.
session = requests.Session()
subreddits = set()
if flags.arg.subreddits:
  with open(flags.arg.subreddits, "r") as f:
    for line in f.readlines():
      sr = line.strip().lower();
      if len(sr) == 0 or sr[0] == '#': continue;
      subreddits.add(sr);
  print("Crawl", len(subreddits), "subreddits")

# Connect to reddit submission database.
redditdb = None
if len(subreddits) > 0:
  redditdb = sling.Database(flags.arg.redditdb)

# Connect to Reddit.
with open(flags.arg.apikeys, "r") as f:
  apikeys = json.load(f)

reddit = praw.Reddit(client_id=apikeys["client_id"],
                     client_secret=apikeys["client_secret"],
                     user_agent=apikeys["user_agent"],
                     check_for_updates=False)
reddit.read_only = True

# Fetch submission and store in database.
def fetch_submission(id):
  try:
    # Fetch submission from Reddit.
    headers = {"User-agent": "SLING Bot 1.0"}
    r = session.get("https://api.reddit.com/api/info/?id=" + id, headers=headers)
    r.raise_for_status()
    root = r.json()
    data = root["data"]["children"][0]["data"]

    # Save submission in database.
    redditdb[id] = json.dumps(data)
  except:
    traceback.print_exc(file=sys.stdout)

# Monitor live Reddit submission stream for news articles.
crawler = news.Crawler("reddit")
while True:
  try:
    for submission in reddit.subreddit('all').stream.submissions():
      # Ignore self submissions.
      if submission.is_self: continue

      # Archive submission if it is in a monitored subreddit.
      subreddit = str(submission.subreddit).lower()
      if subreddit in subreddits:
        print("###",
              str(submission.subreddit),
              submission.name,
              "NSFW" if submission.over_18 else "",
              submission.url,
              submission.title)
        fetch_submission(submission.name)

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
    crawler.dumpstats()
    sys.exit()

  except:
    traceback.print_exc(file=sys.stdout)
    time.sleep(60)

