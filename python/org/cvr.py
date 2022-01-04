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

"""Convert Danish CVR register to SLING."""

import sys
import sling
import json
import re

# Load KB.
print("Loading KB")
kb = sling.Store()
kb.load("data/e/kb/kb.sling")

n_id = kb["id"]
n_is = kb["is"]
n_name = kb["name"]
n_instance_of = kb["P31"]
n_inception = kb["P571"]
n_dissolved = kb["P576"]
n_start_time = kb["P580"]
n_end_time = kb["P582"]
n_other_name = kb["P2561"]
n_country = kb["P17"]
n_work_location = kb["P937"]
n_located_at_street_address = kb["P6375"]
n_postal_code = kb["P281"]
n_headquarters_location = kb["P159"]
n_municipality_code = kb["P1168"]
n_country_code = kb["P297"]
n_located_in = kb["P131"]
n_country_of_citizenship = kb["P27"]
n_cvr_number = kb["P1059"]
n_cvr_branch_number = kb["P2814"]
n_cvr_person_id = kb["P7972"]
n_inception = kb["P571"]
n_dissolved = kb["P576"]
n_opencorporates_id = kb["P1320"]
n_founded_by = kb["P112"]
n_founder_of = kb["Q65972149"]
n_owned_by = kb["P127"]
n_owner_of = kb["P1830"]
n_external_auditor = kb["P8571"]
n_supervisory_board_member = kb["P5052"]
n_director = kb["P1037"]

n_subsidiary = kb["P355"]
n_parent_organization = kb["P749"]
n_follows = kb["P155"]
n_followed_by = kb["P156"]
n_corporate_officer = kb["P2828"]
n_employer = kb["P108"]
n_legal_form = kb["P1454"]
n_industry = kb["P452"]
n_chief_executive_officer = kb["P169"]
n_director_manager = kb["P1037"]
n_board_member = kb["P3320"]
n_chairperson = kb["P488"]
n_opencorp = kb["P1320"]
n_described_by_source = kb["P1343"]
n_cvr = kb["Q795419"]
n_external_data_available_at = kb["P1325"]

n_organization = kb["Q43229"]
n_business = kb["Q4830453"]
n_foundation = kb["Q157031"]
n_association = kb["Q48204"]
n_human = kb["Q5"]
n_family_name = kb["Q101352"]

aliases = sling.PhraseTable(kb, "data/e/kb/da/phrase-table.repo")
factex = sling.FactExtractor(kb)

"""
# Read registers.
registers = kb.load("../lei/registers.sling")
n_company_property = kb["company_property"]
n_opencorporates_jurisdiction = kb["opencorporates_jurisdiction"]
n_reg_country_code = kb["country_code"]
n_format = kb["format"]
regauth = {}
for register in registers:
  company_property = register[n_company_property]
  opencorp_prefix = register[n_opencorporates_jurisdiction]
  country = register[n_reg_country_code]
  if company_property == None: continue
  if opencorp_prefix == None: continue
  if country == None: continue
  if regauth.get(country) == None: regauth[country] = []
  regauth[country].append(register)
"""

# Read NACE industry codes.
nace_file = open("data/c/sic/nace.txt", "r")
nace = {}
for line in nace_file:
  fields = line.strip().split("|")
  code = fields[0].replace(".", "")
  qid = fields[1]
  if len(qid) == 0:
    nace[code] = None
  else:
    nace[code] = kb[qid]
nace_file.close()

corporate_roles = {
  "TEGNINGSBERETTIGEDE": None,
  "REVISION": n_external_auditor,
  "STIFTERE": n_founded_by,
  "FULDT_ANSVARLIG_DELTAGERE": n_owned_by,
  "LEDELSESORGAN": n_corporate_officer,
  "SÆRLIGE_FINANSIELLE_DELTAGERE": None,
  "HOVEDSELSKAB": n_parent_organization,
  "REGISTER": n_owned_by,
  "FORSIKRINGER": None,
  "HVIDVASK": None,
  "REPRÆSENTANTER": None,
}

