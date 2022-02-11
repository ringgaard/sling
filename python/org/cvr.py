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
n_description = kb["description"]
n_instance_of = kb["P31"]
n_inception = kb["P571"]
n_dissolved = kb["P576"]
n_start_time = kb["P580"]
n_end_time = kb["P582"]
n_point_in_time = kb["P585"]
n_other_name = kb["P2561"]
n_country = kb["P17"]
n_residence = kb["P551"]
n_work_location = kb["P937"]
n_located_at_street_address = kb["P6375"]
n_postal_code = kb["P281"]
n_headquarters_location = kb["P159"]
n_municipality_code = kb["P1168"]
n_country_code = kb["P297"]
n_located_in = kb["P131"]
n_phone_number = kb["P1329"]
n_email_address = kb["P968"]
n_official_website = kb["P856"]
n_country_of_citizenship = kb["P27"]
n_cvr_number = kb["P1059"]
n_cvr_branch_number = kb["P2814"]
n_cvr_person_id = kb["P7972"]
n_cvr_unit_number = kb["PCVR"]
n_inception = kb["P571"]
n_dissolved = kb["P576"]
n_opencorporates_id = kb["P1320"]
n_founded_by = kb["P112"]
n_founder_of = kb["Q65972149"]
n_owned_by = kb["P127"]
n_owner_of = kb["P1830"]
n_position_held = kb["P39"]
n_external_auditor = kb["P8571"]
n_supervisory_board_member = kb["P5052"]
n_occupation = kb["P106"]
n_director = kb["P1037"]
n_subsidiary = kb["P355"]
n_parent_organization = kb["P749"]
n_has_part = kb["P527"]
n_part_of = kb["P361"]
n_replaces = kb["P1365"]
n_replaced_by = kb["P1366"]
n_merged_into = kb["P7888"]
n_separated_from = kb["P807"]
n_corporate_officer = kb["P2828"]
n_employer = kb["P108"]
n_legal_form = kb["P1454"]
n_industry = kb["P452"]
n_chief_executive_officer = kb["P169"]
n_director_manager = kb["P1037"]
n_board_member = kb["P3320"]
n_chairperson = kb["P488"]
n_manager = kb["Q2462658"]
n_business_manager = kb["Q832136"]
n_opencorp = kb["P1320"]
n_described_by_source = kb["P1343"]
n_nace_code = kb["P4496"]
n_cvr = kb["Q795419"]
n_organization = kb["Q43229"]
n_business = kb["Q4830453"]
n_foundation = kb["Q157031"]
n_association = kb["Q48204"]
n_human = kb["Q5"]
n_family_name = kb["Q101352"]

n_denmark = kb["Q35"]
n_danish = kb["/lang/da"]
n_copenhagen = kb["Q1748"]
n_frederiksberg = kb["Q30096"]

aliases = sling.PhraseTable(kb, "data/e/kb/da/phrase-table.repo")
factex = sling.FactExtractor(kb)

# Read Danish postal codes.
fin = open("data/org/dkpostnr.txt")
postalcodes = {}
for postnr in range(1000, 1800): postalcodes[postnr] = n_copenhagen
for postnr in range(1800, 2000): postalcodes[postnr] = n_frederiksberg

for line in fin:
  line = line.strip()
  if len(line) == 0: continue;
  fields = line.split(",")
  postnr = fields[0]
  qid = fields[2]
  postalcodes[int(postnr)] = kb[qid]
