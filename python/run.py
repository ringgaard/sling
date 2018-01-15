# Copyright 2017 Google Inc.
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

"""Run SLING processing"""

import sling
import sling.flags as flags
import sling.log as log
import sling.task.corpora as corpora
import sling.task.workflow as workflow

# Command-line flags.
flags.define("--download_wikidata",
             help="download wikidata dump",
             default=False,
             action='store_true')

flags.define("--download_wikipedia",
             help="download wikipedia dump(s)",
             default=False,
             action='store_true')

flags.define("--import_wikidata",
             help="convert wikidata to sling format",
             default=False,
             action='store_true')

flags.define("--import_wikipedia",
             help="convert wikidata dump(s) to sling format",
             default=False,
             action='store_true')

def run_workflow(wf):
  print wf.dump()
  wf.run()

def download_corpora():
  # Download wikidata dump.
  if flags.arg.download_wikidata:
    log("Download wikidata dump")
    corpora.download_wikidata()

  # Download wikipedia dumps.
  if flags.arg.download_wikipedia:
    for language in flags.arg.languages:
      log("Download " + language + " wikipedia dump")
      corpora.download_wikipedia(language)

def import_wiki():
  # Import wikidata.
  if flags.arg.import_wikidata:
    log("Import wikidata")
    wf = workflow.Workflow()
    wf.wikidata()
    run_workflow(wf)

  # Import wikipedia(s).
  if flags.arg.import_wikipedia:
    for language in flags.arg.languages:
      log("Import " + language + " wikipedia")
      wf = workflow.Workflow()
      wf.wikipedia()
      run_workflow(wf)


if __name__ == '__main__':
  # Parse command-line arguments.
  flags.parse()

  # Download corpora.
  download_corpora()

  # Import wikidata and wikipedia(s).
  import_wiki()

  # Done.
  log("Done")

