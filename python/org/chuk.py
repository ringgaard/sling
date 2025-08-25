# Copyright 2025 Ringgaard Research ApS
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

"""Workflow for building Companies House items."""

import sling.flags as flags
import sling.task.corpora as corpora
from sling.task import *

flags.define("--chsdb",
             help="Companies House database",
             default="chs",
             metavar="DBURL")

class CHUKWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)

  def chs(self):
    """Resource for Companies House database ."""
    return self.wf.resource(flags.arg.chsdb, format="db/json")

  def chuk(self):
    """Resource for CH-UK items."""
    return self.wf.resource("chuk@10.rec",
                            dir=corpora.workdir("org"),
                            format="records/frame")

  def convert_companies_house(self):
    reader = self.wf.read(self.chs(), name="db-reader", params={"stream": True})
    input = self.wf.parallel(reader)
    output = self.chuk()
    with self.wf.namespace("companies-house"):
      return self.wf.mapreduce(input=input,
                               output=output,
                               mapper="companies-house-mapper",
                               reducer=None,
                               format="message/frame",
                               params={"indexed": True})

def convert_companies_house():
  log.info("Companies House converter")
  wf = CHUKWorkflow("org")
  wf.convert_companies_house()
  run(wf.wf)