fin.close()


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
  "adm. direktør": n_chief_executive_officer,
  "adm.direktør": n_chief_executive_officer,
  "adm. dir.": n_chief_executive_officer,
  "adm. dir": n_chief_executive_officer,
  "adm.dir:": n_chief_executive_officer,
  "adm.dir.": n_chief_executive_officer,
  "adm.dir": n_chief_executive_officer,
  "administrerende direktør": n_chief_executive_officer,
  "bestyrelse": n_board_member,
  "bestyrelsesmedlem": n_board_member,
  "daglig ledelse": n_manager,
  "formand": n_chairperson,
  "forretningsfører": n_business_manager,
  "forretningsudvalg": n_board_member,
  "forretningsudvalgsmedlem": n_board_member,
  "generalsekretær": n_chief_executive_officer,
  "hovedbestyrelse": n_board_member,
  "koncernchef": n_chief_executive_officer,
  "koncerndirektør": n_chief_executive_officer,
  "konsortierådsmedlem": n_board_member,
  "medlem": n_board_member,
  "næstformand": n_board_member,
  "ordførende direktør": n_chief_executive_officer,
  "overdirektør": n_chief_executive_officer,
  "præsiditet": n_board_member,
  "repræsentantskab": n_board_member,
  "styringskomite": n_supervisory_board_member,
  "tilsynsråd": n_supervisory_board_member,
}

