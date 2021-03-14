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

"""Convert GLEIF register to SLING."""

import zipfile
import csv
import sling
import sling.dataset.bizreg

# Load KB.
print("Loading KB")
kb = sling.Store()
kb.load("data/e/kb/kb.sling")

n_id = kb["id"]
n_is = kb["is"]
n_isa = kb["isa"]
n_name = kb["name"]
n_instance_of = kb["P31"]
n_country_code = kb["P297"]
n_region_code = kb["P300"]
n_organization = kb["Q43229"]
n_country = kb["P17"]
n_street_address = kb["P6375"]
n_postal_code = kb["P281"]
n_headquarters = kb["P159"]
n_located_in = kb["P131"]
n_location = kb["P276"]
n_location_of_creation = kb["P1071"]
n_lei = kb["P1278"]
n_swift_bic_code = kb["P2627"]
n_parent = kb["P749"]
n_subsidiary = kb["P355"]
n_owner_of = kb["P1830"]
n_owned_by = kb["P127"]
n_legal_form = kb["P1454"]
n_coord_location = kb["P625"]
n_geo = kb["/w/geo"]
n_lat = kb["/w/lat"]
n_lng = kb["/w/lng"]

aliases = sling.PhraseTable(kb, "data/e/kb/en/phrase-table.repo")
factex = sling.FactExtractor(kb)

city_types = factex.taxonomy([
  "Q486972",   # human settlement
  "Q56061",    # administrative territorial entity
  "Q2983893",  # quarter
])

# Read registers.
bizregs = sling.dataset.bizreg.BusinessRegistries(kb)
regauth = bizregs.by_auth_code()

# Build country and region table.
countries = {}
regions = {}
for item in kb:
  code = item[n_country_code]
  if code != None: countries[kb.resolve(code)] = item

  code = item[n_region_code]
  if code != None: regions[kb.resolve(code)] = item

# XML tags.
x_lang = kb["xml:lang"]
x_content = kb["is"]

x_record = kb["lei:LEIRecord"]
x_lei = kb["lei:LEI"]
x_entity = kb["lei:Entity"]
x_legal_name = kb["lei:LegalName"]
x_legal_address = kb["lei:LegalAddress"]
x_headquarters_address = kb["lei:HeadquartersAddress"]
x_legal_address = kb["lei:LegalAddress"]
x_first_address_line = kb["lei:FirstAddressLine"]
x_additional_address_line = kb["lei:AdditionalAddressLine"]
x_city = kb["lei:City"]
x_region = kb["lei:Region"]
x_country = kb["lei:Country"]
x_postal_code = kb["lei:PostalCode"]
x_registration_authority = kb["lei:RegistrationAuthority"]
x_registration_authority_id = kb["lei:RegistrationAuthorityID"]
x_registration_authority_entity_id = kb["lei:RegistrationAuthorityEntityID"]
x_legal_jurisdiction = kb["lei:LegalJurisdiction"]
x_legal_form = kb["lei:LegalForm"]
x_legal_form_code = kb["lei:EntityLegalFormCode"]
x_entity_category = kb["lei:EntityCategory"]
x_extension = kb["lei:Extension"]
x_geocoding = kb["gleif:Geocoding"]
x_original_address = kb["gleif:original_address"]
x_lat = kb["gleif:lat"]
x_lng = kb["gleif:lng"]
x_relationship_record = kb["rr:RelationshipRecord"]
x_relationship = kb["rr:Relationship"]
x_relationship_type = kb["rr:RelationshipType"]
x_start_node = kb["rr:StartNode"]
x_end_node = kb["rr:EndNode"]
x_node_id = kb["rr:NodeID"]
x_relationship_periods = kb["rr:RelationshipPeriods"]
x_relationship_period = kb["rr:RelationshipPeriod"]
x_start_date = kb["rr:StartDate"]
x_end_date = kb["rr:EndDate"]
x_period_type = kb["rr:PeriodType"]

kb.freeze()

def closure(item, property):
  store = item.store()
  items = [item]
  i = 0
  while i < len(items):
    f = items[i]
    i += 1
    for subitem in f(property):
      subitem = store.resolve(subitem)
      if subitem not in items:
        items.append(subitem)
  return items

def city_in(cityname, region):
  for item in aliases.lookup(cityname):
    if item is None: continue
    if city_types.classify(item) == None: continue
    if region in closure(item, n_located_in): return item
  return None

