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

flags.define("--bprdebug",
             help="output boiler plate debug information",
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
  ("&", "&#38;"),
  ("〈", "<"),
  ("〉", ">"),
  ("</b><b>", ""),
  ("</b> <b>", " "),
  ("</em><em>", ""),
  ("</em> <em>", " "),
]

LEVEL_START       = 0
LEVEL_PARA        = 1
LEVEL_SUBHEADING  = 2
LEVEL_HEADING     = 3

FONT_SUPERSCRIPT  = 1
FONT_ITALIC       = 2
FONT_SANS         = 4
FONT_MONO         = 8
FONT_BOLD         = 16

level_start = ["", "<p>", "\n<h2>", "\n<h1>"]
level_end = ["", "</p>\n", "</h2>\n\n", "</h1>\n"]

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
  def __init__(self, page, line):
    self.page = page
    self.hyphen = False
    self.indent = False
    self.short = False
    self.para = False
    self.vbreak = False
    self.flags = 0

    # Bounding box.
    bbox = line["bbox"]
    self.x0 = bbox[0]
    self.y0 = bbox[1]
    self.x1 = bbox[2]
    self.y1 = bbox[3]

    # Text.
    book = page.book
    self.text = ""
    fontsizes = []
    for span in line["spans"]:
      word = span["text"]
      fontsize = span["size"]
      flags = span["flags"]
      #print(word, int(fontsize), span["font"], span["flags"])

      # Skip reference numbers.
      if fontsize < book.refsize and word.isnumeric():
        refno = int(word)
        if refno > book.nextref and refno < book.nextref + 5:
          if refno == book.nextref + 2:
            print("missing refno", book.nextref)
          elif refno != book.nextref + 1:
            print("missing refnos [%d-%d]" % (book.nextref, refno - 1))

          #print("ref", word, fontsize, span["font"], span["flags"])
          book.nextref = refno
          continue

      # Font variants.
      if word != " ":
        if flags & FONT_BOLD: word = "〈b〉%s〈/b〉" % word
        if flags & FONT_ITALIC: word = "〈em〉%s〈/em〉" % word

      # Add text to line
      self.text += word
      self.flags |= (flags ^ 4)
      fontsizes.append(fontsize)

    self.fontsize = mean(fontsizes)

    # Soft-break hyphen.
    if len(self.text) > 0 and ord(self.text[-1]) == 173:
      self.hyphen = True
      self.text = self.text[:-1]

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

  def find_page_number(self, expected):
    best = None
    for m in re.findall(r"\d+", self.text):
      num = int(m)
      if best is None or abs(num - expected) <= abs(best - expected):
        best = num
    return best

class PDFPage:
  def __init__(self, book):
    self.book = book
    self.pageno = None
    self.chapter = None
    self.lines = []

  def extract(self, page):
    p = page.get_textpage().extractDICT(sort=False)
    for b in p["blocks"]:
      if b["type"] == 1:
        if flags.arg.debug: print("skip image")
        continue
      for l in b["lines"]:
        # Skip text that is not vertical.
        dir = l["dir"]
        if dir[0] != 1.0 or dir[1] != 0.0: continue
        line = PDFLine(self, l)
        self.lines.append(line)

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

  def linewidth(self):
    if len(self.lines) == 0: return 0.0;
    values = []
    for l in self.lines: values.append(l.x1 - l.x0);
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
    parbreak = self.book.param("parbreak", 0)
    if parbreak == 0:
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
          if prev.text.endswith("."): points += 1
          if l.indent: points += 1
          if l.y0 - prev.y0 > height: points += 1
          points += l.capital()
          if points > 1: l.para = True
        prev = l
    elif parbreak == 1:
      # Multi-column
      height = self.lineheight() + flags.arg.indent
      width = self.linewidth() - flags.arg.indent
      for l in self.lines:
        # Mark short lines.
        if l.x1 - l.x0 < width: l.short = True

        # Fix up soft hyphens.
        if prev and prev.last() == "-":
          if not prev.short and l.capital() < 0:
            prev.hyphen = True
            prev.text = prev.text[:-1]

        # Vertical breaks.
        if l.text.startswith("〈b〉"): l.vbreak = True
        if prev and l.y0 - prev.y0 > height: l.vbreak = True

        # Check for paragraph break.
        if prev:
          points = 0
          if prev.short: points += 1
          if prev.hyphen: points -= 1
          if prev.text.endswith("."): points += 1
          if l.vbreak: points += 1
          if points > 1: l.para = True
        prev = l


