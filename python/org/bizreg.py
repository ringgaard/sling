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

"""Business registries."""

import re

def transform(companyid, transforms):
  if transforms != None:
    for pattern, formatter in transforms:
      m = re.findall(pattern, companyid)
      if len(m) == 1:
        if formatter is None: return None
        companyid = formatter % m[0]

  return companyid

class BusinessRegistries:
  def __init__(self, store):
    self.store = store
    self.n_registration_authority_code = store["registration_authority_code"]
    self.n_jurisdiction_name = store["jurisdiction_name"]
    self.n_company_property = store["company_property"]
    self.n_opencorporates_jurisdiction = store["opencorporates_jurisdiction"]
    self.n_opencorporates_prefix = store["opencorporates_prefix"]
    self.n_eu_vat_prefix = store["eu_vat_prefix"]
    self.n_format = store["format"]
    self.n_transform = store["transform"]

    self.n_opencorporates_id = store["P1320"]
    self.n_eu_vat_number = store["P3608"]

    self.registers = store.load("data/org/bizreg.sling")
    self.regauth = {}
    for register in self.registers:
      regcode = register[self.n_registration_authority_code]
      self.regauth[regcode] = register

  def by_auth_code(self):
    return self.regauth

  def companyids(self, register, companyid, source = None):
    # Transform company id.
    companyid = transform(companyid, register[self.n_transform])
    if companyid is None: return []

    # Check company id format.
    fmt = register[self.n_format]
    if fmt != None:
      m = re.fullmatch(fmt, companyid)
      if m is None:
        regname = register[self.n_registration_authority_code]
        place = register[self.n_jurisdiction_name]
        print("Invalid", regname, place, "company id:",
              companyid, "for", source)
        return []

    # Authoritative company register.
    slots = []
    company_property = register[self.n_company_property]
    if company_property != None:
      slots.append((company_property, companyid))

    # OpenCorporates company number.
    jurisdiction = register[self.n_opencorporates_jurisdiction]
    if jurisdiction != None:
      prefix = register[self.n_opencorporates_prefix]
      if prefix is None:
        opencorp_id = jurisdiction + "/" + companyid
      elif len(prefix) > 0:
        opencorp_id = jurisdiction + "/" + prefix + "_" + companyid
      else:
        opencorp_id = None
        code = register[self.n_registration_authority_code]
        print("Missing opencorporates prefix for", code, companyid)
      if opencorp_id != None:
        slots.append((self.n_opencorporates_id, opencorp_id))

    # EU VAT number.
    euvat_prefix = register[self.n_eu_vat_prefix]
    if euvat_prefix != None:
      slots.append((self.n_eu_vat_number, euvat_prefix + companyid))

    return slots