def trim(s):
  if s == None: return None
  if s.endswith(","): s = s[:-1]
  s = s.strip()
  return s if len(s) > 0 else None

def get_address(store, elem):
  addr_parts = [trim(elem[x_first_address_line])]
  for line in elem(x_additional_address_line):
    if line != addr_parts[-1]: addr_parts.append(line)
    prev = line
  cityname = trim(elem[x_city])
  postal_code = elem[x_postal_code]

  region_code = elem[x_region]
  region = regions.get(region_code)
  country_code = elem[x_country]
  country = countries[country_code]

  city = city_in(cityname, region if region != None else country)

  location = city
  if location == None:
    if cityname != None and cityname != addr_parts[-1]:
      addr_parts.append(cityname)
    location = region
  if location == None:
    location = country
    country = None

  addrline = ', '.join(filter(None, addr_parts))

  slots = []
  if location != None: slots.append((n_is, location))
  if len(addrline) > 0: slots.append((n_street_address, addrline))
  if postal_code != None: slots.append((n_postal_code, postal_code))
  if country != None: slots.append((n_country, country))
  return store.frame(slots)

def get_coord(rec, addr):
  address_prefix = trim(addr[x_first_address_line])
  if address_prefix is None:
    print("No address prefix", addr)
    return None
  ext = rec[x_extension]
  if ext is None: return None
  for geocode in ext(x_geocoding):
    original_address = geocode[x_original_address]
    if original_address is None: continue
    if not original_address.startswith(address_prefix): continue
    lat = geocode[x_lat]
    lng = geocode[x_lng]
    if lat is None or lng is None: continue
    store = rec.store()
    return rec.store().frame([
      (n_isa, n_geo),
      (n_lat, float(lat)),
      (n_lng, float(lng))
    ])
  return None

store = sling.Store(kb)

def find_lei(lei):
  company = store["P1278/" + lei]
  if company.isproxy() or company.isglobal(): return None
  return company

# LEI company data (level 1).
print("Reading GLEIF entities")
lei = zipfile.ZipFile("data/c/lei/lei2.xml.zip", "r")
leifile = lei.open(lei.namelist()[0], "r")

lines = []
num_companies = 0
companies = []
unknown_regauth = {}
unknown_categories = {}
for line in leifile:
  if line.startswith(b"<lei:LEIRecord"):
    # Start new block.
    lines = [line]
    continue
  else:
    # Add line and continue until end tag found.
    lines.append(line)
    if line != b"</lei:LEIRecord>\n": continue

  # Parse XML record.
  xmldata = b"".join(lines)
  root = store.parse(xmldata, xml=True)

  # Build company frame.
  slots = []
  rec = root[x_record]
  lei_number = rec[x_lei]
  lei_id = "P1278/" + lei_number
  slots.append((n_id, lei_id))
  slots.append((n_lei, lei_number))
  slots.append((n_instance_of, n_organization))
  entity = rec[x_entity]

  # Organization type.
  category = entity[x_entity_category]
  if category != None:
    unknown_categories[category] = unknown_categories.get(category, 0) + 1

  # Company name.
  legal_name = entity[x_legal_name]
  if type(legal_name) is sling.Frame:
    name_lang = legal_name[x_lang]
    name = legal_name[x_content]
  else:
    name_lang = None
    name = legal_name
  slots.append((n_name, name))

  # Address.
  hq = entity[x_headquarters_address]
  legal_address = entity[x_legal_address]
  if hq != None:
    addr = get_address(store, hq)
    coord = get_coord(rec, hq)
    if coord != None: addr.append(n_coord_location, coord)
    slots.append((n_headquarters, addr))
  elif legal_address != None:
    addr = get_address(store, legal_address)
    coord = get_coord(rec, legal_address)
    if coord != None: addr.append(n_coord_location, coord)
    slots.append((n_location, addr))

  # Country and region for jurisdiction.
  jurisdiction = entity[x_legal_jurisdiction]
  if jurisdiction != None:
    country = countries.get(jurisdiction)
    if country == None:
      region = regions.get(jurisdiction)
      if region != None:
        slots.append((n_location_of_creation, region))
        country = region[n_country]
    if country != None:
      slots.append((n_country, country))

  # Legal form.
  legal_form = entity[x_legal_form]
  if legal_form != None:
    elf = legal_form[x_legal_form_code]
    if elf != None and elf != "8888" and elf != "9999":
      slots.append((n_legal_form, store["PELF/" + elf]))

  # Company identifiers.
  reg_auth = entity[x_registration_authority]
  if reg_auth != None:
    reg_auth_id = reg_auth[x_registration_authority_id]
    entity_id = reg_auth[x_registration_authority_entity_id]
    if reg_auth_id != None and reg_auth_id != "RA888888" and entity_id != None:
      register = regauth.get(reg_auth_id)
      if register is None:
        unknown_regauth[reg_auth_id] = unknown_regauth.get(reg_auth_id, 0) + 1
      else:
        ids = bizregs.companyids(register, entity_id)
        slots.extend(ids)

  # Create item frame for company.
  f = store.frame(slots)
  companies.append(f)
  num_companies += 1

