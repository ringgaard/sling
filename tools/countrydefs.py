# -*- coding: utf-8 -*-

import sling
import sling.task.corpora
import sling.flags as flags
import sys

flags.define("--templ", default=False, action='store_true')

flags.parse()

# Initialize knowledge base.
kb = sling.Store()
kb.load("data/e/kb/kb.sling")
n_country_code = kb["P298"]

# Find all ISO 3166 country codes in the knowledge base.
for item in kb:
  if n_country_code in item:
    code = item[n_country_code]
    if type(code) is sling.Frame: code = code.resolve()
    if flags.arg.templ:
      print '"%s": {type: "text" text: "%s" link: %s}' % (code, item.name, item.id)
    else:
      print '  "' + code + '": ' + item.id

