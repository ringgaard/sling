"""
Check for companies that need refresh in a Companies House dump file.
"""

import csv
import zipfile
import requests
import json
import io
import os
import sling.flags as flags

flags.define("--dump",
             help="Companies House basic company data dump",
             default=None,
             metavar="FILE")

flags.define("--chsdb",
             help="Database for Companies House records",
             default="http://compute02.jbox.dk:7070/chs",
             metavar="DBURL")

flags.define("--new",
             help="Only output new companies",
             default=False,
             action="store_true")

flags.parse()

dbsession = requests.Session()

# Convert date from DD/MM/YYYY or YYYY-MM-DD to SLING format.
def get_date(s):
  if len(s) == 0: return None
  if s[2] == "/" and s[5] == "/":
    year = int(s[6:10])
    month = int(s[3:5])
    day = int(s[0:2])
  else:
    year = int(s[0:4])
    month = int(s[5:7])
    day = int(s[8:10])
  return year * 10000 + month * 100 + day

# Look up company in database.
def lookup_company(company_no):
  r = dbsession.get(flags.arg.chsdb + "/" + company_no)
  if r.status_code == 200:
    return json.loads(r.text)
  elif r.status_code == 404:
    return None
  else:
    r.raise_for_status()

# Open company data file.
dump = zipfile.ZipFile(flags.arg.dump, "r")
fn = os.path.basename(flags.arg.dump).replace(".zip", ".csv")
reader = csv.reader(io.TextIOWrapper(
                      dump.open(fn, "r"),
                      encoding="utf8"))

# Skip field names in first row.
reader.__next__()

# Iterate over all companies in dump.
num_companies = 0
num_refresh = 0
for row in reader:
  company_name = row[0]
  company_no = row[1]
  num_companies += 1

  # Check if company is already in database.
  company = lookup_company(company_no)

  # Skip if company found and only new companies is output.
  if flags.arg.new and company != None: continue

  # Check if company needs to be refreshed.
  latest = get_date(row[54])
  if company != None:
    if latest == None: continue
    if "confirmation_statement" not in company: continue
    confstmt = company["confirmation_statement"]
    if "last_made_up_to" not in confstmt: continue
    current = get_date(confstmt["last_made_up_to"])
    if current >= latest: continue

  # Output company number.
  num_refresh += 1
  print(num_refresh, "/", num_companies, company_no, company_name, latest)

