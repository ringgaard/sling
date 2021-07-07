"""
Check for companies that need refresh in a Companies House dump file.
"""

import csv
import zipfile
import json
import io
import os
import sys
import sling
import sling.flags as flags

flags.define("--dump",
             help="Companies House basic company data dump",
             default=None,
             metavar="FILE")

flags.define("--chsdb",
             help="Database for Companies House records",
             default="chs",
             metavar="DB")

flags.define("--output",
             help="Output file with company ids",
             default=None,
             metavar="FILE")

flags.define("--new",
             help="Only output new companies",
             default=False,
             action="store_true")

flags.define("-v",
             help="Output detailed information",
             default=False,
             action="store_true")

flags.define("--max",
             help="Maximum number of companies to output",
             default=999999999,
             type=int,
             metavar="NUM")

flags.parse()

chsdb = sling.Database(flags.arg.chsdb, "chsrefresh")

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
  rec = chsdb[company_no]
  if rec is None: return None
  return json.loads(rec)

# Open output file.
fout = None
if flags.arg.output: fout = open(flags.arg.output, "w")

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
  if flags.arg.v:
    print(num_refresh, "/", num_companies, company_no, company_name, latest)
  else:
    print(company_no)
  sys.stdout.flush()

  if fout: fout.write(company_no + "\n")

  if num_refresh >= flags.arg.max: break

if fout: fout.close()

