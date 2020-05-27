"""
Fetch SEC filings from U.S. Securities and Exchange Commission's EDGAR system.
"""

import datetime
import sys
import requests
import sling.flags as flags

flags.define("--date",
             help="Date for SEC filing batch",
             default=None,
             metavar="YYYY-MM-DD")

flags.define("--earliest",
             help="Earliest date for SEC filings",
             default="1995-09-15",
             metavar="YYYY-MM-DD")

flags.define("--dir",
             help="Directory for SEC filings",
             default="/archive/4/sec",
             metavar="PATH")

flags.define("--backlog",
             help="Number of days to fetch",
             default=1,
             type=int,
             metavar="DAYS")

flags.define("--dryrun",
             help="Display command instead of running them",
             default=False,
             action="store_true")

flags.parse()

dt = datetime.date.fromisoformat(flags.arg.date)
earliest = datetime.date.fromisoformat(flags.arg.earliest)

for _ in range(flags.arg.backlog):
  # Compute URL for date.
  basename = "%04d%02d%02d.nc.tar.gz" % (dt.year, dt.month, dt.day)
  quarter = ((dt.month - 1) // 3) + 1
  url = "https://www.sec.gov/Archives/edgar/Feed/%04d/QTR%d/%s" % (
         dt.year, quarter, basename)
  filename = flags.arg.dir + "/" + basename

  # No SEC fillings during weekend.
  if dt.weekday() < 5:
    # Download file.
    if flags.arg.dryrun:
      print("Fetch", url)
    else:
      print("Downloading", basename)
      r = requests.get(url, stream=True)
      if r.status_code == 403:
        print("Missing:", url)
      else:
        r.raise_for_status()
        with open(filename, 'wb') as f:
          for chunk in r.iter_content(chunk_size=65536):
            f.write(chunk)
  else:
    print("Skipping", basename)

  # Previous day.
  dt -= datetime.timedelta(days=1)
  if dt < earliest: break
  sys.stdout.flush()

print("Done.")