corporate_functions = {
  "bestyrelse": n_board_member,
  "formand": n_chairperson,
  "næstformand": n_board_member,
  "bestyrelsesmedlem": n_board_member,
  "adm. dir": n_chief_executive_officer,
  "adm. dir.": n_chief_executive_officer,
  "ordførende direktør": n_chief_executive_officer,
  "medlem": n_board_member,
  "tilsynsråd": n_supervisory_board_member,
  "repræsentantskab": n_board_member,
  "forretningsudvalg": n_board_member,
  "hovedbestyrelse": n_board_member,
  "adm.dir.": n_chief_executive_officer,
  "koncerndirektør": n_chief_executive_officer,
  "præsiditet": n_board_member,
  "adm. direktør": n_chief_executive_officer,
  "konsortierådsmedlem": n_board_member,
  "adm.dir": n_chief_executive_officer,
  "styringskomite": n_supervisory_board_member,
  "overdirektør": n_chief_executive_officer,
  "forretningsudvalgsmedlem": n_board_member,
  "generalsekretær": n_chief_executive_officer,
  "koncernchef": n_chief_executive_officer,
  "administrerende direktør": n_chief_executive_officer,
  "adm.direktør": n_chief_executive_officer,
  "adm.dir:": n_chief_executive_officer,
}

participant_roles = {
  "TEGNINGSBERETTIGEDE": None,
  "REVISION": None,
  "STIFTERE": n_founder_of,
  "FULDT_ANSVARLIG_DELTAGERE": n_owner_of,
  "LEDELSESORGAN": n_employer,
  "SÆRLIGE_FINANSIELLE_DELTAGERE": None,
  "HOVEDSELSKAB": n_subsidiary,
  "REGISTER": n_owner_of,
  "FORSIKRINGER": None,
  "HVIDVASK": None,
  "REPRÆSENTANTER": None,
}

legal_forms = {
  10: (kb["Q2912172"], n_business, "Enkeltmandsvirksomhed"),
  15: (kb["Q2912172"], n_business, "Personligt ejet mindre virksomhed"),
  20: (None, kb["Q11035463"], "Dødsbo"),
  30: (kb["Q728646"], n_business ,"Interessentskab"),
  40: (kb["Q165758"], n_business ,"Kommanditselskab"),
  45: (kb["Q26702423"], n_business, "Medarbejderinvesteringsselskab"),
  50: (kb["Q17165668"],n_business, "Partrederi"),
  60: (kb["Q2624520"], n_business, "Aktieselskab"),
  70: (kb["Q12331028"],n_business, "Kommanditaktieselskab/partnerskab"),
  80: (kb["Q15649047"], n_business, "Anpartsselskab"),
  81: (kb["Q18586645"], n_business, "Iværksætterselskab"),
  90: (kb["Q157031"], n_foundation, "Fond"),
  95: (kb["Q854022"], n_organization, "Trust"),
  100: (kb["Q157031"], n_business, "Erhvervsdrivende fond"),
  110: (kb["Q48204"], n_association, "Forening"),
  115: (kb["Q48204"], n_association, "Frivillig forening"),
  130: (kb["Q4539"], n_business, "Andelsselskab(-forening)"),
  140: (kb["Q12301349"], n_business, "Andelsselskab med begrænset ansvar"),
  150: (kb["Q12301349"], n_organization, "Forening/selskab med begrænset ansvar"),
  151: (kb["Q12334948"], n_business, "Selskab med begrænset ansvar"),
  152: (kb["Q48204"], n_association, "Forening med begrænset ansvar"),
  160: (kb["Q1377654"], n_business, "Europæisk økonomisk firmagruppe"),
  170: (kb["Q2624520"], kb["Q658255"], "Filial af udl. aktieselskelsbab"),
  180: (kb["Q18624259"], kb["Q658255"], "Filial af udenlandsk anpartsselskab eller selskab"),
  190: (kb["Q33685"], kb["Q658255"], "Filial af udenlandsk virksomhed med begrænset ansvar"),
  195: (kb["Q1109804"], n_business, "SCE-selskab"),
  196: (kb["Q1109804"], kb["Q658255"], "Filial af udenlandsk SCE-selskab"),
  200: (None, kb["Q658255"], "Filial af anden udenlandsk virksomhedsform"),
  210: (None, n_business, "Anden udenlandsk virksomhed"),
  220: (kb["Q1377654"], n_business, "Fast forretningssted af Europæisk økonomisk firmagruppe"),
  230: (None, kb["Q2659904"], "Statslig administrativ enhed"),
  235: (None, kb["Q1421490"], "Selvstændig offentlig virksomhed"),
  240: (None, kb["Q18199255"], "Amtskommune"),
  245: (None, kb["Q62326"], "Region"),
  250: (None, kb["Q2177636"], "Primærkommune"),
  260: (None, kb["Q1530022"], "Folkekirkelig institution"),
  270: (None, None, "Enhed under oprettelse i Erhvervs- og Selskabsstyrelsen"),
  280: (None, n_organization, "Øvrige virksomhedsformer"),
  285: (kb["Q650241"], n_business, "Særlig finansiel virksomhed"),
  290: (kb["Q279014"], n_business, "SE-selskab"),
  291: (kb["Q279014"], kb["Q658255"], "Filial af SE-selskab"),
  520: (None, kb["Q658255"], "Grønlandsk afdeling af udenlandsk selskab eller virksomhed"),
  990: (None, n_business, "Uoplyst virksomhedform"),
}

