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

flags.define("-v",
             help="Output detailed statistics",
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
  ("KnolCase page",        re.compile(r"^\/knolcase$")),
  ("CotM page",            re.compile(r"^\/cotm$")),
  ("Home app",             re.compile(r"^\/home\/app\/")),
  ("Home image",           re.compile(r"^\/home\/image\/")),
  ("Case home",            re.compile(r"^\/c\/$")),
  ("Case open",            re.compile(r"^\/c\/\d+")),
  ("Case app",             re.compile(r"^\/(case|c)\/app\/(.+)$")),

  ("media file",           re.compile(r"^\/media\/.+")),
  ("thumbnail",            re.compile(r"^\/thumb\/.+")),

  ("common library",       re.compile(r"^\/common\/")),

  ("CMS search (legacy)",  re.compile(r"^\/query:.*$")),
  ("CMS user (legacy)",    re.compile(r"^\/\/?user\/.+")),
  ("CMS system (legacy)",  re.compile(r"^\/system\/.+")),
  ("CMS admin (legacy)",   re.compile(r"^\/admin.*")),

  ("data download",        re.compile(r"^\/data\/.*")),

  ("KB app",               re.compile(r"^\/kb/app/")),
  ("KB home",              re.compile(r"^\/kb\/$")),
  ("KB item",              re.compile(r"^\/kb\/(.+)$")),

  ("photo search",         re.compile(r"^\/photosearch\/")),
  ("redreport",            re.compile(r"^\/redreport\/")),
  ("reddit report",        re.compile(r"^\/reddit\/report\/")),
]

apis = [
  ("item fetch",           re.compile(r"^\/kb/item\?fmt=cjson&id=(.+)$")),
  ("name lookup",          re.compile(r"^\/kb/query\?fmt=cjson&q=(.+)$")),

  ("Wikidata auth",        re.compile(r"^\/case/wikibase\/(.+)$")),
  ("create case",          re.compile(r"^\/case\/new$")),
  ("fetch case",           re.compile(r"^\/case/fetch\/?\?(.+)$")),
  ("share case",           re.compile(r"^\/case/share$")),
  ("case service",         re.compile(r"^\/case/service\/")),
  ("case proxy",           re.compile(r"^\/case/proxy\?")),
  ("case plugin",          re.compile(r"^\/case/plugin\/")),
  ("case xrefs",           re.compile(r"^\/case/xrefs$")),
  ("image caching",        re.compile(r"^\/case/cacheimg$")),
  ("schema",               re.compile(r"^\/schema(.+)$")),
  ("collaboration",        re.compile(r"^\/collab\/")),
  ("redreport add media",  re.compile(r"^\/redreport\/addmedia\/")),


  ("KB query" ,            re.compile(r"^\/kb\/query\?")),
  ("KB search",            re.compile(r"^\/kb\/search\?")),
  ("KB item",              re.compile(r"^\/kb\/item\?")),
  ("KB frame",             re.compile(r"^\/kb\/frame\?")),
  ("KB topic",             re.compile(r"^\/kb\/topic\?")),
  ("KB stubs",             re.compile(r"^\/kb\/stubs\?")),


  ("refine" ,              re.compile(r"^\/refine")),
  ("preview" ,             re.compile(r"^\/preview")),
]

browsers = [
  ("Firefox",              re.compile(r"Firefox\/([0-9\.]+)")),
  ("Edge",                 re.compile(r"Edge?\/([0-9\.]+)")),
  ("Chromium",             re.compile(r"Chromium\/([0-9\.]+)")),
  ("Opera",                re.compile(r"OPR\/([0-9\.]+)")),
  ("Opera",                re.compile(r"Opera\/([0-9\.]+)")),
  ("Chrome",               re.compile(r"Chrome\/([0-9\.]+)")),
  ("Chrome",               re.compile(r"CriOS\/([0-9\.]+)")),
  ("Safari",               re.compile(r"Safari\/([0-9\.]+)")),
  ("Internet Explorer",    re.compile(r"; MSIE \d+;")),
  ("Internet Explorer",    re.compile(r"Trident\/([0-9\.]+)")),
  ("Facebook",             re.compile(r"Mobile\/15E148")),
  ("curl",                 re.compile(r"curl\/([0-9\.]+)")),
  ("python",               re.compile(r"python-requests\/([0-9\.]+)")),
  ("python",               re.compile(r"Python-urllib\/([0-9\.]+)")),
  ("python",               re.compile(r"Python\/([0-9\.]+)")),
  ("pip",                  re.compile(r"pip\/([0-9\.]+)")),
  ("perl",                 re.compile(r"libwww-perl\/([0-9\.]+)")),
  ("None",                 re.compile(r"-")),
  ("Other",                re.compile(r"")),
]

