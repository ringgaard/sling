# Copyright 2025 Ringgaard Research ApS
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

""" Extract text from PDFs in case file and output items for indexing."""

import fitz
import requests

import sling
import sling.flags as flags
import sling.extract.pdf2epub as pdf2epub

flags.define("--casedb",
             default="case",
             help="case database")

flags.define("--caseno",
             default=None,
             help="case number")

flags.define("--plaintext",
             help="output plaintext in documents",
             default=False,
             action='store_true')

flags.define("--output",
             default=None,
             help="record output file")

flags.parse()

store = sling.Store()
n_topics = store["topics"]
n_url = store["P2699"]
n_lex = store["lex"]
n_text = store["text"]

# Fetch case from case database.
casedb = sling.Database(flags.arg.casedb)
data = casedb[flags.arg.caseno]
casefile = store.parse(data)

# Extract text from PDF files.
output = sling.RecordWriter(flags.arg.output, index=True)
for topic in casefile[n_topics]:
  url = store.resolve(topic[n_url])
  if url is not None and url.endswith(".pdf"):
    print(topic.id, topic.name, url)

    # Fetch PDF.
    r = requests.get(url)
    mag = pdf2epub.PDFBook()
    pdf = fitz.open(stream=r.content)
    mag.extract(pdf)
    mag.analyze_boilerplate()
    mag.remove_boilerplate()
    mag.analyze()
    mag.add_single_book_chapter()
    content = mag.collect()
    topic[n_lex] = content

    # Extract plain text.
    if flags.arg.plaintext:
      doc = sling.lex(content)
      plain = doc.phrase(0, len(doc.tokens))
      topic[n_text] = plain

  output.write(topic.id, topic.data(binary=True))

output.close()
