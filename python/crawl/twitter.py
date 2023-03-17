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

flags.define("--filter",
             default=None,
             help="file with twitter filters")

flags.define("--max_rule_length",
             help="Maximum length of twitter filter rule",
             default=512,
             type=int,
             metavar="NUM")

flags.define("--max_rules",
             help="Maximum number of twitter filter rules",
             default=25,
             type=int,
             metavar="NUM")

flags.define("--retweets",
             help="Process retweets",
             default=False,
             action="store_true")

flags.parse()

num_tweets = 0
user_cache = {}

def collect_urls(obj, urls):
  if "entities" in obj:
    entities = obj["entities"]
    if "urls" in entities:
      for url in entities["urls"]:
        if "unwound_url" in url:
          expanded_url = url["unwound_url"]
        else:
          expanded_url = url["expanded_url"]
        if expanded_url.startswith("https://twitter.com/"): continue
        if expanded_url.startswith("https://www.twitter.com/"): continue
        if expanded_url.startswith("https://mobile.twitter.com/"): continue
        urls.add(expanded_url)

  if "retweeted_status" in obj:
    retweet = obj["retweeted_status"]
    collect_urls(retweet, urls)
  if "extended_tweet" in obj:
    extended = obj["extended_tweet"]
    collect_urls(extended, urls)

class NewsStreamFeed(tweepy.StreamingClient):
  def on_data(self, data):
    global num_tweets
    num_tweets += 1
    tweet = json.loads(data)["data"]

    # Check for retweet.
    retweet = tweet["text"].startswith("RT @")
    if retweet and not flags.arg.retweets: return

    # Get author.
    user = tweet["author_id"]
    user = user_cache.get(user, user)

    #print(json.dumps(tweet))

    # Get urls.
    urls = set([])
    collect_urls(tweet, urls)

    for url in urls:
      # Check for blocked sites.
      if news.blocked(url): continue

      # Check for news site. Try to crawl all urls in tweets from feeds.
      # Otherwise the site must be in the whitelist.
      site = news.sitename(url)
      if site not in news.sites: continue

      # Crawl URL.
      print("---", num_tweets, user, "-", news.trim_url(url))
      crawler.crawl(url)
      sys.stdout.flush()

  def on_errors(self, errors):
    print("Stream error:", tweet)
    return False

# Read twitter user cache.
if flags.arg.cache:
  with open(flags.arg.cache, "r") as f:
    for line in f.readlines():
      fields = line.strip().split(' ')
      user_name = fields[0][1:]
      user_id = fields[1]
      user_cache[user_id] = user_name

# Get twitter credentials.
with open(flags.arg.apikeys, "r") as f:
  apikeys = json.load(f)

# Create twitter stream.
feed = NewsStreamFeed(apikeys["bearer_token"], wait_on_rate_limit=True)

# Update filter.
if flags.arg.filter:
  # Create new rules.
  newrules = []
  with open(flags.arg.filter, "r") as f:
    parts = []
    left = flags.arg.max_rule_length
    for line in f.readlines():
      line = line.strip()
      if len(line) == 0 or line[0] == ";": continue
      if left < len(line):
        newrules.append(" OR ".join(parts))
        parts = []
        left = flags.arg.max_rule_length
      parts.append(line)
      left -= len(line) + 4
  if len(parts) > 0: newrules.append(" OR ".join(parts))
  if len(newrules) > flags.arg.max_rules:
    print(flags.arg.max_rules, "of", len(newrules),"rules used")
    print("skip from", newrules[flags.arg.max_rules])
    newrules = newrules[:flags.arg.max_rules]
  else:
    print(len(newrules), "rules")

  # Delete all existing rules.
  rules = feed.get_rules()
  if rules.data is not None:
    ruleids = []
    for rule in rules.data: ruleids.append(rule.id)
    feed.delete_rules(ruleids)

  # Create new rules.
  for r in newrules:
    print("add rule:", r)
    feed.add_rules(tweepy.StreamRule(r))

# Initialize news crawler.
news.init()
crawler = news.Crawler("twitter")

print("Start")
feed.filter(expansions=["author_id"], tweet_fields=["entities"])