platforms = [
  ("Windows",              re.compile(r"Windows NT (\d+\.\d+)")),
  ("MacOS",                re.compile(r"Mac OS X ([0-9_]+)")),
  ("IOS",                  re.compile(r"iPhone OS ([0-9_]+)")),
  ("IOS",                  re.compile(r"iOS ([0-9_]+)")),
  ("Android",              re.compile(r"Android (\d+)")),
  ("Chrome OS",            re.compile(r"CrOS (\w+)")),
  ("Linux",                re.compile(r"Linux (\w+)")),
  ("FreeBSD",              re.compile(r"FreeBSD (\w+)")),
  ("Other",                re.compile(r"")),
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
  ("Dataprovider.com",       re.compile(r"Dataprovider\.com")),

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
  ("NetcraftSurveyAgent",    re.compile(r"NetcraftSurveyAgent")),


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
  ("owa", re.compile(r"^\/owa")),
  ("nmap", re.compile(r"^\/nmap")),
  ("pools", re.compile(r"^\/pools")),
  ("phpmyadmin", re.compile(r"^\/phpmyadmin\/")),
  ("cgi-bin", re.compile(r"^\/cgi-bin\/")),
  ("fbclid", re.compile(r"^\/\?fbclid\=")),
  ("xploidID", re.compile(r"^\/\?xploidID\=")),
  ("author", re.compile(r"^\/\?author\=")),
  ("jndi", re.compile(r"^\/\?x=\$\{jndi")),
  ("dns", re.compile(r"^\/\?dns\=")),
  ("dns", re.compile(r"^\/\?\=PHP")),
  ("unix", re.compile(r"^\/\?unix:")),
  ("version.js", re.compile(r"^\/c\/version\.js")),
  ("root params", re.compile(r"^\/\?")),
]

spammers = set([
  "ahar.net",
  "amatocanizalez.net",
  "aoul.top",
  "aucoinhomes.com",
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
  "qxnr.net",
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
num_monitor = 0
num_curls = 0

page_hits = defaultdict(int)
api_calls = defaultdict(int)
date_hits = defaultdict(int)
date_visitors = defaultdict(set)
visitors = set()
download_hits = defaultdict(int)
media_hits = defaultdict(int)
item_hits = defaultdict(int)
query_hits = defaultdict(int)
browser_hits = defaultdict(int)
platform_hits = defaultdict(int)
http_codes = defaultdict(int)
bot_hits = defaultdict(int)
worm_hits = defaultdict(int)
spam_hits = defaultdict(int)
referrers = defaultdict(int)
referring_domains = defaultdict(int)

prev_query = {}
for logfn in flags.arg.logfiles:
  if logfn.endswith(".gz"):
    logfile = gzip.open(logfn, "rt")
  else:
    logfile = open(logfn, "rt")
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
    if ipaddr.startswith("10.1.") or ipaddr.startswith("127."):
      if ua.startswith("Monit/"):
        num_monitor += 1
      else:
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

    # Mobile.
    if mobile_pattern.search(ua):
      num_mobile += 1

    # Browsers and platforms.
    for browser_name, browser_pattern in browsers:
      if browser_pattern.search(ua):
        browser_hits[browser_name] += 1
        #if browser_name == "Other": print("Browser?", ua)
        break

    for platform_name, platform_pattern in platforms:
      if platform_pattern.search(ua):
        platform_hits[platform_name] += 1
        #if platform_name == "Other": print("Platform?", ua)
        break

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
      if not path.endswith("/"): download_hits[path] += 1
      if flags.arg.d: print(logline.strip())
    if ua.startswith("curl/"): num_curls += 1

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

    # Known APIs.
    known = False
    for api_call, api_pattern in apis:
      if api_pattern.match(path):
        api_calls[api_call] += 1
        known = True
        break

    # Known pages.
    if not known:
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
      if flags.arg.e: print(logline.strip())
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
print("%6d internal monitoring" % num_monitor)
print("%6d favicons" % num_favicons)
print("%6d robots.txt" % num_robotstxt)
print("%6d curl requests" % num_curls)
print("%6d bot requests" % num_bots)
print("%6d worm requests" % num_worms)
print("%6d spam hits" % num_spammers)
print("%6d errors" % num_errors)
print("%6d unknown requests" % num_unknown)
print("%6d invalid requests" % num_invalid)
print("%6d total hits" % total_hits)
print("%6d MB" % (num_bytes / (1024 * 1024)))

print_table("PAGES", "page", page_hits)
print_table("API CALLS", "api", api_calls)
print_table("HITS PER DAY", "date", date_hits, chron=True)
print_table("VISITS PER DAY", "date", visits_per_day, chron=True)
print_table("BROWSERS", "browser", browser_hits)
print_table("PLATFORMS", "platform", platform_hits)
print_table("DOWNLOADS", "file", download_hits)
if flags.arg.v:
  print_table("MEDIA", "file", media_hits)
  print_table("ITEMS", "item", item_hits)
  print_table("QUERIES", "query", query_hits)
print_table("REFFERING DOMAINS", "domain", referring_domains)
print_table("REFERRERS", "referrer", referrers)
if flags.arg.v:
  print_table("HTTP ERRORS", "HTTP status", http_codes)
  print_table("BOTS", "bot", bot_hits)
  print_table("WORMS", "worm", worm_hits)
  print_table("SPAM", "spam", spam_hits)

