"""
Parse SEC filings from U.S. Securities and Exchange Commission's EDGAR system.
"""

import tarfile
import sling
import re
import sys

line_pat = re.compile(b'<(\/?)([A-Z0-9\-]+)>(.*)\n?')

class Filings:
  def __init__(self, commons, filename):
    self.commons = commons
    self.filename = filename

  def parse(self, store, data):
    slots = []
    n_text = store[b'TEXT']
    while True:
      # Get next line with tag and value.
      line = data.readline()
      if not line: break
      m = line_pat.match(line)
      end = m.group(1)
      tag = store[m.group(2)]
      value = m.group(3)

      # Check for empty value.
      if value == b"": value = None

      if end == b"/":
        # Find corresponding begin tag and create subframe.
        begin = len(slots) - 1
        while slots[begin][0] != tag: begin -= 1
        slots[begin] = (tag, store.frame(slots[begin + 1:]))
        del slots[begin + 1:]
      elif tag == n_text:
        # Get document.
        lines = []
        line = data.readline()
        while line != b"</TEXT>\n":
          lines.append(line);
          line = data.readline()
        doc = b''.join(lines)
        slots.append((tag, doc))
      else:
        # Add slot.
        slots.append((tag, value))

    return store.frame(slots)

  def __iter__(self):
    # Iterate over all files in (compressed) tar file.
    tar = tarfile.open(self.filename, "r")
    for tarinfo in tar:
      if tarinfo.isdir(): continue
      data = tar.extractfile(tarinfo)
      store = sling.Store(self.commons)
      filing = self.parse(store, data)
      yield filing
    tar.close()


if __name__ == "__main__":
  commons = sling.Store()
  commons.freeze()
  for filename in sys.argv[1:]:
    for filing in Filings(commons, filename):
      # Truncate documents.
      submission = filing["SUBMISSION"]
      for doc in submission("DOCUMENT"):
        text = doc["TEXT"]
        if text: doc["TEXT"] = text[:80] + "..." + text[-80:]

      # Print filing.
      print(filing.data(pretty=True))