leifile.close()
lei.close()
print(num_companies, "companies")

# Read entity relationships (level 2).
print("Reading GLEIF relationships")
lines = []
rr = zipfile.ZipFile("data/c/lei/rr.xml.zip", "r")
rrfile = rr.open(rr.namelist()[0], "r")
relations = []
for line in rrfile:
  if line.startswith(b"<rr:RelationshipRecord"):
    # Start new block.
    lines = [line]
    continue
  else:
    # Add line and continue until end tag found.
    lines.append(line)
    if line != b"</rr:RelationshipRecord>\n": continue

  # Parse XML record.
  xmldata = b"".join(lines)
  root = store.parse(xmldata, xml=True)

  rec = root[x_relationship_record]
  relationship = rec[x_relationship]
  start = relationship[x_start_node]
  end = relationship[x_end_node]
  start_lei = start[x_node_id]
  end_lei = end[x_node_id]
  reltype = relationship[x_relationship_type]

  # Dertermine relationship type.
  if reltype == "IS_ULTIMATELY_CONSOLIDATED_BY":
    indirect = True
  elif reltype == "IS_DIRECTLY_CONSOLIDATED_BY":
    indirect = False
  elif reltype == "IS_INTERNATIONAL_BRANCH_OF":
    indirect = False
  else:
    print("Unknown relationship:", reltype)
    continue

  relations.append((end_lei, start_lei, indirect))

rrfile.close()
rr.close()

# Add relationships to companies.
relations.sort()
prev_parent = None
prev_subsidiary = None
num_relations = 0
for rel in relations:
  # Get related organizations.
  parent = find_lei(rel[0])
  if parent is None:
    print("Missing parent:", rel[0])
    continue
  subsidiary = find_lei(rel[1])
  if subsidiary is None:
    print("Missing subsidiary:", rel[1])
    continue
  indirect = rel[2]

  # Only include either direct or indirect ownership.
  if parent == prev_parent and  subsidiary == prev_subsidiary: continue

  # Add relationship to both parent and subsidiary.
  if indirect:
    parent.append(n_owner_of, subsidiary)
    subsidiary.append(n_owned_by, parent)
  else:
    parent.append(n_subsidiary, subsidiary)
    subsidiary.append(n_parent, parent)

  prev_parent = parent
  prev_subsidiary = subsidiary
  num_relations += 1

print(num_relations, "relations")

"""
# Add SWIFT BIC codes to companies.
print("Adding SWITFT/BIC codes")
bicfile = open("bic_lei_gleif_v1_monthly_full_" + bicdate + ".csv", "r")
bicreader = csv.reader(bicfile)
bicreader.next()  # skip header
for row in bicreader:
  bic = row[0]
  lei = row[1]
  lei_id = "P1278/" + lei
  if lei_id not in store:
    print("LEI not found:", lei)
    continue
  company = store[lei_id]
  company.append(n_swift_bic_code, bic)
bicfile.close()
"""

# Write companies to record file.
print("Writing companies to file")
recout = sling.RecordWriter("data/e/lei/gleif.rec")
for company in companies:
  recout.write(company.id, company.data(binary=True))
recout.close()

# Output unknown registers.
for ra, count in unknown_regauth.items():
  print("Unknown RA", ra, ",", count, "companies")

# Output unknown categories.
for category, count in unknown_categories.items():
  print("Unknown category", category, ",", count, "companies")

print("Done.")