corporate_position = {
  "adm. direktør": kb["Q484876"],
  "adm.direktør": kb["Q484876"],
  "adm. dir.": kb["Q484876"],
  "adm. dir": kb["Q484876"],
  "adm.dir:": kb["Q484876"],
  "adm.dir.": kb["Q484876"],
  "adm.dir": kb["Q484876"],
  "administrerende direktør": kb["Q484876"],
  "bestyrelse": kb["Q2824523"],
  "bestyrelsesmedlem": kb["Q2824523"],
  "direktion": kb["Q1162163"],
  "direktør": kb["Q1162163"],
  "filialbestyrere": kb["Q4956387"],
  "formand": kb["Q140686"],
  "forretningsudvalg": kb["Q2824523"],
  "forretningsudvalgsmedlem": kb["Q2824523"],
  "generalsekretær": kb["Q484876"],
  "hovedbestyrelse": kb["Q2824523"],
  "koncernchef": kb["Q484876"],
  "koncerndirektør": kb["Q484876"],
  "konsortierådsmedlem": kb["Q2824523"],
  "kasserer": kb["Q388338"],
  "likvidator": kb["Q108559404"],
  "likvidator iht. vedtægt": kb["Q108559404"],
  "medlem": kb["Q2824523"],
  "næstformand": kb["Q1127270"],
  "ordførende direktør": kb["Q484876"],
  "overdirektør": kb["Q484876"],
  "personlig_suppleant": kb["Q3504856"],
  "præsiditet": kb["Q2824523"],
  "repræsentantskab": kb["Q2824523"],
  "styringskomite": kb["Q63858690"],
  "suppleant": kb["Q3504856"],
  "tilsynsråd": kb["Q63858690"],
  "vice administrerende direktør": kb["Q64154210"],
  "økonomidirektør": kb["Q623268"],
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

occupations = factex.taxonomy([
  "Q12737077",  # occupation
  "Q192581",    # job
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

class Merger:
  def __init__(self, date, split):
    self.date = date
    self.split = split
    self.incoming = []
    self.outgoing = []

mergers = {}

def resolve(f):
  if type(f) is sling.Frame: f = f.resolve()
  return f

# Build country and municipality table.
country_map = {}
municipality_map = {}
countries = set()
for item in kb:
  code = resolve(item[n_country_code])
  if code is not None:
    country_map[code] = item
    countries.add(item)

  code = resolve(item[n_municipality_code])
  if code is not None:
    municipality_map[code] = item

person_description = kb.qstr("erhvervsperson", n_danish)
company_description = kb.qstr("virksomhed", n_danish)
branch_description = kb.qstr("produktionsenhed", n_danish)

kb.freeze()

class FrameBuilder:
  def __init__(self, store):
    self.store = store
    self.slots = []

  def __setitem__(self, key, value):
    self.slots.append((key, value))

  def add(self, key, value):
    self.slots.append((key, value))

  def create(self):
    return self.store.frame(self.slots)

  def write(self, out):
    frame = self.create()
    out.write(frame.id, frame.data(binary=True))

def is_person_name(name):
  first = True
  for token in name.split(" "):
    if token in ["AB", "AS"]: return False
    match = False
    if first:
      for m in aliases.lookup(token):
        if given_names.classify(m) is not None: match = True
    else:
      for m in aliases.lookup(token):
        if person_names.classify(m) is not None: match = True
    if not match: return False
    first = False
  return True

def get_country(name):
  if name is None: return None
  country = demonyms.get(name.lower())
  if country is not None: return country
  for m in aliases.lookup(name):
    if m in countries: return m
  return None

def located_in(location, region):
  if region is None: return True
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
  if attributes is None: return None
  for attr in attributes:
    if attr["type"] == key:
      for value in attr["vaerdier"]:
        v = value.get("vaerdi")
        if v is not None: return v
  return None

# Convert date from YYYY-MM-DD to SLING format.
def get_date(s):
  if s is None or len(s) == 0: return None
  year = int(s[0:4])
  month = int(s[5:7])
  day = int(s[8:10])
  return year * 10000 + month * 100 + day

def convert_address(rec, addr):
  # Country.
  place = country_map.get(rec["landekode"])

  text = rec["fritekst"]
  municipality = rec["kommune"]
  postnr = rec["postnummer"]
  if postnr == 0 or postnr == 9999: postnr = None
  addrlines = []
  if text is not None:
    # Free-text address.
    text = text.replace("\r\n", "\n")
    text = text.replace(" ,\n", "\n")
    text = text.replace(",\n", "\n")
    text = text.replace("  ", "\n")
    addrlines = text.split("\n")
  elif municipality is not None:
    # Municipality.
    code = municipality["kommuneKode"]
    if code is not None:
      location = municipality_map.get(str(code))
      if location is not None: place = location

    # Postal district.
    if postnr is not None:
      location = postalcodes.get(postnr)
      if location is not None:
        place = location
        #place = location_in(post_district, place)
      else:
        print("Unknown postal code", key, postnr, rec["postdistrikt"])

    # City.
    city_name = rec["bynavn"]
    if city_name is not None:
      place = location_in(city_name, place)

  # Address lines.
  if rec["conavn"] is not None:
    careof = rec["conavn"]
    if careof.lower().startswith("c/o"):
      addrlines.append(careof)
    else:
      addrlines.append("c/o " + careof)
  if rec["postboks"] is not None:
    addrlines.append("Postboks " + rec["postboks"])
  if rec["vejnavn"] is not None:
    street = rec["vejnavn"]
    if rec["husnummerFra"] is not None:
      street += " " + str(rec["husnummerFra"])
    if rec["husnummerTil"] is not None:
      street += "-" + str(rec["husnummerTil"])
    if rec["bogstavFra"] is not None:
      street += rec["bogstavFra"]
    if rec["bogstavTil"] is not None:
      street += rec["bogstavTil"]
    if rec["etage"] is not None:
      street += " " + rec["etage"] + "."
    if rec["sidedoer"] is not None:
      street += " " + rec["sidedoer"]
    addrlines.append(street)
  if rec["bynavn"] is not None:
    addrlines.append(rec["bynavn"])
  if postnr is not None and rec["postdistrikt"] is not None:
    addrlines.append(str(postnr) + " " + rec["postdistrikt"])

  # Compile address.
  if place is not None: addr.add(n_is, place)

  if len(addrlines) > 0:
    lines = []
    for l in addrlines:
      l = l.strip()
      if len(l) > 0: lines.append(l)
    addr.add(n_located_at_street_address, ", ".join(lines))

  if postnr is not None: addr.add(n_postal_code, postnr)

  # Add period.
  period = rec.get("periode")
  start = get_date(period.get("gyldigFra"))
  end = get_date(period.get("gyldigTil"))
  if end is not None:
    if start is not None: addr[n_start_time] = start
    addr[n_end_time] = end

def timeframed(value, start, end, store):
  if end is None: return value
  timeframe = FrameBuilder(store)
  timeframe[n_is] = value
  if start is not None: timeframe[n_start_time] = start
  timeframe[n_end_time] = end
  return timeframe.create()

def get_contact(rec):
  contact = rec["kontaktoplysning"]
  period = rec["periode"]
  start = get_date(period["gyldigFra"])
  end = get_date(period["gyldigTil"])
  return contact, start, end

def match_company_id(country, company_id):
  regs = regauth.get(country)
  if regs is not None:
    for register in regs:
      format = register[n_format]
      if format is None: continue
      if re.match(format + "$", company_id) is not None: return register
  return None

def find_company_id(country, id):
  reg = match_company_id(country, id)
  if reg is not None: return reg, id

  # Remove text before colon.
  colon = id.find(":")
  if colon != -1:
    id = id[colon + 1:].strip()
    reg = match_company_id(country, id)
    if reg is not None: return reg, id

  # Remove country code prefix.
  if id.startswith(country):
    id = id[len(country):].strip()
    reg = match_company_id(country, id)
    if reg is not None: return reg, id

  # Remove spaces.
  if " " in id:
    id = id.replace(" ", "")
    reg = match_company_id(country, id)
    if reg is not None: return reg, id

  # Replace dots with dashes.
  if "." in id:
    id = id.replace(".", "-")
    reg = match_company_id(country, id)
    if reg is not None: return reg, id

  # Remove dashes
  if "-" in id:
    id = id.replace("-", "")
    reg = match_company_id(country, id)
    if reg is not None: return reg, id

  # Add leading zero
  id = "0" + id
  reg = match_company_id(country, id)
  if reg is not None: return reg, id

  return None, None

def get_occupation(job):
  if job is None: return None
  for item in aliases.lookup(job):
    if item is None: continue
    if occupations.classify(item) is not None: return item
  return None

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
unk_functions = {}
industry_codes = {}
for key, rec in cvrdb.items():
  num_entities += 1
  if num_entities % 10000 == 0:
    print(num_entities, "entities")
    sys.stdout.flush()

  # Parse JSON record.
  data = json.loads(rec)
  store = sling.Store(kb)
  entity = FrameBuilder(store)

  # Determine entity type.
  person = False
  unit_type = data.get("enhedstype")
  unit_number = str(data["enhedsNummer"])
  if unit_type == "PERSON":
    # Get CVR person number.
    entity[n_id] = "P7972/" + unit_number
    entity.add(n_instance_of, n_human)
    entity.add(n_cvr_person_id, unit_number)
    entity.add(n_description, person_description)

    person = True
    num_persons += 1
  elif unit_type == "VIRKSOMHED":
    # Get CVR number
    cvrnr = str(data["cvrNummer"])
    entity[n_id] = "P1059/" + cvrnr
    entity.add(n_cvr_number, cvrnr)
    entity.add(n_opencorporates_id, "dk/" + cvrnr)
    entity.add(n_description, company_description)

    # Legal entity form.
    for form in data["virksomhedsform"]:
      code = form["virksomhedsformkode"]
      orgtype = legal_forms.get(code)
      if orgtype is not None:
        if orgtype[1] is not None:
          entity.add(n_instance_of, orgtype[1])
        if orgtype[0] is not None:
          entity.add(n_legal_form, orgtype[0])
      else:
        print("unknown legal form:", key, code)

    num_companies += 1
  elif unit_type == "PRODUKTIONSENHED":
    pnr = str(data["pNummer"])
    entity[n_id] = "P2814/" + pnr
    entity.add(n_cvr_branch_number, pnr)
    entity.add(n_description, branch_description)
  elif unit_type == "ANDEN_DELTAGER":
    entity[n_id] = "PCVR/" + unit_number
    participant_type = get_attribute(data, "ANDRE_DELT_TYPE")
    if participant_type == "PERSON":
      entity.add(n_instance_of, n_human)
      entity.add(n_cvr_person_id, unit_number)
      entity.add(n_description, person_description)
      person = True
      num_persons += 1
    elif participant_type == "VIRKSOMHED":
      entity.add(n_instance_of, n_organization)
      entity.add(n_description, company_description)
      num_companies += 1
    else:
      num_other += 1
      if participant_type is not None:
        print("unknown participant type:", participant_type, key)
  else:
    print("Unknown entity type:", unit_type)
    continue
  entity[n_cvr_unit_number] = unit_number

  # Get entity names.
  name_found = False
  metadata = data.get("virksomhedMetadata")
  if metadata is not None:
    newestname = metadata.get("nyesteNavn")
    if newestname is not None:
      name = newestname.get("navn")
      if name is not None:
        entity.add(n_name, name)
        name_found = True

  names = []
  for n in data["navne"]:
    name = n.get("navn")
    if name is None or name == "Ukendt": continue
    name = " ".join(name.split())
    period = n.get("periode")
    start = get_date(period.get("gyldigFra"))
    end = get_date(period.get("gyldigTil"))
    names.append((start, end, name))
  for n in sorted(names, key=lambda x: x[2], reverse=True):
    start = n[0]
    end = n[1]
    name = n[2]
    if start is None and end is None:
      entity.add(n_other_name, name)
    else:
      alias = FrameBuilder(store)
      alias[n_is] = name
      if start is not None: alias[n_start_time] = start
      if end is not None: alias[n_end_time] = end
      entity.add(n_other_name, alias.create())
    if not name_found:
      entity.add(n_name, name)
      name_found = True

  subnames = data.get("binavne")
  if subnames is not None and len(subnames) > 0:
    names = set()
    for n in subnames: names.add(n["navn"])
    for name in names: entity.add(n_other_name, name)

  # Occupation.
  occupation = data.get("stilling")
  if occupation is not None and len(occupation) > 0:
    job = get_occupation(occupation)
    if job is None: job = store.qstr(occupation, n_danish)
    entity[n_occupation] = job

  # Citizenship.
  id_type = get_attribute(data, "IDENTIFIKATION_TYPE")
  citizenship = None
  if id_type == "PASNUMMER" or id_type == "TINNUMMER":
    nationality = get_attribute(data, "OPRINDELIGT_STATSBORGERSKAB")
    if nationality is not None and nationality not in no_citizenship:
      citizenship = get_country(nationality)
      if citizenship is not None:
       num_nationality += 1

    if citizenship is None:
      country_code = get_attribute(data, "IDENTIFIKATION_LANDEKODE")
      country = country_map.get(country_code)
      if country is not None:
        citizenship = country

  if citizenship is not None:
    entity.add(n_country_of_citizenship, citizenship)
  elif person:
    num_nocitizenship += 1

  # Foreign companies.
  """
  if id_type == "UDENLANDSK REGISTRERINGSNUMMER":
    country_code = get_attribute(data, "IDENTIFIKATION_LANDEKODE")
    auth = get_attribute(data, "IDENTIFIKATION_MYNDIGHED")
    company_id = get_attribute(data, u"IDENTIFIKATION_VÆRDI")
    if country_code is not None and company_id is not None:
      reg, id = find_company_id(country_code, company_id)
      if reg is not None:
        company_property = reg[n_company_property]
        opencorp_prefix = reg[n_opencorporates_jurisdiction]
        if company_property is not None:
          comp_id = company_property.id + "/" + id
          entity.add(company_property, id)
        if opencorp_prefix is not None:
          opencorp_id = opencorp_prefix + "/" + id
          entity.add(n_opencorporates_id, opencorp_id)

      else:
        print(docid, "country:", country_code, "id:", company_id)
  """

  # Lifespan.
  lifespan = data.get("livsforloeb")
  inception = None
  dissolved = None
  if lifespan is not None:
    closed = True
    for l in lifespan:
      period = l.get("periode")
      start = get_date(period.get("gyldigFra"))
      end = get_date(period.get("gyldigTil"))
      if start is not None:
        if inception is None or start < inception: inception = start
      if end is not None:
        if dissolved is None or end > dissolved: dissolved = end
      else:
        closed = False

    if not closed: dissolved = None

  if inception is not None:
    entity.add(n_inception, inception)
  if dissolved is not None:
    entity.add(n_dissolved, dissolved)

  # Industry.
  main_industries = data.get("hovedbranche")
  if main_industries is not None:
    industries = set()
    for min in main_industries:
      dkcode = min["branchekode"]
      industry_codes[dkcode] = min["branchetekst"]
      code1 = int(dkcode[0:2])
      code2 = int(dkcode[2:4])
      code3 = int(dkcode[4:6])
      if code1 == 99 or code1 == 98: continue
      if code3 != 0:
        nacecode = "P4496/%d.%d.%dDK" % (code1, code2, code3)
      elif code2 != 0:
        nacecode = "P4496/%d.%d" % (code1, code2)
      else:
        nacecode = "P4496/%d" % (code1)
      industries.add(nacecode)
    for industry in industries:
      entity.add(n_industry, store[industry])

  # Branches.
  branches = data.get("penheder")
  if branches is not None:
    if len(branches) == 1:
      # Merge branch with parent organization for single-branch organizations.
      pnr = str(branches[0]["pNummer"])
      entity.add(n_cvr_branch_number, pnr)
    else:
      # Add branches to this organization as well as inverse relations.
      num_branches += len(branches)
      this = store["PCVR/" + unit_number]
      for branch in branches:
        pnr = str(branch["pNummer"])
        entity.add(n_has_part, store["P2814/" + pnr])
        inverse = FrameBuilder(store)
        inverse.add(n_id, "P2814/" + pnr)
        inverse.add(n_part_of, this)
        inverse.write(recout);

  # Relations (company->person).
  relations = data.get("deltagerRelation")
  if relations is not None:
    for r in relations:
      participant = r["deltager"]
      if participant is None: continue
      target = str(participant["enhedsNummer"])

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
        position = None
        relation = corporate_roles[maintype]
        if relation == n_corporate_officer and function is not None:
          role = corporate_functions.get(function)
          if role is not None:
            relation = role
          else:
            position = corporate_position.get(function)
            if position is None:
              position = store.qstr(function, n_danish)
              unk_functions[function] = unk_functions.get(function, 0) + 1
        if relation is not None:
          if start is not None or end is not None or position is not None:
            f = FrameBuilder(store)
            f[n_is] = store["PCVR/" + target]
            if position is not None: f[n_position_held] = position
            if start is not None: f[n_start_time] = start
            if end is not None: f[n_end_time] = end
            entity.add(relation, f.create())
          else:
            entity.add(relation, store["PCVR/" + target])

  # Relations (person->company).
  relations = data.get("virksomhedSummariskRelation")
  if relations is not None:
    for r in relations:
      company = r["virksomhed"]
      if company is None: continue
      target = str(company["enhedsNummer"])

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
        relation = participant_roles[maintype]
        position = None
        if relation == n_employer and function is not None:
          position = corporate_position.get(function)
          if position is None:
            position = store.qstr(function, n_danish)
            unk_functions[function] = unk_functions.get(function, 0) + 1
        if relation is not None:
          if start is not None or end is not None or position is not None:
            f = FrameBuilder(store)
            f[n_is] = store["PCVR/" + target]
            if position is not None: f[n_position_held] = position
            if start is not None: f[n_start_time] = start
            if end is not None: f[n_end_time] = end
            entity.add(relation, f.create())
          else:
            entity.add(relation, store["PCVR/" + target])

  # Mergers.
  fusions = data.get("fusioner")
  if fusions is not None and len(fusions) > 0:
    for f in fusions:
      mergerid = f["enhedsNummerOrganisation"]
      date = get_date(f["organisationsNavn"][0]["periode"]["gyldigFra"])
      merger = mergers.get(mergerid)
      if merger is None:
        merger = Merger(date, False)
        mergers[mergerid] = merger

      incoming = f.get("indgaaende")
      outgoing = f.get("udgaaende")
      if incoming != None and len(incoming) > 0: merger.incoming.append(key)
      if outgoing != None and len(outgoing) > 0: merger.outgoing.append(key)

  # Splits.
  splits = data.get("spaltninger")
  if splits is not None and len(splits) > 0:
    for s in splits:
      mergerid = s["enhedsNummerOrganisation"]
      date = get_date(s["organisationsNavn"][0]["periode"]["gyldigFra"])
      merger = mergers.get(mergerid)
      if merger is None:
        merger = Merger(date, True)
        mergers[mergerid] = merger

      incoming = s.get("indgaaende")
      outgoing = s.get("udgaaende")
      if incoming != None and len(incoming) > 0: merger.incoming.append(key)
      if outgoing != None and len(outgoing) > 0: merger.outgoing.append(key)

  # Address.
  for address in data["beliggenhedsadresse"]:
    addr = FrameBuilder(store)
    convert_address(address, addr)

    f = addr.create()
    if person:
      if address.get("kommune"):
        entity.add(n_residence, f)
      else:
        entity.add(n_work_location, f)
    else:
      entity.add(n_headquarters_location, f)

  # Contact information.
  phones = data.get("telefonNummer")
  if phones is not None:
    for phone in phones:
      contact, start, end = get_contact(phone)
      if contact is None: continue
      if not contact.startswith("+"): contact = "+45" + contact
      entity.add(n_phone_number, timeframed(contact, start, end, store))

  emails = data.get("obligatoriskEmail")
  if emails is None: emails = data.get("elektroniskPost")
  if emails is not None:
    for email in emails:
      contact, start, end = get_contact(email)
      if contact is None: continue
      contact = "mailto:" + contact
      entity.add(n_email_address, timeframed(contact, start, end, store))

  homepages = data.get("hjemmeside")
  if homepages != None:
    for homepage in homepages:
      contact, start, end = get_contact(homepage)
      if contact is None: continue
      contact = "http://"  + contact
      entity.add(n_official_website, timeframed(contact, start, end, store))

  # Source.
  entity.add(n_described_by_source, n_cvr)

  # Write item frame for entity.
  entity.write(recout);

# Output mergers.
for mid, m in mergers.items():
  store = sling.Store(kb)

  for key in m.outgoing:
    if key in m.incoming:
      m.incoming.remove(key)

  if len(m.incoming) == 0:
    print("merge missing incomming", mid, m.date, m.incoming, m.outgoing)
  elif len(m.outgoing) == 0:
    print("merge missing outgoing", mid, m.date, m.incoming, m.outgoing)
  elif len(m.incoming) > 1 or len(m.outgoing) > 1:
    print("multi-merge", mid, m.date, m.incoming, m.outgoing)

  for inkey in m.incoming:
    for outkey in m.outgoing:
      incoming = FrameBuilder(store)
      incoming.add(n_id, "PCVR/" + inkey)
      o = FrameBuilder(store)
      o.add(n_is, store["PCVR/" + outkey])
      o.add(n_point_in_time, m.date);
      if m.split:
        incoming.add(n_merged_into, o.create())
      else:
        incoming.add(n_replaced_by, o.create())

      outgoing = FrameBuilder(store)
      outgoing.add(n_id, "PCVR/" + outkey)
      i = FrameBuilder(store)
      i.add(n_is, store["PCVR/" + inkey])
      i.add(n_point_in_time, m.date);
      if m.split:
        outgoing.add(n_separated_from, i.create())
      else:
        outgoing.add(n_replaces, i.create())

      incoming.write(recout)
      outgoing.write(recout)

for f in unk_functions:
  if unk_functions[f] > 10:
    print("%6d %s" % (unk_functions[f], f))

#for ic in sorted(industry_codes):
#  print(ic, industry_codes[ic])

print(num_nationality, "with nationality")
print(num_nocitizenship, "without citizenship")
print(num_entities, "entities")
print(num_companies, "companies")
print(num_persons, "persons")
print(num_branches, "branches")
print(num_other, "other participants")
print(len(mergers), "mergers")

recout.close()
cvrdb.close()

