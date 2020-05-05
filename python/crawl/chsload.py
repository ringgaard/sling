"""
Load data from Companies House from a dump file or an id file.
"""

import requests
import time
import json
import sys
import sling.flags as flags
import sling.crawl.chs as chs

flags.define("--ids",
             help="File with list of company numbers to load",
             default=None,
             metavar="FILE")

flags.define("--chskeys",
             help="Companies House API key file",
             default="local/keys/chs.txt",
             metavar="FILE")

flags.define("--chsdb",
             help="database for storing Companies House records",
             default="http://compute02.jbox.dk:7070/chs",
             metavar="DBURL")

flags.define("--dbmode",
             help="Mode for database updates",
             default="add",
             metavar="MODE")

flags.define("--skip",
             help="Skip company number before",
             default=None,
             metavar="COMPANYNO")

flags.define("--priority",
             help="CHS query quota priority",
             default=200,
             type=int,
             metavar="NUM")

flags.parse()

# Initialize credentials
chs.init(flags.arg.chskeys, flags.arg.priority)
dbsession = requests.Session()

# Look up company in database.
def lookup_company(company_no):
  r = dbsession.get(flags.arg.chsdb + "/" + company_no)
  if r.status_code == 200:
    return json.loads(r.text)
  elif r.status_code == 404:
    return None
  else:
    r.raise_for_status()

# Save company profile in database.
def save_company(company_no, company):
  r = dbsession.put(
    flags.arg.chsdb + "/" + company_no,
    json=company,
    headers={"Mode": flags.arg.dbmode}
  )
  r.raise_for_status()
  return r.headers["Result"]

# Read company numbers form file.
num_companies = 0
num_companies_fetched = 0
fin = open(flags.arg.ids)
skipping = flags.arg.skip != None
for line in fin:
  company_no = line.strip()
  num_companies += 1

  # Check if company should be skipped.
  if skipping:
    if company_no != flags.arg.skip: continue
    skipping = False

  # Check if company is already in database.
  company = lookup_company(company_no)
  if company != None and flags.arg.dbmode == "add": continue

  # Fetch company profile from Companies House.
  company = chs.retrieve_company(company_no)
  if company == None:
    print(company_no, "not found")
    continue
  chs.retrieve_officers(company)
  chs.retrieve_owners(company)

  # Save company in database.
  save_company(company_no, company)

  num_companies_fetched += 1
  print(num_companies_fetched, "/", num_companies, company_no,
        company.get("company_name"))
  sys.stdout.flush()

fin.close()
print("Done.")

