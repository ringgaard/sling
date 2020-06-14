import tweepy
import json
import traceback
import sys
import time
import sling.crawl.news as news
import sling.flags as flags

flags.define("--apikeys",
             default="local/keys/twitter.json",
             help="Twitter API key file")

flags.define("--cache",
             default=None,
             help="mapping of twitter user names to ids")

flags.define("--retweets",
             help="Process retweets",
             default=False,
             action="store_true")

flags.parse()

# Load news site list.
news.init()

# Connect to Twitter.
with open(flags.arg.apikeys, "r") as f:
  apikeys = json.load(f)

auth = tweepy.OAuthHandler(apikeys["consumer_key"], apikeys["consumer_secret"])
auth.set_access_token(apikeys["access_key"], apikeys["access_secret"])
api = tweepy.API(auth, wait_on_rate_limit=True, wait_on_rate_limit_notify=True)

# Read twitter user cache.
user_cache = {}
if flags.arg.cache:
  with open(flags.arg.cache, "r") as f:
    for line in f.readlines():
      fields = line.strip().split(' ')
      user_cache[fields[0]] = fields[1]

# Make list of users to follow.
print("Look up feeds...")
feeds = set()
users = set()
for domain, site in news.sites.items():
  if site.twitter != None:
    users.add(site.twitter.lower()[1:])
    if site.twitter in user_cache:
      feeds.add(user_cache[site.twitter])
    else:
      try:
        user = api.get_user(site.twitter)
        feeds.add(str(user.id))
        print(site.twitter, user.id)
      except Exception as e:
        print("Ignore bad feed for domain", domain, ":", site.twitter, e)

print("Follow", len(feeds), "twitter feeds")

# Initialize news crawler.
crawler = news.Crawler("twitter")

class NewsStreamListener(tweepy.StreamListener):
  def on_status(self, status):
    # Ignore tweets without urls.
    if len(status.entities["urls"]) == 0: return

    # Check for retweet.
    retweet = status.text.startswith("RT @")

    # Get urls.
    urls = []
    for url in status.entities["urls"]:
      expanded_url = url["expanded_url"]
      if expanded_url.startswith("https://twitter.com/"): continue
      if expanded_url.startswith("https://www.twitter.com/"): continue
      if expanded_url.startswith("https://mobile.twitter.com/"): continue
      urls.append(expanded_url)
    if len(urls) == 0: return

    user = status.user.screen_name.lower()
    for url in urls:
      # Check for blocked sites.
      if news.blocked(url): continue

      # Check for news site. Try to crawl all urls in tweets from feeds.
      # Otherwise the site must be in the whitelist.
      site = news.sitename(url)
      if user not in users:
        if retweet and not flags.arg.retweets: continue
        if site not in news.sites: continue

      # Crawl URL.
      print("---", user, "-", news.trim_url(url))
      crawler.crawl(url)
      sys.stdout.flush()

  def on_error(self, status_code):
    print("Stream error:", status_code)
    return False

# Monitor live twitter stream for news articles.
while True:
  try:
    print("Start twitter stream")
    stream = tweepy.Stream(auth, NewsStreamListener(), tweet_mode="extended")
    stream.filter(follow=feeds)
    time.sleep(20)

  except KeyboardInterrupt as error:
    print("Stopped")
    crawler.dumpstats()
    sys.exit()

  except:
    traceback.print_exc(file=sys.stdout)
    time.sleep(60)

