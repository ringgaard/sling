import json
import requests
import sys
import time

credentials = None
stream_credentials = None
url_prefix = "https://api.companieshouse.gov.uk"
stream_url = "https://stream.companieshouse.gov.uk/companies"
apisession = requests.Session()
quota_priority = 10
quota_left = 0

# Initialize API keys.
def init(apikeys, priority=10):
  global credentials, stream_credentials, quota_priority
  with open(apikeys) as f:
    apikey = f.readline().strip()
    streamkey = f.readline().strip()
  credentials = requests.auth.HTTPBasicAuth(apikey, "")
  stream_credentials = requests.auth.HTTPBasicAuth(streamkey, "")
  quota_priority = priority

# HTTP event stream.
class CHSStream(object):
  def __init__(self, timepoint=None):
    self.timepoint = timepoint
    self.session = requests.Session()

  def __iter__(self):
    while True:
      # Send request.
      url = stream_url
      if self.timepoint:
        print("Restart stream from", self.timepoint)
        url = url + "?timepoint=" + str(self.timepoint)
      r = self.session.get(url, auth=stream_credentials, stream=True,
                           timeout=60)
      if r.status_code // 100 == 5:
        # Delay on server error.
        now = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
        print(now, r.status_code, r.reason)
        time.sleep(60)
        continue
      if r.status_code == 416:
        print("Stream expired:", r.status_code, r.reason)
        self.timepoint = None
        time.sleep(10)
        continue

      r.raise_for_status()

      # Receive response stream and break it into messages.
      idle = 0
      for line in r.iter_lines():
        # Ignore keep-alive events.
        if len(line) == 0:
          # Raise error if only keep-alive messages are received.
          idle += 1
          print("idle", idle)
          if idle > 100: raise Exception("Stream inactive")
          continue

        # Parse event.
        data = json.loads(line.decode("utf8"))

        # Get latests timepoint.
        self.timepoint = int(data["event"]["timepoint"])
        print("event", self.timepoint)

        # Return next event.
        yield data

        # Go to next timepoint.
        self.timepoint += 1
        idle = 0

      # Delay before retry.
      time.sleep(60)

# Retrieve data from Companies House with rate limiting.
def retrieve(url):
  # Send query.
  r = apisession.get(url, auth=credentials)

  if r.status_code == 502:
    # Try again on 502 Bad Gateway error.
    print("bad gateway, try again")
    time.sleep(10)
    r = apisession.get(url, auth=credentials)
  elif r.status_code == 401:
    # Keep trying on authorization errors.
    while r.status_code == 401:
      print("auth error", r.headers)
      time.sleep(60)
      r = apisession.get(url, auth=credentials)

  # Check status.
  if r.status_code != 200:
    if r.status_code == 404:
      print("not found:", url)
      return None
    elif r.status_code == 429:
      print("too many requests")
      time.sleep(10)
      return None

    print("Error retrieving", url, ":", r.status_code, r.headers)
    return None

  # Rate limiting.
  global quota_left
  remain = int(r.headers["X-Ratelimit-Remain"])
  quota_left = remain
  if remain < quota_priority:
    reset = int(r.headers["X-Ratelimit-Reset"])
    now = time.time()
    wait = reset - now + 10
    if wait > 0:
      print("throttle", int(wait), "s")
      sys.stdout.flush()
      time.sleep(wait)

  # Return reply.
  return r

# Retrieve pageable list of items.
def retrieve_list(url):
  items = []
  while True:
    # Fetch next batch.
    query = url + "?items_per_page=100"
    if len(items) > 0:
      start = len(items)
      print("fetch from", start)
      query += "&start_index=" + str(start)
    r = retrieve(query)

    # Parse reply.
    if r == None:
      return items if len(items) > 0 else None
    if len(r.text) == 0:
      print("empty reply", r.status_code, r.headers)
      return None
    rsp = json.loads(r.text)
    if "items" not in rsp:
      print("no results")
      return None

    batch = rsp["items"]
    total = rsp["total_results"]
    items.extend(batch)
    if total is None or len(items) >= total: return items

# Check if link is available for company.
def has_link(company, link):
  links = company.get("links")
  if links == None: return False
  return link in links

# Return company URL
def company_url(company):
  if type(company) == dict: company = company["company_number"]
  return url_prefix + "/company/" + company

# Retrieve company information
def retrieve_company(company_no):
  # Fetch company profile.
  r = retrieve(company_url(company_no))
  if r is None: return None
  company = json.loads(r.text)
  return company

# Fetch officers and add them to company profile.
def retrieve_officers(company):
  if has_link(company, "officers"):
    # Fetch list of officers.
    officers = retrieve_list(company_url(company) + "/officers")
    if officers != None: company["officers"] = officers

# Fetch owners and add them to company profile.
def retrieve_owners(company):
  if has_link(company, "persons_with_significant_control"):
    psc = retrieve_list(company_url(company) +
                        "/persons-with-significant-control")
    if psc != None: company["psc"] = psc
