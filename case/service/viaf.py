# Copyright 2022 Ringgaard Research ApS
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

"""SLING VIAF profile service"""

import re
import requests
import sling
import sling.util

# Mapping from VIAF component to Wikidata property.
viaf_component_mapping = {
  "ARBABN": "P3788",
  "B2Q": "P3280",
  "BAV": "P1017",
  "BAV": "P8034",
  "BIBSYS": "P1015",
  "BLBNB": "P4619",
  "BNCHL": "P7369",
  "BNC": "P1273",
  "BNC": "P9984",
  "BNE": "P950",
  "BNF": "P268",
  "BNL": "P7028",
  "CAOONL": "P8179",
  "CYT": "P10307",
  "CYT": "P1048",
  "DBC": "P3846",
  "DE663": "P5504",
  "DNB": "P227",
  "EGAXA": "P1309",
  "ERRR": "P6394",
  "FAST": "P2163",
  "GeoNames": "P1566",
  "GRATEVE": "P3348",
  "ICCU": "P396",
  "Identities": "P7859",
  "ISNI": "P213",
  "J9U": "P8189",
  "JPG": "P245",
  "KRNLK": "P5034",
  "LAC": "P1670",
  "LC": "P244",
  #"LIH": "P7699",
  "LNB": "P1368",
  "LNL": "P7026",
  "MRBNR": "P7058",
  "N6I": "P10227",
  "NDL": "P349",
  "NII": "P271",
  "NKC": "P691",
  "NLA": "P409",
  "NLB": "P3988",
  "NLI": "P949",
  "NLP": "P1695",
  "NLR": "P7029",
  "NSK": "P1375",
  "NSZL": "P3133",
  "NSZL": "P951",
  "NTA": "P1006",
  "NUKAT": "P1207",
  "NYNYRILM": "P9171",
  "PERSEUS": "P7041",
  "PLWABN": "P7293",
  "PTBNP": "P1005",
  "RERO": "P3065",
  "SELIBR": "P5587",
  "SELIBR": "P906",
  "SIMACOB": "P1280",
  "SKMASNL": "P7700",
  "SRP": "P6934",
  "SUDOC": "P269",
  "SZ": "P227",
  "UAE": "P10021",
  "UIY": "P7039",
  "VLACC": "P7024",
  "W2Z": "P1015",
}

# Initialize commons store.
commons = sling.Store()
n_is = commons["is"]
n_name = commons["name"]
n_alias = commons["alias"]
n_description = commons["description"]

n_instance_of = commons["P31"]
n_human = commons["Q5"]
n_gender = commons["P21"]
n_female = commons["Q6581072"];
n_male = commons["Q6581097"];
n_born = commons["P569"]
n_died = commons["P570"]
n_viaf = commons["P214"];
n_lc = commons["P244"];
n_worldcat = commons["P7859"];

# MARC record format.
n_record = commons["mx:record"]
n_datafield = commons["mx:datafield"]
n_subfield = commons["mx:subfield"]
n_tag = commons["tag"]
n_code = commons["code"]

viaf_components = {}
for name, property in viaf_component_mapping.items():
  viaf_components[name] = commons[property]

commons.freeze()

xref_pat = re.compile(r"^\((\w+)\)(.+)$")
date_pat = re.compile(r"^(\d*)-(\d*)$")

class MARCField:
  def __init__(self, field):
    self.field = field

  def subfield(self, code):
    for sf in self.field(n_subfield):
      if sf[n_code] == code:
        return sf
    return None

  def subfield_text(self, code):
    sf = self.subfield(code)
    if sf is None: return None
    return sf[n_is]

class MARCRecord:
  def __init__(self, record):
    self.datafields = {}
    for field in record(n_datafield):
      tag = field[n_tag]
      if tag not in self.datafields: self.datafields[tag] = []
      self.datafields[tag].append(MARCField(field))

  def field(self, tag):
    f = self.datafields.get(tag)
    if f is None: return None
    return f[0]

  def fields(self, tag):
    f = self.datafields.get(tag)
    if f is None: return []
    return f

class VIAFService:
  def __init__(self):
    # Initialize request session.
    self.session = requests.Session()

  def fetch(self, viafid):
    # Create local store for profile.
    store = sling.Store(commons)
    profile = sling.util.FrameBuilder(store)

    # Fetch MARC-21 record from VIAF.
    r = self.session.get("https://viaf.org/viaf/%s/marc21.xml" % viafid)
    r.raise_for_status()

    # Parse XML.
    marc = store.parse(r.content, xml=True)
    #print(marc.data(pretty=True))
    record = MARCRecord(marc[n_record])

    # Add names and description.
    main_name = None
    description = None
    born = None
    died = None
    xrefs = {}
    for tag in ["700", "710"]:
      for f in record.fields(tag):
        # Names and aliases.
        name = f.subfield_text("a")
        if tag == "700":
          comma = name.find(", ")
          if comma != -1:
            name = name[comma + 2:] + " " + name[:comma]

        if main_name is None:
          main_name = name
          profile.put(n_name, name)
        elif name != main_name:
          profile.put(n_alias, name)

        # Description.
        if description is None:
          description = f.subfield_text("c")

        # Birth and death.
        date = f.subfield_text("d")
        if date is not None:
          m = date_pat.match(date)
          if m is not None:
            if len(m[1]) > 0: born = int(m[1])
            if len(m[2]) > 0: died = int(m[2])

        # References.
        xref = f.subfield_text("0")
        if xref is not None:
          m = xref_pat.match(xref)
          if m is not None:
            reftype = m[1]
            refid = m[2].replace(" ", "")
            if reftype == "WKP":
              prop = n_is;
              refid = refid
            else:
              prop = viaf_components.get(reftype)
            if prop is not None:
              xrefs[prop] = refid
            else:
              print("unknown viaf component", reftype, refid)

    if description is not None:
      profile.put(n_description, description)

    # Add type.
    if record.field("700"):
      profile.put(n_instance_of, n_human)

      # Gender.
      gender = record.field("375")
      if gender is not None:
        g = gender.subfield_text("a")
        if g == "male": profile.put(n_gender, n_male)
        if g == "female": profile.put(n_gender, n_female)

      # Birth and death.
      if born is not None:
        profile.put(n_born, born)
      if died is not None:
        profile.put(n_died, died)

    # Add references.
    profile.put(n_viaf, viafid)
    for prop, value in xrefs.items():
      profile.put(prop, value)
      if prop == n_lc:
        profile.put(n_worldcat, "lccn-" + value)

    # Return profile frame.
    return profile.create()

  def handle(self, request):
    viafid = request.param("id")
    return self.fetch(viafid)

if __name__ == "__main__":
  import sys
  viaf = VIAFService()
  profile = viaf.fetch(sys.argv[1])
  print(profile.data(pretty=True))

