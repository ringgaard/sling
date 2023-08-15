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

"""Convert PDF to epub."""

import re
import zipfile
import fitz

import sling.flags as flags

flags.define("--input",
             help="PDF input file",
             metavar="FILE")

flags.define("--output",
             help="EPUB output file",
             metavar="FILE")

flags.define("--toc",
             help="table of contents for dividing into chapters",
             default=None,
             metavar="FILE")

flags.define("--indent",
             help="line indention",
             default=5.0,
             type=float,
             metavar="NUM")

flags.define("--debug",
             help="output debug information",
             default=False,
             action="store_true")

flags.parse()

escapes = [
  ("<", "&#60;"),
  (">", "&#62;"),
  ("{", "&#123;"),
  ("|", "&#124;"),
  ("}", "&#125;"),
  ("[", "&#91;"),
  ("]", "&#93;"),
]

def escape(s):
  for text, rep in escapes:
    s = s.replace(text, rep)
  return s

def mean(values):
  if len(values) == 0: return 0.0
  outliers = int(len(values) / 10)
  center = sorted(values)[:-outliers][outliers:]
  if len(center) == 0: center = values
  return sum(center) / len(center)

class PDFLine:
  def __init__(self, page, block):
    self.page = page
    self.block = block
    self.text = ""
    self.x0 = None
    self.y0 = None
    self.x1 = None
    self.y1 = None
    self.hyphen = False
    self.indent = False
    self.short = False
    self.para = False

  def add(self, word, x0, y0, x1, y1):
    # Update bounding box.
    if self.x0 is None or x0 < self.x0: self.x0 = x0
    if self.y0 is None or y0 < self.y0: self.y0 = y0
    if self.x1 is None or x1 > self.x1: self.x1 = x1
    if self.y1 is None or y1 > self.y1: self.y1 = y1

    # Soft-break hyphen.
    last = ord(word[-1])
    if last == 173:
      self.hyphen = True
      word = word[:-1]

    # Add word to line.
    if len(self.text) > 0: self.text += " "
    self.text += word

  def first(self):
    if len(self.text) == 0: return ""
    return self.text[0]

  def last(self):
    if len(self.text) == 0: return ""
    return self.text[-1]

  def capital(self):
    if len(self.text) == 0: return 0
    first = self.first()
    return int(first.isupper()) - 2 * int(first.islower())

  def period(self):
    return self.last() == "."

class PDFPage:
  def __init__(self, book):
    self.book = book
    self.pageno = None
    self.chapter = None
    self.lines = []

  def extract(self, page):
    words = page.get_textpage().extractWORDS()
    current_blockno = None
    current_lineno = None
    line = None
    for w in words:
      blockno = w[5]
      lineno = w[6]
      if blockno != current_blockno or current_lineno != lineno:
        line = PDFLine(self, blockno)
        self.lines.append(line)
        current_blockno = blockno
        current_lineno = lineno
      line.add(w[4], w[0], w[1], w[2], w[3])

  def linestart(self):
    if len(self.lines) == 0: return 0.0;
    values = []
    for l in self.lines: values.append(l.x0);
    return mean(values)

  def lineend(self):
    if len(self.lines) == 0: return 0.0;
    values = []
    for l in self.lines: values.append(l.x1);
    return mean(values)

  def lineheight(self):
    if len(self.lines) == 0: return 0.0;
    values = []
    prev = None
    for l in self.lines:
      if prev: values.append(l.y0 - prev.y0);
      prev = l
    return mean(values)

  def analyze(self, prev=None):
    left = self.linestart() + flags.arg.indent
    right = self.lineend() - flags.arg.indent
    height = self.lineheight() + flags.arg.indent
    for l in self.lines:
      # Mark indented and short lines.
      if l.x0 > left: l.indent = True
      if l.x1 < right: l.short = True

      # Fix up soft hyphens.
      if prev and prev.last() == "-":
        if not prev.short and not l.indent and l.capital() < 0:
          prev.hyphen = True
          prev.text = prev.text[:-1]

      # Check for paragraph break.
      if prev:
        points = 0
        if prev.short: points += 1
        if prev.hyphen: points -= 1
        if l.indent: points += 1
        #if prev.period(): points += 1
        #if prev.block != l.block and (prev.short or prev.period()): points += 1
        if l.y0 - prev.y0 > height: points += 1
        points += l.capital()
        if points > 1: l.para = True
      prev = l

class PDFChapter:
  def __init__(self, pageno, title, index):
    self.pageno = pageno
    self.title = title
    self.pages = []
    self.filename = "chapter-%03d.html" % index
    self.ref = "item%03d" % index

  def generate(self):
    s = []
    s.append('<?xml version="1.0" encoding="UTF-8"?>\n')
    s.append('<html xmlns="http://www.w3.org/1999/xhtml">\n')
    s.append('<head>\n')
    s.append('<title>%s</title>\n' % self.title)
    s.append('<meta charset="UTF-8"/>\n')
    s.append('</head>\n')
    s.append('<body>\n')

    s.append('<p>')
    for p in self.pages:
      for l in p.lines:
        if l.para: s.append('</p>\n<p>')
        s.append(escape(l.text))
        if not l.hyphen: s.append(' ')

    s.append('</p>\n</body>\n')
    s.append('</html>\n')
    return "".join(s)

meta_fields = [
  ("title", "dc:title", ''),
  ("author", "dc:creator", ' opf:role="aut"'),
  ("published", "dc:date", ''),
  ("publisher", "dc:publisher", ''),
  ("language", "dc:language", ''),
  ("isbn", "dc:identifier", ' opf:scheme="ISBN"'),
]

