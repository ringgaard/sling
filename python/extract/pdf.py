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

"""Convert PDF to LEX format."""

import fitz

import sling.extract.pdf2epub as pdf2epub
import sling
import sling.log as log

commons = sling.Store()

n_lex = commons["lex"]

commons.freeze()

def extract(content, params):
  book = pdf2epub.PDFBook()

  pdfparams = params.get("pdfparams")
  if pdfparams:
    print("PDF params:", pdfparams)
    for line in pdfparams.split(";"):
      colon = line.index(':')
      key = line[:colon].strip()
      value = line[colon + 1:].strip()
      book.meta[key] = value

  pdf = fitz.open("pdf", content)
  book.extract(pdf)
  book.analyze_boilerplate()
  book.remove_boilerplate()
  book.analyze()
  book.add_single_book_chapter()

  content = book.collect()

  store = sling.Store(commons)
  return store.frame([(n_lex, content)])