given_names = factex.taxonomy([
  "Q202444",  # given name
])

person_names = factex.taxonomy([
  "Q101352", # family name
  "Q202444",  # given name
])

demonyms = {
  "afghansk": kb["Q889"],
  "algier": kb["Q262"],
  "amerikansk statsborger": kb["Q30"],
  "amerikansk statsborgerskab": kb["Q30"],
  "amerikansk (usa)": kb["Q30"],
  "bahamansk": kb["Q778"],
  "bahraini": kb["Q398"],
  "bangladeshi": kb["Q902"],
  "belgian": kb["Q31"],
  "boliviansk": kb["Q750"],
  "brasilliansk": kb["Q155"],
  "british citizen": kb["Q145"],
  "britisk statsborger": kb["Q145"],
  "bulgarisk": kb["Q219"],
  "bulgarsk": kb["Q219"],
  "cambodian": kb["Q424"],
  "chilensk": kb["Q298"],
  "chinese": kb["Q148"],
  "cypriotisk": kb["Q229"],
  "cypriot": kb["Q229"],
  "deutsch": kb["Q183"],
  "dutch": kb["Q55"],
  "egyptian": kb["Q79"],
  "egyptisk": kb["Q79"],
  "emirati": kb["Q878"],
  "engelsk": kb["Q21"],
  "england": kb["Q21"],
  "equador": kb["Q736"],
  "estisk": kb["Q191"],
  "estonian": kb["Q191"],
  "etiopisk": kb["Q115"],
  "filippiner": kb["Q928"],
  "filippinsk": kb["Q928"],
  "fillipiner": kb["Q928"],
  "finish": kb["Q30Q33"],
  "finnish": kb["Q33"],
  "finsk": kb["Q30Q33"],
  "francaise": kb["Q142"],
  "french": kb["Q142"],
  "gambian": kb["Q1005"],
  "georgiansk": kb["Q230"],
  "georgisk": kb["Q230"],
  "ghanaian": kb["Q117"],
  "graesk": kb["Q41"],
  "græsk": kb["Q41"],
  "great britain": kb["Q145"],
  "haitianer": kb["Q790"],
  "hellenic": kb["Q41"],
  "hollandsk": kb["Q55"],
  "hollandsk statsborgerskab": kb["Q55"],
  "hollansk": kb["Q55"],
  "hungarian": kb["Q28"],
  "inder": kb["Q668"],
  "indian": kb["Q668"],
  "irakisk": kb["Q796"],
  "iraner": kb["Q794"],
  "iransk": kb["Q794"],
  "irish": kb["Q27"],
  "irlandsk": kb["Q27"],
  "irlandsk": kb["Q27"],
  "irsk": kb["Q27"],
  "islænding": kb["Q189"],
  "islandisk": kb["Q189"],
  "islandsk": kb["Q189"],
  "israeler": kb["Q801"],
  "italiana": kb["Q38"],
  "italian": kb["Q38"],
  "italiener": kb["Q38"],
  "italliensk": kb["Q38"],
  "jamaicansk": kb["Q766"],
  "japansk": kb["Q17"],
  "jordish": kb["Q810"],
  "kazakhstand": kb["Q232"],
  "kenyansk": kb["Q114"],
  "kineser": kb["Q148"],
  "korea": kb["Q884"],
  "koreansk": kb["Q884"],
  "kroatisk": kb["Q224"],
  "kypriotisk": kb["Q229"],
  "latvijas": kb["Q211"],
  "lebanese": kb["Q822"],
  "lebanesisk": kb["Q822"],
  "letisk": kb["Q211"],
  "letlandsk": kb["Q211"],
  "lette": kb["Q211"],
  "lettisk": kb["Q211"],
  "libaneser": kb["Q822"],
  "libyan": kb["Q1016"],
  "liechtensteiner": kb["Q347"],
  "lietuva": kb["Q211"],
  "litauer": kb["Q37"],
  "litauiske": kb["Q37"],
  "litauisk": kb["Q37"],
  "lithauisk": kb["Q37"],
  "lithuanian": kb["Q37"],
  "makedonsk": kb["Q221"],
  "malaisisk": kb["Q833"],
  "malaysian": kb["Q833"],
  "maltesisk": kb["Q233"],
  "marokansk": kb["Q1028"],
  "morakkansk": kb["Q1028"],
  "nederlandse": kb["Q55"],
  "nederlandsk": kb["Q55"],
  "nepali": kb["Q837"],
  "netherland": kb["Q55"],
  "new york, usa": kb["Q30"],
  "newzealandsk": kb["Q664"],
  "new zeeland": kb["Q664"],
  "nigeriansk": kb["Q1033"],
  "norsk statsborgerskab": kb["Q20"],
  "norwegian": kb["Q20"],
  "østrisk": kb["Q40"],
  "peruviansk": kb["Q419"],
  "polak": kb["Q36"],
  "polska": kb["Q36"],
  "polskie": kb["Q36"],
  "portugese": kb["Q45"],
  "portugisisk": kb["Q45"],
  "republic of belarus": kb["Q184"],
  "republic of korea": kb["Q884"],
  "romanian": kb["Q218"],
  "rumænsk": kb["Q218"],
  "rumænsk": kb["Q218"],
  "rusisk": kb["Q159"],
  "russian": kb["Q159"],
  "russisk": kb["Q159"],
  "schweisisk": kb["Q39"],
  "schwiez": kb["Q39"],
  "serbisk": kb["Q403"],
  "singaporiansk": kb["Q334"],
  "slovakisk": kb["Q214"],
  "slovak": kb["Q214"],
  "slovensk": kb["Q215"],
  "spanioler": kb["Q29"],
  "spanish": kb["Q29"],
  "storbritaninen": kb["Q145"],
  "storbritannnien": kb["Q145"],
  "svejtsisk": kb["Q39"],
  "svendsk": kb["Q34"],
  "svensk statsborgerskab": kb["Q34"],
  "swedish": kb["Q34"],
  "swiss": kb["Q39"],
  "sydkoreaner": kb["Q884"],
  "taiwanesisk": kb["Q865"],
  "tawanesisk": kb["Q865"],
  "thailandsk": kb["Q869"],
  "tjekkisk": kb["Q213"],
  "tunesisk": kb["Q948"],
  "tyrker": kb["Q43"],
  "ugandisk": kb["Q1036"],
  "uk": kb["Q145"],
  "ukrainian": kb["Q212"],
  "ukrainsk": kb["Q212"],
  "ungarsk": kb["Q28"],
  "united states": kb["Q30"],
  "united states of american": kb["Q30"],
  "uruguiansk": kb["Q77"],
  "venezuelaner": kb["Q717"],
  "venezuelansk": kb["Q717"],
  "østrisk": kb["Q40"],
}

