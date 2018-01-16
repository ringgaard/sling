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

import re
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

flags.define("--dryrun",
             help="build worflows but do not run them",
             default=False,
             action='store_true')

class ChannelStats:
  def __init__(self):
    self.key_bytes = 0
    self.value_bytes = 0
    self.messages = 0
    self.shards_done = 0
    self.shards_total = 0

  def update(self, metric, value):
    if metric == "input_key_bytes" or metric == "output_key_bytes":
      self.key_bytes = value
    elif metric == "input_value_bytes" or metric == "output_value_bytes":
      self.value_bytes = value
    elif metric == "input_messages" or metric == "output_messages":
      self.messages = value
    elif metric == "input_shards" or metric == "output_shards":
      self.shards_total = value
    elif metric == "input_shards_done" or metric == "output_shards_done":
      self.shards_done = value
    else:
      return False
    return True


def run_workflow(wf):
  # In dryrun mode the workflow is just dumped without running it.
  if flags.arg.dryrun:
    print wf.dump()
    return

  # Start workflow.
  log("run workflow")
  wf.run()

  # Wait until workflow completes.
  while not wf.wait(5000):
    counters = wf.counters()
    print
    print "{0:64} {1:>16}".format("Counter", "Value")
    channels = {}
    for ctr in sorted(counters):
      m = re.match(r"(.+)\[(.+)\]", ctr)
      if m != None:
        channel = m.group(2)
        metric = m.group(1)
        if channel not in channels: channels[channel] = ChannelStats()
        channels[channel].update(metric, counters[ctr])
      else:
        print "{0:64} {1:>16,}".format(ctr, counters[ctr])
    if len(channels) > 0:
      print
      print "{0:64} {1:>16} {2:>16} {3:>16} {4:>13}".format(
            "Channel", "Key bytes", "Value bytes", "Messages", "Shards done")
      for channel in channels:
        stats = channels[channel]
        print "{0:64} {1:>16,} {2:>16,} {3:>16,} {4:>5} / {5:>5}".format(
              channel, stats.key_bytes, stats.value_bytes, stats.messages,
              stats.shards_done, stats.shards_total)


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

