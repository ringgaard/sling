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

"""Convert epub books to LEX format."""

import io
import re
import zipfile

import sling
import sling.log as log

commons = sling.Store()

n_data = commons["is"]
n_id = commons["_id"]
n_container = commons["container"]
n_rootfiles = commons["rootfiles"]
n_rootfile = commons["rootfile"]
n_full_path = commons["full-path"]
n_package = commons["package"]
n_metadata = commons["metadata"]
n_meta = commons["meta"]
n_name = commons["name"]
n_content = commons["content"]
n_manifest = commons["manifest"]
n_item = commons["item"]
n_href = commons["href"]
n_spine = commons["spine"]
n_toc = commons["toc"]
n_itemref = commons["itemref"]
n_idref = commons["idref"]
n_toc = commons["toc"]

n_title = commons["P1476"]
n_author = commons["P50"]
n_publisher = commons["P123"]
n_pubdate = commons["P577"]
n_language = commons["P407"]
n_isbn13 = commons["P212"]
n_isbn10 = commons["P957"]
n_lex = commons["lex"]
n_name = commons["name"]
n_is = commons["is"]

n_dc_title = commons["dc:title"]
n_dc_creator = commons["dc:creator"]
n_dc_publisher = commons["dc:publisher"]
n_dc_date = commons["dc:date"]
n_dc_description = commons["dc:description"]
n_dc_language = commons["dc:language"]
n_dc_identifier = commons["dc:identifier"]
n_opf_schema = commons["opf:scheme"]
n_opf_role = commons["opf:role"]

n_ncx = commons["ncx"]
n_navmap = commons["navMap"]
n_navpoint = commons["navPoint"]
n_navlabel = commons["navLabel"]
n_content = commons["content"]
n_src = commons["src"]
n_text = commons["text"]

n_html = commons["html"]
n_head = commons["head"]
n_body = commons["body"]
n_title = commons["title"]
n_class = commons["class"]
n_role = commons["role"]

languages = {
  None: None,
  "en": commons["Q1860"],
  "eng": commons["Q1860"],
  "en-US": commons["Q1860"],
  "en-GB": commons["Q1860"],
  "da": commons["Q9035"],
  "dan": commons["Q9035"],
}

KEEP    = 1
SKIP    = 2
NOTAG   = 3
BREAK   = 4
NEWLINE = 5

tags = {
  commons["body"]: NOTAG,
  commons["section"]: KEEP,
  commons["p"]: {
    None: NEWLINE,
  },
  commons["h1"]: KEEP,
  commons["h2"]: KEEP,
  commons["h3"]: KEEP,
  commons["h4"]: KEEP,
  commons["h5"]: KEEP,
  commons["br"]: BREAK,
  commons["em"]: KEEP,
  commons["b"]: KEEP,
  commons["i"]: KEEP,
  commons["strong"]: KEEP,
  commons["small"]: KEEP,
  commons["sup"]: KEEP,
  commons["sub"]: KEEP,
  commons["div"]: KEEP,
  commons["ol"]: KEEP,
  commons["ul"]: KEEP,
  commons["li"]: KEEP,
  commons["span"]: {
    None: KEEP,
    "doc-pagebreak": SKIP,
  },
  commons["hr"]: {
    None: KEEP,
    "empty-line": BREAK,
  },
  commons["a"]: KEEP,
  commons["title"]: KEEP,
  commons["blockquote"]: KEEP,
  commons["table"]: KEEP,
  commons["tr"]: KEEP,
  commons["td"]: KEEP,


  commons["style"]: SKIP,
  commons["figure"]: SKIP,
  commons["img"]: SKIP,
  commons["svg"]: SKIP,
  commons["xmlns"]: SKIP,
  commons["xmlns:xlink"]: SKIP,
  commons["xlink:href"]: SKIP,
  commons["version"]: SKIP,
  commons["width"]: SKIP,
  commons["height"]: SKIP,
  commons["viewBox"]: SKIP,
  commons["preserveAspectRatio"]: SKIP,
  commons["preserveAspectRatio"]: SKIP,
  commons["image"]: SKIP,
  commons["class"]: SKIP,
  commons["_id"]: SKIP,
  commons["epub:type"]: SKIP,
  commons["aria-label"]: SKIP,
  commons["aria-labelledby"]: SKIP,
  commons["role"]: SKIP,
  commons["href"]: SKIP,
  commons["hidden"]: SKIP,
  commons["xmlU0003Alang"]: SKIP,
}

title_tags = [
  commons["h1"],
  commons["h2"],
  commons["h3"],
]

commons.freeze()

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

def detag(s):
  if s is None: return None
  return re.sub(r"<\/?[^>]+(>|$)", "", s)

def content(element):
  if element is None: return None
  if type(element) is str: return escape(element)
  s = []
  for child in element(n_data): s.append(child)
  if len(s) == 0: return None
  return "".join(s).strip()