no_citizenship = set([
  "Ikke registreret",
  "Ikke angivet",
  "Ukendt", "ukendt",
  "Uoplyst",
])

# Build country and municipality table.
country_map = {}
municipality_map = {}
countries = set()
for item in kb:
  code = item[n_country_code]
  if code != None:
    if type(code) is sling.Frame: code = code.resolve()
    country_map[code] = item
    countries.add(item)

  code = item[n_municipality_code]
  if code != None:
    if type(code) is sling.Frame: code = code.resolve()
    municipality_map[code] = item

kb.freeze()

class FrameBuilder:
  def __init__(self):
    self.slots = []

  def __setitem__(self, key, value):
    self.slots.append((key, value))

  def add(self, key, value):
    self.slots.append((key, value))

  def create(self, store):
    return store.frame(self.slots)

def is_person_name(name):
  first = True
  for token in name.split(" "):
    if token in ["AB", "AS"]: return False
    match = False
    if first:
      for m in aliases.lookup(token):
        if given_names.classify(m) != None: match = True
    else:
      for m in aliases.lookup(token):
        if person_names.classify(m) != None: match = True
    if not match: return False
    first = False
  return True

def get_country(name):
  if name == None: return None
  country = demonyms.get(name.lower())
  if country != None: return country
  for m in aliases.lookup(name):
    if m in countries: return m
  return None

