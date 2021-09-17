# Copyright 2021 Ringgaard Research ApS
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Web log analyzer."""

import gzip
import re
import datetime
import urllib.parse
from collections import defaultdict
import sling.flags as flags

flags.define("-u",
             help="Output unknown pages",
             default=False,
             action="store_true")

flags.define("-k",
             help="Output known pages",
             default=False,
             action="store_true")

flags.define("-r",
             help="Output redirects",
             default=False,
             action="store_true")

flags.define("-d",
             help="Output downloads",
             default=False,
             action="store_true")

flags.define("-e",
             help="Output errors",
             default=False,
             action="store_true")

flags.define("-b",
             help="Output other bots",
             default=False,
             action="store_true")

flags.define("logfiles",
             nargs="*",
             help="NCSA log files in NCSA Combined format",
             metavar="FILE")

flags.parse()

ncsa_log_pattern = re.compile(
  r"(\d+\.\d+\.\d+\.\d+) - - "  # 1: IP address
  r"\[(.*?)\] "                 # 2: timestamp
  r"\"([A-Z]+) "                # 3: method
  r"(.*?) "                     # 4: path
  r"(.*?)\" "                   # 5: protocol
  r"(\d+) "                     # 6: HTTP status code
  r"(\d+) "                     # 7: size
  r"\"(.*?)\" "                 # 8: referrer
  r"\"(.*?)\""                  # 9: user agent
)

local_domains = set([
  "87.104.43.9",
  "87.104.43.9.static.fibianet.dk",
  "jbox.dk",
  "ringgaard.com",
  "localhost",
  "127.0.0.1",
  "",
])

url_pattern = re.compile(r"https?:\/\/([A-Za-z0-9\-\.]+)(\:[0-9]+)?(\/.*)?")
item_pattern = re.compile(r"\/kb\/item\?fmt=cjson\&id=(.+)")
query_pattern = re.compile(r"\/kb\/query\?fmt=cjson(\&fullmatch=1)?\&q=(.+)")
mobile_pattern = re.compile(r"iPhone|Android")

pages = [
  ("Home page",            re.compile(r"^\/$")),
  ("About page",           re.compile(r"^\/about$")),
  ("Contact page",         re.compile(r"^\/contact$")),
  ("Privacy page",         re.compile(r"^\/privacy$")),
  ("JSON item fetch",      re.compile(r"^\/kb/item\?fmt=cjson&id=(.+)$")),
  ("JSON name lookup",     re.compile(r"^\/kb/query\?fmt=cjson&q=(.+)$")),
  ("media file",           re.compile(r"^\/media\/.+")),
  ("thumbnail",            re.compile(r"^\/thumb\/.+")),
  ("common library",       re.compile(r"^\/common\/")),
  ("CMS search",           re.compile(r"^\/query:.*$")),
  ("CMS user",             re.compile(r"^\/\/?user\/.+")),
  ("CMS system",           re.compile(r"^\/system\/.+")),
  ("CMS admin",            re.compile(r"^\/admin.*")),
  ("data download",        re.compile(r"^\/data\/.*")),
  ("kb.js",                re.compile(r"^\/kb/app/kb.js")),
  ("KB home",              re.compile(r"^\/kb\/$")),
  ("KB item",              re.compile(r"^\/kb\/(.+)$")),
]

bots = [
  ("Google",                 re.compile(r"Googlebot")),
  ("Google Adwords",         re.compile(r"Google-Adwords-Instant")),
  ("Yandex",                 re.compile(r"YandexBot")),
  ("Baidu",                  re.compile(r"Baiduspider")),
  ("Zoom",                   re.compile(r"ZoominfoBot")),
  ("Twitter",                re.compile(r"Twitterbot/1.0")),
  ("Telegram",               re.compile(r"TelegramBot")),
  ("Facebook",               re.compile(r"facebookexternalhit")),
  ("WhatsApp",               re.compile(r"WhatsApp")),
  ("Apple",                  re.compile(r"Applebot")),
  ("Bing",                   re.compile(r"bingbot")),
  ("Telegram",               re.compile(r"TelegramBot")),
  ("Slack",                  re.compile(r"Slackbot")),
  ("Slack Image",            re.compile(r"Slack-ImgProxy")),
  ("Pinterest",              re.compile(r"Pinterestbot")),
  ("Semantic Scholar",       re.compile(r"SemanticScholarBot")),
  ("medium.com",             re.compile(r"Mediumbot-MetaTagFetcher")),
  ("DuckDuckGo Favicons",    re.compile(r"DuckDuckGo-Favicons-Bot")),
  ("commoncrawl.org",        re.compile(r"CCBot")),

  ("Majestic",               re.compile(r"MJ12bot")),
  ("Mail.RU",                re.compile(r"Mail.RU_Bot")),
  ("Ahrefs",                 re.compile(r"AhrefsBot")),
  ("Coc Coc Web",            re.compile(r"coccocbot-web")),
  ("Coc Coc Image",          re.compile(r"coccocbot-image")),
  ("SEMrush",                re.compile(r"SemrushBot")),
  ("Petal",                  re.compile(r"PetalBot")),
  ("Serendeputy",            re.compile(r"SerendeputyBot")),
  ("Pleroma",                re.compile(r"Pleroma")),
  ("Mastodon",               re.compile(r"Mastodon")),
  ("Seznam",                 re.compile(r"SeznamBot")),
  ("Ads",                    re.compile(r"Adsbot")),
  ("YaK",                    re.compile(r"bot@linkfluence.com")),
  ("Tweetmeme",              re.compile(r"TweetmemeBot")),
  ("similartech.com",        re.compile(r"SMTBot")),
  ("Semantic",               re.compile(r"Semanticbot")),
  ("Refind",                 re.compile(r"Refindbot")),
  ("BLEX",                   re.compile(r"BLEXBot")),
  ("DNSResearch",            re.compile(r"DNSResearchBot")),
  ("opensiteexplorer.org",   re.compile(r"DotBot")),
  ("xforce-security.com",    re.compile(r"oBot")),
  ("Paper.li",               re.compile(r"PaperLiBot")),
  ("linguee.com",            re.compile(r"Linguee Bot")),
  ("mediatoolkit.com",       re.compile(r"Mediatoolkitbot")),
  ("CensysInspect",          re.compile(r"CensysInspect")),
  ("Expanse",                re.compile(r"Expanse")),
  ("security.ipip.net",      re.compile(r"HTTP Banner Detection")),
  ("gdnplus.com",            re.compile(r"Gather Analyze Provide")),
  ("ltx71.com",              re.compile(r"ltx71.com")),
  ("Bytespider",             re.compile(r"Bytespider")),
  ("Nuzzel",                 re.compile(r"Nuzzel")),
  ("zgrab",                  re.compile(r"zgrab")),
  ("dataminr",               re.compile(r"dataminr.com")),
  ("netsystemsresearch",     re.compile(r"netsystemsresearch.com")),
  ("TsunamiSecurityScanner", re.compile(r"TsunamiSecurityScanner")),
  ("Yahoo answers",          re.compile(r"Y!J-DLC/1.0")),

  ("Other bots",           re.compile(r"[Bb]ot")),
  ("Other crawlers",       re.compile(r"[Cc]rawl")),
  ("Other bots",           re.compile(r"[Bb]ot")),
  ("Other crawlers",       re.compile(r"[Cc]rawl")),
  ("Other gabbers",        re.compile(r"[Gg]rabber")),
  ("Other spiders",        re.compile(r"[Ss]pider")),
]

worms = [
  ("login", re.compile(r"^\/login$")),
  ("XDEBUG_SESSION_START", re.compile(r"^\/\?XDEBUG_SESSION_START=phpstorm$")),
  ("php die", re.compile(r"^\/\?a=fetch\&content=\<php\>die")),
  ("invokefunction", re.compile(r"s=\/Index\/\\x5Cthink\\x5Capp\/invokefunction")),
  ("Python env", re.compile(r"^(\/)+\.env")),
  ("jsonws", re.compile(r"^\/api\/jsonws\/invoke$")),
  ("autodiscover", re.compile(r"^\/[Aa]utodiscover\/[Aa]utodiscover\.xml")),
  ("Backoffice", re.compile(r"^\/Backoffice\/")),
  ("wp-login.php", re.compile(r"wp-login\.php")),
  ("boaform login", re.compile(r"^\/boaform\/admin")),
  ("console", re.compile(r"^\/console\/")),
  ("ignition", re.compile(r"^\/_ignition\/execute-solution")),
  ("shittiest_lang.php", re.compile(r"^\/index.php\/PHP\%0Ais_the_shittiest_lang\.php")),
  ("jenkins login", re.compile(r"^\/jenkins\/login")),
  ("owa", re.compile(r"^\/owa\/")),
  ("phpmyadmin", re.compile(r"^\/phpmyadmin\/")),
  ("cgi-bin", re.compile(r"^\/cgi-bin\/")),
  ("fbclid", re.compile(r"^\/\?fbclid\=")),
  ("xploidID", re.compile(r"^\/\?xploidID\=")),
  ("author", re.compile(r"^\/\?author\=")),
]

spammers = set([
  "ahar.net",
  "amatocanizalez.net",
  "aucoinhomes.com",
  "aoul.top",
  "boyddoherty.top",
  "czcedu.com",
  "ecosia.org",
  "fineblog.top",
  "gcmx.net",
  "johnnyhaley.top",
  "joyceblog.top",
  "manwang.net",
  "meendoru.net",
  "myra.top",
  "pacificdentalcenter.com",
  "pardot.com",
  "rczhan.com",
  "sarahmilne.top",
  "sloopyjoes.com",
])

total_hits = 0
num_invalid = 0
num_unknown = 0
num_bots = 0
num_worms = 0
num_spammers = 0
num_internal = 0
num_errors = 0
num_cached = 0
num_posts = 0
num_robotstxt = 0
num_favicons = 0
num_hits = 0
num_bytes = 0
num_mobile = 0

page_hits = defaultdict(int)
date_hits = defaultdict(int)
date_visitors = defaultdict(set)
visitors = set()
download_hits = defaultdict(int)
media_hits = defaultdict(int)
item_hits = defaultdict(int)
query_hits = defaultdict(int)
http_codes = defaultdict(int)
bot_hits = defaultdict(int)
worm_hits = defaultdict(int)
spam_hits = defaultdict(int)
referrers = defaultdict(int)
referring_domains = defaultdict(int)
searches = defaultdict(int)

prev_query = {}
for logfn in flags.arg.logfiles:
  logfile = gzip.open(logfn, "rt")
  for logline in logfile:
    # Parse log line.
    total_hits += 1
    m = ncsa_log_pattern.match(logline)
    if m is None:
      num_invalid += 1
      continue

    # Get fields.
    ipaddr = m.group(1)
    timestamp = m.group(2)
    method = m.group(3)
    path = m.group(4)
    protocol = m.group(5)
    status = m.group(6)
    bytes = m.group(7)
    referrer = m.group(8)
    ua = m.group(9)

    # Internal traffic.
    if ipaddr.startswith("10.1."):
      num_internal += 1
      continue

    # Bots.
    bot = False
    for bot_name, bot_pattern in bots:
      if bot_pattern.search(ua):
        bot_hits[bot_name] += 1
        bot = True
        if flags.arg.b and bot_name.startswith("Other"): print(ua)
        break
    if bot:
      num_bots += 1
      continue

    # Worms.
    worm = False
    for worm_name, worm_pattern in worms:
      if worm_pattern.search(path):
        worm_hits[worm_name] += 1
        worm = True
        break
    if worm:
      num_worms += 1
      continue

    # robots.txt
    if path.startswith("/robots.txt"):
      num_robotstxt += 1
      continue

    # favicon.
    if path == "/favicon.ico" or path.startswith("/apple-touch-icon"):
      num_favicons += 1
      continue

    # HTTP errors.
    if status > "204": http_codes[status] += 1
    if status == "304":
      num_cached += 1
    elif status != "200" and status != "204":
      num_errors += 1
      if flags.arg.e: print(logline.strip())
      if status == "301" and flags.arg.r: print(logline.strip())
      continue

    # POSTs.
    if method == "POST":
      num_posts += 1
      continue

    # Mobile.
    if mobile_pattern.search(ua):
      num_mobile += 1

    # Referrers.
    if referrer != "-" and not referrer.startswith("https://ringgaard.com"):
      referrers[referrer] += 1
      m = url_pattern.match(referrer)
      if m != None:
        hostname = m.group(1)
        if hostname.startswith("www."): hostname = hostname[4:]
        if hostname in spammers:
          spam_hits[hostname] += 1
          num_spammers += 1
          continue

        dot = hostname.find(".")
        if dot != 0:
          domain = hostname[dot + 1:]
          if domain in spammers:
            spam_hits[domain] += 1
            num_spammers += 1
            continue

        if hostname not in local_domains:
          referring_domains[hostname] += 1

    # Downloads.
    if path.startswith("/data"):
      download_hits[path] += 1
      if flags.arg.d: print(logline.strip())

    # Media.
    if path.startswith("/media/"):
      media_hits[path[7:]] += 1
    elif path.startswith("/thumb/"):
      media_hits[path[7:]] += 1

    # Items.
    m = item_pattern.match(path)
    if m:
      item = urllib.parse.unquote(m.group(1))
      item_hits[item] += 1

    # Queries.
    m = query_pattern.match(path)
    if m:
      query = urllib.parse.unquote(m.group(2))
      query_hits[query] += 1
      prev = prev_query.get(ipaddr)
      if prev != None and prev != query:
       if query.startswith(prev):
         query_hits[prev] = 0
       elif prev.startswith(query):
         query_hits[query] = 0
      prev_query[ipaddr] = query

    # Searches.
    if path.startswith("/query:"):
      query = urllib.parse.unquote(path[7:])
      searches[query] += 1

    # Known pages.
    known = False
    for page_name, page_pattern in pages:
      if page_pattern.match(path):
        page_hits[page_name] += 1
        known = True
        break

    # Hits and visitors per day.
    ts = datetime.datetime.strptime(timestamp, "%d/%b/%Y:%H:%M:%S %z")
    day = ts.date()
    date_hits[day] += 1
    date_visitors[day].add(ipaddr)
    visitors.add(ipaddr)

    if known:
      if flags.arg.k: print(ua)
    else:
      num_unknown += 1
      page_hits[path] += 1
      if flags.arg.v: print(logline.strip())
    num_hits += 1
    num_bytes += int(bytes)

  logfile.close()

def print_table(title, keyname, table, chron=False):
  print("\n" + title + "\n")
  print("     # |       % | " + keyname)
  print("-------+---------+-----------------------------------------------")
  total = sum(table.values())
  if chron:
    for key in sorted(table.keys()):
      hits = table[key]
      pct = hits * 100.0 / total
      print("%6d | %6.2f%% | %s" % (hits, pct, key.strftime("%a, %B %d, %Y")))
  else:
    for key in sorted(table, key=table.get, reverse=True):
      hits = table[key]
      pct = hits * 100.0 / total
      if hits > 0: print("%6d | %6.2f%% | %s" % (hits, pct, key))
  print("-------+---------+-----------------------------------------------")
  print("%6d |         | total" % total)
  print("-------+---------+-----------------------------------------------")


visits_per_day = {}
for date, ipaddrs in date_visitors.items():
  visits_per_day[date] = len(ipaddrs)

print("\nSUMMARY\n")
print("%6d hits" % num_hits)
print("%6d visitors" % len(visitors))
print("%6d cached" % num_cached)
print("%6d POSTs" % num_posts)
print("%6d mobile hits" % num_mobile)
print("%6d internal hits" % num_internal)
print("%6d favicons" % num_favicons)
print("%6d robots.txt" % num_robotstxt)
print("%6d bot requests" % num_bots)
print("%6d worm requests" % num_worms)
print("%6d spam hits" % num_spammers)
print("%6d errors" % num_errors)
print("%6d unknown requests" % num_unknown)
print("%6d invalid requests" % num_invalid)
print("%6d total hits" % total_hits)
print("%6d MB" % (num_bytes / (1024 * 1024)))

print_table("PAGES", "page", page_hits)
print_table("HITS PER DAY", "date", date_hits, chron=True)
print_table("VISITS PER DAY", "date", visits_per_day, chron=True)
print_table("DOWNLOADS", "file", download_hits)
print_table("MEDIA", "file", media_hits)
print_table("ITEMS", "item", item_hits)
print_table("QUERIES", "query", query_hits)
print_table("SEARCHES", "query", searches)
print_table("REFFERING DOMAINS", "domain", referring_domains)
print_table("REFERRERS", "referrer", referrers)
print_table("HTTP ERRORS", "HTTP status", http_codes)
print_table("BOTS", "bot", bot_hits)
print_table("WORMS", "worm", worm_hits)
print_table("SPAM", "spam", spam_hits)

