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

    self.registers = store.load("data/org/bizreg.sling")

  def by_auth_code(self):
    regauth = {}
    for register in self.registers:
      regcode = register[self.n_registration_authority_code]
      regauth[regcode] = register
    return regauth

