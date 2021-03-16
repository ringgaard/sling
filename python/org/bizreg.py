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

class BusinessRegistries:
  def __init__(self, store):
    self.store = store
    self.n_registration_authority_code = store["registration_authority_code"]
    self.n_company_property = store["company_property"]
    self.n_opencorporates_jurisdiction = store["opencorporates_jurisdiction"]
    self.n_opencorporates_prefix = store["opencorporates_prefix"]
    self.n_transform = store["transform"]

    self.n_opencorporates_id = store["P1320"]

    self.registers = store.load("data/org/bizreg.sling")

  def by_auth_code(self):
    regauth = {}
    for register in self.registers:
      regcode = register[self.n_registration_authority_code]
      regauth[regcode] = register
    return regauth

  def companyids(self, register, companyid):
    slots = []

    # Normalize company id.
    companyid = companyid.replace(" " , "")
    transform = register[self.n_transform]
    if transform != None:
      for old, new in transform:
        if type(old) is int:
          if companyid[old : old + len(new)] != new:
            companyid = companyid[:old] + new + companyid[:old]
        else:
          companyid = companyid.replace(old, new)

    # Authoritative company register.
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

    return slots