def located_in(location, region):
  if region == None: return True
  closure = [location]
  i = 0
  while i < len(closure):
    location = closure[i]
    if location == region: return True
    if location is not None:
      for l in location(n_located_in):
        if l not in closure: closure.append(l)
    i += 1
  return False

def location_in(name, region):
  for m in aliases.lookup(name):
    if located_in(m, region): return m
  return region

def get_attribute(doc, key):
  attributes = doc.get("attributter")
  if attributes == None: return None
  for attr in attributes:
    if attr["type"] == key:
      for value in attr["vaerdier"]:
        v = value.get("vaerdi")
        if v != None: return v
  return None

# Convert date from YYYY-MM-DD to SLING format.
def get_date(s):
  if s == None or len(s) == 0: return None
  year = int(s[0:4])
  month = int(s[5:7])
  day = int(s[8:10])
  return year * 10000 + month * 100 + day

def convert_address(rec, addr):
  # Country.
  place = country_map.get(rec["landekode"])

  text = rec["fritekst"]
  municipality = rec["kommune"]
  addrlines = []
  if text != None:
    # Free-text address.
    text = text.replace("\r\n", "\n")
    text = text.replace(" ,\n", "\n")
    text = text.replace(",\n", "\n")
    text = text.replace("  ", "\n")
    addrlines = text.split("\n")
  elif municipality != None:
    # Municipality.
    code = municipality["kommuneKode"]
    if code != None:
      location = municipality_map.get(str(code))
      if location != None: place = location

    # Postal district.
    post_district = rec["postdistrikt"]
    if post_district != None:
      place = location_in(post_district, place)

    # City.
    city_name = rec["bynavn"]
    if city_name != None:
      place = location_in(city_name, place)

  # Address lines.
  if rec["conavn"] != None:
    careof = rec["conavn"]
    if careof.lower().startswith("c/o"):
      addrlines.append(careof)
    else:
      addrlines.append("c/o " + careof)
  if rec["postboks"] != None: addrlines.append("Postboks " + rec["postboks"])
  if rec["vejnavn"] != None:
    street = rec["vejnavn"]
    if rec["husnummerFra"] != None:
      street += " " + str(rec["husnummerFra"])
    if rec["husnummerTil"] != None:
      street += "-" + str(rec["husnummerTil"])
    if rec["bogstavFra"] != None:
      street += rec["bogstavFra"]
    if rec["bogstavTil"] != None:
      street += rec["bogstavTil"]
    if rec["etage"] != None:
      street += " " + rec["etage"] + "."
    if rec["sidedoer"] != None:
      street += " " + rec["sidedoer"]
    addrlines.append(street)
  if rec["bynavn"] != None:
    addrlines.append(rec["bynavn"])
  if rec["postnummer"] != None and rec["postdistrikt"] != None:
    addrlines.append(str(rec["postnummer"]) + " " + rec["postdistrikt"])

  # Compile address.
  if place != None: addr.add(n_is, place)

  if len(addrlines) > 0:
    lines = []
    for l in addrlines:
      l = l.strip()
      if len(l) > 0: lines.append(l)
    addr.add(n_located_at_street_address, ", ".join(lines))

  postal_code = rec["postnummer"]
  if postal_code != None: addr.add(n_postal_code, postal_code)

  # Add period.
  period = rec.get("periode")
  start = get_date(period.get("gyldigFra"))
  end = get_date(period.get("gyldigTil"))
  if end != None:
    if start != None: addr[n_start_time] = start
    addr[n_end_time] = end