class PDFBook:
  def __init__(self):
    self.pages = []
    self.spine = []
    self.chapters = {}
    self.meta = {}

  def read_toc(self, filename):
    with open(filename) as f:
      self.toc = []
      index = 1
      for line in f.read().split("\n"):
        line = line.strip()
        if len(line) == 0: continue
        if line[0] == '!':
          colon = line.index(':')
          key = line[1:colon].strip()
          value = line[colon + 1:].strip()
          self.meta[key] = value
        elif ' ' in line:
          last_space = line.rindex(' ')
          name = line[0:last_space].strip()
          pageno = int(line[last_space:].strip())
          chapter = PDFChapter(pageno, name, index)
          index += 1
          self.spine.append(chapter)
          self.chapters[pageno] = chapter

  def extract(self, pdf):
    for p in pdf:
      page = PDFPage(self)
      self.pages.append(page)
      page.extract(p)

  def analyze_boilerplate(self):
    foot_lines = int(self.meta.get("footlines", "1"))
    footer_positions = []
    body_positions = []
    for p in book.pages:
      if len(p.lines) < foot_lines + 1: continue
      last = p.lines[-foot_lines]
      penultimate = p.lines[-foot_lines - 1]
      footer_positions.append(last.y0)
      body_positions.append(penultimate.y1)

    body_end = mean(body_positions)
    foot_start = mean(footer_positions)
    foot_sep = (foot_start + body_end) / 2
    print("footer: %d [%d-%d]" %
      (int(foot_sep), int(body_end), int(foot_start)))
    if "footer" not in self.meta: self.meta["footer"] = str(foot_sep)

  def remove_boilerplate(self):
    titles = set()
    if "title" in self.meta: titles.add(self.meta["title"].lower())
    for chapter in self.spine: titles.add(chapter.title.lower())

    footer = float(self.meta.get("footer", "0"))
    pageno = 1
    for p in book.pages:
      if footer:
        cut = None
        lineno = 0
        for l in p.lines:
          if l.y0 > footer:
            if cut is None: cut = lineno
            num = None
            m = re.search(r"(\d+)", l.text)
            if m:
              num = int(m[1])
              if num != pageno:
                print("expected page", pageno, ", got", num)
                pageno = num
            elif l.text.lower() not in titles:
              print("page", pageno, "footer", l.text)
          lineno += 1
        if cut: p.lines = p.lines[:cut]
      p.pageno = pageno
      pageno += 1

  def analyze(self):
    prev = None
    chapter = None
    for p in self.pages:
      p.chapter = self.chapters.get(p.pageno)
      if p.chapter: chapter = p.chapter
      if chapter: chapter.pages.append(p)

      p.analyze(prev)
      if len(p.lines) == 0:
        prev = None
      else:
        prev = p.lines[-1]

  def generate(self, epubfn):
    zip = zipfile.ZipFile(epubfn, "w")
    zip.writestr("mimetype", "application/epub+zip")

    # Write container.
    container = [
      '<?xml version="1.0" encoding="UTF-8"?>',
      '<container version="1.0" ' +
        'xmlns="urn:oasis:names:tc:opendocument:xmlns:container">',
      '<rootfiles>',
      ' <rootfile full-path="content.opf" ' +
        'media-type="application/oebps-package+xml"/>',
      '</rootfiles>',
      '</container>',
    ]
    zip.writestr("META-INF/container.xml", "\n".join(container))

    # Write package file.
    package = [
      '<?xml version="1.0" encoding="utf-8"?>',
      '<package xmlns="http://www.idpf.org/2007/opf" version="2.0">',
    ]

    package.append('<metadata ' +
      'xmlns:dc="http://purl.org/dc/elements/1.1/" ' +
      'xmlns:opf="http://www.idpf.org/2007/opf">')
    for mf in meta_fields:
      value = self.meta.get(mf[0])
      if value:
        package.append("<%s%s>%s</%s>" % (mf[1], mf[2], value, mf[1]))
    package.append('</metadata>')

    package.append('<manifest>')
    for chapter in book.spine:
      package.append('<item href="%s" id="%s" '
        % (chapter.filename, chapter.ref) +
        'media-type="application/xhtml+xml"/>')
    package.append('<item href="toc.ncx" id="ncx" ' +
      'media-type="application/x-dtbncx+xml"/>')
    package.append('</manifest>')

    package.append('<spine toc="ncx">')
    for chapter in book.spine:
      package.append('<itemref idref="%s"/>' % chapter.ref)
    package.append('</spine>')

    package.append('</package>')
    zip.writestr("content.opf", "\n".join(package))

    # Write table of content.
    toc = [
      '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">',
      '<navMap>',
    ]
    for chapter in book.spine:
      toc.append('<navPoint>')
      toc.append('<navLabel><text>%s</text></navLabel>' % chapter.title)
      toc.append('<content src="%s"/>' % chapter.filename)
      toc.append('</navPoint>')
    toc.append('</navMap>')
    toc.append('</ncx>')
    zip.writestr("toc.ncx", "\n".join(toc))

    # Write chapters.
    for chapter in book.spine:
      zip.writestr(chapter.filename, chapter.generate())

    zip.close()

book = PDFBook()
if flags.arg.toc: book.read_toc(flags.arg.toc)
pdf = fitz.open(flags.arg.input)
book.extract(pdf)
book.analyze_boilerplate()
book.remove_boilerplate()
book.analyze()
if flags.arg.output: book.generate(flags.arg.output)

if flags.arg.debug:
  for p in book.pages:
    if p.chapter: print("*** CHAPTER:", p.chapter.title)
    print("=== page", p.pageno)
    for l in p.lines:
      attrs = []
      if l.indent: attrs.append(">")
      if l.short: attrs.append("<")
      if l.hyphen: attrs.append("(-)")
      if l.para: print()
      print(l.text, " ".join(attrs))