class PDFChapter:
  def __init__(self, pageno, title, index):
    self.pageno = pageno
    self.title = title
    self.pages = []
    self.filename = "chapter-%03d.html" % index
    self.ref = "item%03d" % index

  def generate(self, h1, h2):
    s = []
    s.append('<?xml version="1.0" encoding="UTF-8"?>\n')
    s.append('<html xmlns="http://www.w3.org/1999/xhtml">\n')
    s.append('<head>\n')
    s.append('<title>%s</title>\n' % self.title)
    s.append('<meta charset="UTF-8"/>\n')
    s.append('</head>\n')
    s.append('<body>\n')

    level = LEVEL_START
    for p in self.pages:
      for l in p.lines:
        next = level
        if l.fontsize > h1:
          next = LEVEL_HEADING
        elif l.fontsize > h2:
          next = LEVEL_SUBHEADING
        elif l.para:
          next = LEVEL_PARA

        if next != level or l.para:
          s.append(level_end[level])
          if l.vbreak: s.append("\n")
          s.append(level_start[next])
          level = next

        s.append(escape(l.text))
        if not l.hyphen: s.append(' ')

    s.append(level_end[level])
    s.append('</body>\n')
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
    self.nextref = 0
    self.refsize = 0

  def read_toc(self, filename):
    last_toc_pageno = 0
    with open(filename) as f:
      self.toc = []
      index = 1
      for line in f.read().split("\n"):
        line = line.strip()
        if len(line) == 0 or line[0] == ';': continue
        if line[0] == '!':
          colon = line.index(':')
          key = line[1:colon].strip()
          value = line[colon + 1:].strip()
          if key == "subst":
            escapes.append(tuple(value.split("/")))
          else:
            self.meta[key] = value
        elif ' ' in line:
          last_space = line.rindex(' ')
          name = line[0:last_space].strip()
          pageno = int(line[last_space:].strip())
          chapter = PDFChapter(pageno, name, index)
          if pageno < last_toc_pageno:
            print("TOC ordering warning:", line)
          last_toc_pageno = pageno
          index += 1
          self.spine.append(chapter)
          self.chapters[pageno] = chapter

  def param(self, name, defval=None):
    val = self.meta.get(name)
    if val is None: return defval
    if type(defval) is int: return int(val)
    if type(defval) is float: return float(val)
    return val

  def extract(self, pdf):
    self.refsize = self.param("refsize", 0)
    ignore = [int(p) for p in self.param("ignore", "0").split(",")]
    pnum = 0;
    for p in pdf:
      pnum += 1
      if pnum in ignore: continue
      page = PDFPage(self)
      self.pages.append(page)
      page.extract(p)

  def analyze_boilerplate(self):
    head_lines = self.param("headlines", 0)
    foot_lines = self.param("footlines", 1)
    header_positions = []
    top_positions = []
    bottom_positions = []
    footer_positions = []

    for p in book.pages:
      if len(p.lines) < head_lines + foot_lines + 1: continue
      if head_lines > 0:
        head_last = p.lines[head_lines - 1]
        body_first = p.lines[head_lines]
        header_positions.append(head_last.y1)
        top_positions.append(body_first.y0)
      if foot_lines > 0:
        body_last = p.lines[-foot_lines - 1]
        foot_first = p.lines[-foot_lines]
        bottom_positions.append(body_last.y1)
        footer_positions.append(foot_first.y0)

    if head_lines > 0:
      head_end = mean(header_positions)
      body_start = mean(top_positions)
      head_sep = (head_end + body_start) / 2
      print("header: %d [%d-%d]" %
        (int(head_sep), int(head_end), int(body_start)))
      if "header" not in self.meta: self.meta["header"] = str(head_sep)

    if foot_lines > 0:
      body_end = mean(bottom_positions)
      foot_start = mean(footer_positions)
      foot_sep = (foot_start + body_end) / 2
      print("footer: %d [%d-%d]" %
         (int(foot_sep), int(body_end), int(foot_start)))
      if "footer" not in self.meta: self.meta["footer"] = str(foot_sep)

  def remove_boilerplate(self):
    header = self.param("header", 0.0)
    footer = self.param("footer", 0.0)
    bpr = self.param("bpr", 0)
    pageno = self.param("firstpage", 1)
    pageskip = self.param("pageskip", 10)
    for p in book.pages:
      pnumber = None

      if bpr == 0:
        # Remove header and footers and try to find page number.
        lineno = 0
        for l in p.lines:
          in_header = header > 0 and l.y1 < header
          in_footer = footer > 0 and l.y0 > footer

          best_pageno = l.find_page_number(pageno)
          if in_header:
            p.lines[lineno] = None
            pnumber = best_pageno
            if flags.arg.bprdebug: print("remove header:", l.text)
          elif in_footer:
            p.lines[lineno] = None
            pnumber = best_pageno
            if flags.arg.bprdebug: print("remove footer:", l.text)
          elif flags.arg.bprdebug:
            if best_pageno and abs(best_pageno - pageno) <= 1:
              print("page number in text: %d '%s' [%d-%d]" %
                    (pageno, l.text, int(l.y0), int(l.y1)))

          lineno += 1
      elif bpr == 1:
        # Remove last line if it matches the expected page number.
        if len(p.lines) > 0:
          pnumber = p.lines[-1].find_page_number(pageno)
          if pnumber == pageno:
            p.lines[-1] = None
          elif flags.arg.debug:
            if pnumber is None:
              print("missing pageno: %d '%s'" %
                    (pageno, p.lines[-1].text))
            else:
              print("wrong pageno: %d '%s' %d" %
                    (pageno, p.lines[-1].text, pnumber))
      elif bpr == 2:
        # Assume consecutive page numbers, remove first/last line if they
        # match the expected page number.
        if len(p.lines) > 0:
          if p.lines[0].find_page_number(pageno) == pageno:
            p.lines[0] = None
          elif p.lines[-1].find_page_number(pageno) == pageno:
            p.lines[-1] = None
          elif flags.arg.debug:
            print("no pageno found:", pageno)


      p.lines = [l for l in p.lines if l is not None]

      if pnumber is not None:
        if pnumber < pageno or pnumber > pageno + pageskip:
          print("ignore page no", pnumber, ", expected", pageno)
        elif pnumber != pageno:
          print("expected page", pageno, ", got", pnumber)
          pageno = pnumber

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
    h1 = self.param("h1", 32.0)
    h2 = self.param("h2", 24.0)
    for chapter in book.spine:
      zip.writestr(chapter.filename, chapter.generate(h1, h2))

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
      if l.fontsize: attrs.append(str(round(l.fontsize)))
      if l.flags: attrs.append("{%d}" % l.flags)
      if l.para: print()
      print(l.text, " ".join(attrs))