def match_company_id(country, company_id):
  regs = regauth.get(country)
  if regs != None:
    for register in regs:
      format = register[n_format]
      if format == None: continue
      if re.match(format + "$", company_id) != None: return register
  return None

def find_company_id(country, id):
  reg = match_company_id(country, id)
  if reg != None: return reg, id

  # Remove text before colon.
  colon = id.find(":")
  if colon != -1:
    id = id[colon + 1:].strip()
    reg = match_company_id(country, id)
    if reg != None: return reg, id

  # Remove country code prefix.
  if id.startswith(country):
    id = id[len(country):].strip()
    reg = match_company_id(country, id)
    if reg != None: return reg, id

  # Remove spaces.
  if " " in id:
    id = id.replace(" ", "")
    reg = match_company_id(country, id)
    if reg != None: return reg, id

  # Replace dots with dashes.
  if "." in id:
    id = id.replace(".", "-")
    reg = match_company_id(country, id)
    if reg != None: return reg, id

  # Remove dashes
  if "-" in id:
    id = id.replace("-", "")
    reg = match_company_id(country, id)
    if reg != None: return reg, id

  # Add leading zero
  id = "0" + id
  reg = match_company_id(country, id)
  if reg != None: return reg, id

  return None, None

print("Convert CVR data")
cvrdb = sling.Database("vault/cvr")
recout = sling.RecordWriter("data/e/org/cvr.rec")

num_entities = 0
num_persons = 0
num_companies = 0
num_branches = 0
num_other = 0
num_nationality = 0
num_nocitizenship = 0
occupations = {}