class EPUBBook:
  def __init__(self, file):
    self.zip = zipfile.ZipFile(file, "r")
    self.store = sling.Store(commons)
    self.frame = self.store.frame([])
    self.title = None

  def __del__(self):
    self.zip.close()

  def read(self, filename):
    zf = self.zip.open(filename)
    data = zf.read()
    zf.close()
    return data

  def add(self, name, value):
    if value is not None: self.frame.append(name, value)

  def extract(self):
    # Get root file with book metadata, manifest, and spine.
    container = self.store.parse(self.read("META-INF/container.xml"), xml=True)
    rootfn = container[n_container][n_rootfiles][n_rootfile][n_full_path]
    root = self.store.parse(self.read(rootfn), xml=True, idsym=n_id)
    package = root[n_package]
    #print("root", root.data(pretty=True, utf8=True))

    # Get base path.
    base = ""
    lastslash = rootfn.rfind("/")
    if lastslash != -1: base = rootfn[:lastslash + 1]

    # Collect meta data.
    metadata = package[n_metadata]
    if type(metadata) is sling.Frame:
      self.title = content(metadata[n_dc_title])
      self.add(n_title, self.title)

      creator = metadata[n_dc_creator]
      if type(creator) is str:
        self.add(n_author, creator)
      elif type(creator) is sling.Frame:
        role = creator[n_opf_role]
        if role is None or role == "aut":
          self.add(n_author, content(creator))

      self.add(n_publisher, content(metadata[n_dc_publisher]))
      self.add(n_language, languages.get(metadata[n_dc_language]))

      pubdate = self.store.resolve(metadata[n_dc_date])
      if "T" in pubdate: pubdate = pubdate[0:pubdate.find("T")]
      if pubdate: self.add(n_pubdate, int(pubdate.replace("-", "")))

      description = metadata[n_dc_description]
      if description: self.add(None, re.sub(r"\s+", " ", detag(description)))

      isbn = None
      ident = content(metadata[n_dc_identifier])
      if type(ident) is str:
        if ident.startswith("urn:isbn:"): isbn = ident[9:]
      elif type(ident) is sling.Frame:
        schema = ident[n_opf_scema]
        if schema == "ISBN": isbn = content(ident)
      if isbn:
        if len(isbn) == 13:
          self.add(n_isbn13, isbn)
        else:
          self.add(n_isbn10, isbn)

    # Collect manifest.
    manifest = package[n_manifest]
    refs = {}
    for item in manifest(n_item):
      refs[item[n_id]] = base + item[n_href]

    # Read table of content.
    spine = package[n_spine]
    tocref = spine[n_toc]
    titles = {}
    if tocref:
      tocfn = refs[tocref]
      toc = self.store.parse(self.read(tocfn), xml=True, idsym=n_id)
      #print("toc", toc.data(pretty=True, utf8=True))
      for nav in toc[n_ncx][n_navmap](n_navpoint):
        label = nav[n_navlabel][n_text]
        reffn = base + nav[n_content][n_src]
        frag = reffn.find("#")
        if frag != -1: reffn = reffn[0:frag]
        titles[reffn] = label
        #print("toc", label, "fn", reffn)

    # Process sections in spine.
    for section in spine(n_itemref):
      ref = section[n_idref]
      itemfn = refs[ref]
      label = titles.get(itemfn)
      #print("====", itemfn)
      item = self.store.parse(self.read(itemfn), xml=True, idsym=n_id)
      #print("item", item.data(pretty=True, utf8=True))
      title, text = self.extract_section(item)
      if label: title = label

      #print("title", title)
      #print(text)

      self.add(n_lex, self.store.frame([(n_name, title), (n_is, text)]))

    return self.frame

  def extract_section(self, section):
    html = section[n_html]
    head = html[n_head]
    body = html[n_body]

    # Try to find title.
    title = None
    if head:
      title = content(head[n_title])
      if title and self.title and self.title.lower().startswith(title.lower()):
        title = None

    if title is None:
      for tag in title_tags:
        heading = body[tag]
        if heading:
          title = detag(self.extract_text(tag, heading))

    # Extract text from body.
    text = self.extract_text(n_body, body)

    return title, text

  def extract_text(self,  tag, content):
    #print(tag.id, content)
    text = ""
    action = tags.get(tag)
    if action is None:
      log.warning("Unknown tag:", tag.id)
      action = KEEP
    elif type(action) is dict:
      cls = None
      if type(content) is sling.Frame:
        cls = content[n_class]
        if cls is None: cls = content[n_role]
      if cls in action:
        action = action[cls]
      else:
        action = action[None]

    if action == SKIP: return ""
    if action == BREAK: return "<br>"

    #print("tag", tag.id, action)

    if content is None:
      text = ""
    elif type(content) is str:
      text = escape(content)
    else:
      for subtag, child in content:
        if subtag == n_data:
          text += escape(child)
        else:
          text += self.extract_text(subtag, child)

    if action == NOTAG: return text
    if len(text) == 0: return ""
    if action == NEWLINE:
      return "\n<%s>%s</%s>" % (tag.id, text, tag.id)
    else:
      return "<%s>%s</%s>" % (tag.id, text, tag.id)

def extract(content, params):
   file = io.BytesIO(content)
   book = EPUBBook(file)
   return book.extract()

if __name__ == "__main__":
  import sling.flags as flags

  flags.define("--input", help="epub input file", metavar="FILE")
  flags.parse()

  book = EPUBBook(flags.arg.input)
  frame = book.extract()
  print(frame.data(pretty=True, utf8=True))

