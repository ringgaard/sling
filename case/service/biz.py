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

"""SLING business registry service"""

import sling
import sling.org.bizreg

class BizService:
  def __init__(self):
    # Load business registry.
    self.commons = sling.Store()
    self.bizreg = sling.org.bizreg.BusinessRegistries(self.commons)
    self.n_company = self.commons["company"]
    self.n_regauth = self.commons["regauth"]
    self.commons.freeze()

  def handle(self, request):
    ocid = request.param("ocid")
    if ocid:
      parts = ocid.split("/")
      if len(parts) != 2: return 500
      jurisdiction = parts[0]
      companyid = parts[1]
      regauth = self.bizreg.opencorp.get(jurisdiction)
      if regauth is None: return 404

      store = sling.Store(self.commons)
      biz = self.bizreg.companyids(regauth, companyid)
      return store.frame({
        self.n_company: store.frame(biz),
        self.n_regauth: regauth,
      });