for key, rec in cvrdb.items():
  num_entities += 1
  if num_entities % 10000 == 0:
    print(num_entities, "entities")
    sys.stdout.flush()
  #if num_entities == 1000000: break

  # Parse JSON record.
  data = json.loads(rec)
  store = sling.Store(kb)
  entity = FrameBuilder()

  # Determine entity type.
  person = False
  unittype = data.get("enhedstype")
  if unittype == "PERSON":
    # Get CVR person number.
    cvrpid = str(data["enhedsNummer"])
    entity[n_id] = "P7972/" + cvrpid
    entity.add(n_instance_of, n_human)
    entity.add(n_cvr_person_id, cvrpid)

    person = True
    num_persons += 1
  elif unittype == "VIRKSOMHED":
    # Get CVR number
    cvrnr = str(data["cvrNummer"])
    entity[n_id] = "P1059/" + cvrnr
    entity.add(n_cvr_number, cvrnr)
    entity.add(n_opencorporates_id, "dk/" + cvrnr)

    # Legal entity form.
    for form in data["virksomhedsform"]:
      code = form["virksomhedsformkode"]
      orgtype = legal_forms.get(code)
      if orgtype != None:
        if orgtype[1] != None:
          entity.add(n_instance_of, orgtype[1])
        if orgtype[0] != None:
          entity.add(n_legal_form, orgtype[0])
      else:
        print("unknown legal form:", key, code)

    num_companies += 1
  elif unittype == "PRODUKTIONSENHED":
    pnr = str(data["pNummer"])
    entity[n_id] = "P2814/" + pnr
    entity.add(n_cvr_branch_number, pnr)
    num_branches += 1
  elif unittype == "ANDEN_DELTAGER":
    cvrpid = str(data["enhedsNummer"])
    entity[n_id] = "P7972/" + cvrpid
    num_other += 1
  else:
    print("Unknown entity type:", unittype)
    continue

  # Get entity names.
  names = []
  for n in data["navne"]:
    name = n.get("navn")
    if name == None or name == "Ukendt": continue
    name = " ".join(name.split())
    period = n.get("periode")
    start = get_date(period.get("gyldigFra"))
    end = get_date(period.get("gyldigTil"))
    names.append((start, end, name))
  if len(names) == 1:
    entity.add(n_name, names[0][2])
  else:
    first = True
    for n in sorted(names, key=lambda x: x[2], reverse=True):
      if first:
        entity.add(n_name, n[2])
      else:
        alias = FrameBuilder()
        alias[n_is] = n[2]
        if n[0] != None: alias[n_start_time] = n[0]
        if n[1] != None: alias[n_end_time] = n[1]
        entity.add(n_other_name, alias.create(store))
      first = False
  subnames = data.get("binavne")
  if subnames != None and len(subnames) > 0:
    names = set()
    for n in subnames: names.add(n["navn"])
    for name in names: entity.add(n_other_name, name)

  # Occupation.
  occupation = data.get("stilling")
  if occupation != None:
    #occupation = occupation.lower()
    #occupations[occupation] = occupations.get(occupation, 0) + 1
    pass

  # Citizenship.
  id_type = get_attribute(data, "IDENTIFIKATION_TYPE")
  citizenship = None
  if id_type == "PASNUMMER" or id_type == "TINNUMMER":
    nationality = get_attribute(data, "OPRINDELIGT_STATSBORGERSKAB")
    if nationality != None and nationality not in no_citizenship:
      citizenship = get_country(nationality)
      if citizenship != None:
       num_nationality += 1

    if citizenship == None:
      country_code = get_attribute(data, "IDENTIFIKATION_LANDEKODE")
      country = country_map.get(country_code)
      if country != None:
        citizenship = country

  if citizenship != None:
    entity.add(n_country_of_citizenship, citizenship)
  elif person:
    num_nocitizenship += 1

  # Foreign companies.
  """
  if id_type == "UDENLANDSK REGISTRERINGSNUMMER":
    country_code = get_attribute(data, "IDENTIFIKATION_LANDEKODE")
    auth = get_attribute(data, "IDENTIFIKATION_MYNDIGHED")
    company_id = get_attribute(data, u"IDENTIFIKATION_VÆRDI")
    if country_code != None and company_id != None:
      reg, id = find_company_id(country_code, company_id)
      if reg != None:
        company_property = reg[n_company_property]
        opencorp_prefix = reg[n_opencorporates_jurisdiction]
        if company_property != None:
          comp_id = company_property.id + "/" + id
          entity.add(company_property, id)
        if opencorp_prefix != None:
          opencorp_id = opencorp_prefix + "/" + id
          entity.add(n_opencorporates_id, opencorp_id)

      else:
        print(docid, "country:", country_code, "id:", company_id)
  """

  # Lifespan.
  lifespan = data.get("livsforloeb")
  inception = None
  dissolved = None
  if lifespan != None:
    closed = True
    for l in lifespan:
      period = l.get("periode")
      start = get_date(period.get("gyldigFra"))
      end = get_date(period.get("gyldigTil"))
      if start != None:
        if inception == None or start < inception: inception = start
      if end != None:
        if dissolved == None or end > dissolved: dissolved = end
      else:
        closed = False

    if not closed: dissolved = None

  if inception != None:
    entity.add(n_inception, inception)
  if dissolved != None:
    entity.add(n_dissolved, dissolved)

  # Industry.
  main_industries = data.get("hovedbranche")
  if main_industries != None:
    industries = set()
    for min in main_industries:
      dkcode = min["branchekode"]
      industry = nace.get(dkcode[0:4])
      if industry == None:
        #print("Unknown industry", cvrno, dkcode)
        continue
      industries.add(industry)
    for industry in industries:
      entity.add(n_industry, industry)

  # Relations (company->person).
  relations = data.get("deltagerRelation")
  if relations != None:
    for r in relations:
      participant = r["deltager"]
      if participant == None: continue
      target = participant.get("enhedsNummer")

      for o in r["organisationer"]:
        maintype = o["hovedtype"]
        function = None
        start = None
        end = None
        for m in o["medlemsData"]:
          for a in m["attributter"]:
            t = a["type"]
            if t == "FUNKTION" or t == "EJERANDEL_PROCENT":
              for v in a["vaerdier"]:
                function = v["vaerdi"].lower()
                period = v["periode"]
                start = get_date(period["gyldigFra"])
                end = get_date(period["gyldigTil"])
        if start == inception: start = None
        if end == dissolved: end = None
        relation = corporate_roles[maintype]
        if relation == n_corporate_officer:
          role = corporate_functions.get(function)
          if role != None: relation = role
        if relation != None:
          if start != None or end != None:
            f = FrameBuilder()
            f[n_is] = store["P7972/" + str(target)]
            if start != None: f[n_start_time] = start
            if end != None: f[n_end_time] = end
            entity.add(relation, f.create(store))
          else:
            entity.add(relation, store["P7972/" + str(target)])

  # Relations (person->company).
  relations = data.get("virksomhedSummariskRelation")
  if relations != None:
    for r in relations:
      company = r["virksomhed"]
      if company == None: continue
      target = str(company.get("cvrNummer"))

      for o in r["organisationer"]:
        maintype = o["hovedtype"]
        function = None
        start = None
        end = None
        for m in o["medlemsData"]:
          for a in m["attributter"]:
            t = a["type"]
            if t == "FUNKTION" or t == "EJERANDEL_PROCENT":
              for v in a["vaerdier"]:
                function = v["vaerdi"]
                period = v["periode"]
                start = get_date(period["gyldigFra"])
                end = get_date(period["gyldigTil"])
        relation = participant_roles[maintype]
        if relation != None:
          if start != None or end != None:
            f = FrameBuilder()
            f[n_is] = store["P1059/" + str(target)]
            if start != None: f[n_start_time] = start
            if end != None: f[n_end_time] = end
            entity.add(relation, f.create(store))
          else:
            entity.add(relation, store["P1059/" + str(target)])

  # Mergers.
  fusions = data.get("fusioner")
  if fusions != None and len(fusions) > 0:
    for f in fusions:
      orgno = f["enhedsNummerOrganisation"]
      entity.add(n_followed_by, store["P1059/" + str(orgno)])

  # Splits.
  splits = data.get("spaltninger")
  if splits != None and len(splits) > 0:
    for s in splits:
      orgno = s["enhedsNummerOrganisation"]
      entity.add(n_follows, store["P1059/" + str(orgno)])

  # Address.
  for address in data["beliggenhedsadresse"]:
    addr = FrameBuilder()
    convert_address(address, addr)

    f = addr.create(store)
    if person:
      entity.add(n_work_location, f)
    else:
      entity.add(n_headquarters_location, f)

  # Source.
  entity.add(n_described_by_source, n_cvr)
  entity.add(n_external_data_available_at,  "http://vault:7070/cvr/" + key)

  # Write item frame for entity.
  f = entity.create(store)
  recout.write(f.id, f.data(binary=True))

for o in occupations:
  print("%6d %s" % (occupations[o], o))

print(num_nationality, "with nationality")
print(num_nocitizenship, "without citizenship")
print(num_entities, "entities")
print(num_companies, "companies")
print(num_persons, "persons")
print(num_branches, "branches")
print(num_other, "other participants")

recout.close()
cvrdb.close()

