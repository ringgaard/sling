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
      m = re.fullmatch(pattern, companyid)
      if m != None:
        if formatter is None:
          # Discard company number.
          return None
        if formatter[0] == ">":
          # Redirect to another register.
          return formatter[1:], companyid
        else:
          try:
            companyid = formatter % m.groups()
          except Exception as e:
            raise Exception("Error converting " + companyid + " with " +
                            formatter)

  return companyid

class BusinessRegistries:
  def __init__(self, store):
    self.store = store
    self.n_registration_authority_code = store["registration_authority_code"]
    self.n_jurisdiction_name = store["jurisdiction_name"]
    self.n_register = store["register"]
    self.n_register_name = store["register_name"]
    self.n_owner = store["owner"]
    self.n_owner_name = store["owner_name"]
    self.n_company_property = store["company_property"]
    self.n_opencorporates_jurisdiction = store["opencorporates_jurisdiction"]
    self.n_opencorporates_prefix = store["opencorporates_prefix"]
    self.n_eu_vat_prefix = store["eu_vat_prefix"]
    self.n_format = store["format"]
    self.n_transform = store["transform"]

    self.n_is = store["is"]
    self.n_opencorporates_id = store["P1320"]
    self.n_eu_vat_number = store["P3608"]
    self.n_catalog = store["P972"]
    self.n_catalog_code = store["P528"]

    self.registers = store.load("data/org/bizreg.sling")
    self.regauth = {}
    for register in self.registers:
      regcode = register[self.n_registration_authority_code]
      if regcode in self.regauth: print("Duplicate RA:", regcode)
      self.regauth[regcode] = register

  def by_auth_code(self):
    return self.regauth

  def companyids(self, register, companyid, source = None):
    # Transform company id.
    companyid = transform(companyid, register[self.n_transform])
    if type(companyid) is tuple:
      # Redirected to another register.
      register = self.regauth[companyid[0]]
      companyid = transform(companyid[1], register[self.n_transform])
    if companyid is None: return []

    # Check company id format.
    fmt = register[self.n_format]
    if fmt != None:
      try:
        m = re.fullmatch(fmt, companyid)
      except Exception as e:
        print("fmt", fmt)
        print("companyid", companyid)
        raise Exception("Error matching " + companyid + " with " + fmt)
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

    # Add company id as catalog code.
    if len(slots) == 0:
      catalog = register[self.n_register]
      if catalog is None: catalog = register[self.n_owner]
      if catalog is None: catalog = register[self.n_register_name]
      if catalog is None: catalog = register[self.n_owner_name]
      if catalog != None:
        slots.append((self.n_catalog_code, {
          self.n_is: companyid,
          self.n_catalog: catalog,
        }))

    return slots

