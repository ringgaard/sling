#!/usr/bin/python3
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

"""Run SLING command"""

import importlib
import subprocess
import sys
import time

import sling
import sling.flags as flags
import sling.log as log
import sling.task.workflow as workflow

# Command-line flags.
flags.define("COMMAND",
             help="commands(s) to perform",
             default=[],
             nargs="*")

flags.define("-l", "--list",
             help="list commands",
             default=False,
             action="store_true")

flags.define("--spawn",
             help="run command in background",
             default=False,
             action="store_true")

class Command:
  def __init__(self, name,
               help=None,
               package=None,
               function=None,
               triggers=None,
               internal=False):
    self.name = name
    self.help = help
    self.package = package
    self.function = function if function is not None else name
    self.triggers = triggers
    self.internal = internal

# Commands in priority order.
commands = [
  # Wiki pipeline.
  Command("build_wiki",
    help="Run all tasks for building knowledge base",
    package="sling.task.wiki",
    triggers=[
      "import_wikidata",
      "import_wikipedia",
      "parse_wikipedia",
      "merge_categories",
      "invert_categories",
      "extract_wikilinks",
      "compute_fanin",
      "fuse_items",
      "build_kb",
      "extract_names",
      "build_nametab",
      "build_phrasetab",
    ]
  ),

  # Download.
  Command("download_wikidata",
    help="Download Wikidata dump from Wikimedia",
    package="sling.task.download",
    triggers=["download_wiki"]
  ),
  Command("download_wikipedia",
    help="Download Wikipedia dump from Wikimedia",
    package="sling.task.download",
    triggers=["download_wiki"]
  ),
  Command("download_wiki",
    package="sling.task.download",
    internal=True,
  ),

  # Wikimedia import.
  Command("import_wikidata",
    help="Convert Wikidata dump to SLING format",
    package="sling.task.wiki",
    triggers=["import_wiki"]
  ),
  Command("import_wikipedia",
    help="Convert Wikipedia dump(s) to SLING format",
    package="sling.task.wiki",
    triggers=["import_wiki"]
  ),
  Command("import_wiki",
    package="sling.task.wiki",
    internal=True,
  ),
  Command("load_wikidata",
    help="Load Wikidata dump into database",
    package="sling.task.wiki",
  ),
  Command("snapshot_wikidata",
    help="Make Wikidata snapshot from database",
    package="sling.task.wiki",
  ),

  # Wikipedia processing.
  Command("parse_wikipedia",
    help="Parse Wikipedia(s) into SLING documents",
    package="sling.task.wiki",
  ),
  Command("merge_categories",
    help="Merge categories for Wikipedias into items",
    package="sling.task.wiki",
  ),
  Command("invert_categories",
    help="Invert categories from item categories to category members",
    package="sling.task.wiki",
  ),
  Command("extract_wikilinks",
    help="Extract link graph from Wikipedias",
    package="sling.task.wiki",
  ),

  # Knowledge base.
  Command("compute_fanin",
    help="Compute item fan-in",
    package="sling.task.wiki",
  ),
  Command("collect_xrefs",
    help="Collect cross-references from items",
    package="sling.task.wiki",
  ),
  Command("reconcile_items",
    help="Reconcile items from Wikidata with other sources",
    package="sling.task.wiki",
  ),
  Command("fuse_items",
    help="Fuse items from Wikidata and Wikipedia",
    package="sling.task.wiki",
  ),
  Command("build_kb",
    help="Build knowledge base",
    package="sling.task.wiki",
  ),

  # Name and phrase tables.
  Command("extract_names",
    help="Extract names for items",
    package="sling.task.wiki",
  ),
  Command("build_nametab",
    help="Build name table",
    package="sling.task.wiki",
  ),
  Command("build_phrasetab",
    help="Build alias table",
    package="sling.task.wiki",
  ),

  # Word embeddings.
  Command("extract_vocabulary",
    help="Extract vocabulary for word embeddings",
    package="sling.task.embedding",
  ),
  Command("train_word_embeddings",
    help="Train word embeddings",
    package="sling.task.embedding",
  ),

  # Fact embeddings.
  Command("extract_fact_lexicon",
    help="Extract fact and category lexicons",
    package="sling.task.embedding",
  ),
  Command("extract_facts",
    help="Extract facts from knowledge base",
    package="sling.task.embedding",
  ),
  Command("train_fact_embeddings",
    help="Train fact and category embeddings",
    package="sling.task.embedding",
  ),

  # Fact plausibility.
  Command("train_fact_plausibility",
    help="Train fact plausibility model",
    package="sling.task.embedding",
  ),

  # Silver annotations.
  Command("build_idf",
    help="Build IDF table from wikipedia",
    package="sling.task.silver",
  ),
  Command("silver_annotation",
    help="Annotate wikipedia documents with silver annotations",
    package="sling.task.silver",
  ),

  # Parser.
  Command("extract_parser_vocabulary",
    help="Extract vocabulary for parser",
    package="sling.task.silver",
  ),
  Command("train_parser",
    help="Train parser on silver data",
    package="sling.task.silver",
  ),
]

def main():
  # Parse command-line arguments. Load modules for commands before parsing
  # flags to allow each of these to register more flags.
  for arg in sys.argv:
    if arg.startswith("-"): continue
    for cmd in commands:
      if arg == cmd.name:
        importlib.import_module(cmd.package)
        break
  flags.parse()

  # List commands.
  if flags.arg.list:
    print("commands:")
    for cmd in commands:
      if not cmd.internal:
        print("  %-30s %s" % (cmd.name, cmd.help))
    sys.exit(0)

  # Run command in background if requested.
  if flags.arg.spawn:
    # Build command.
    cmd = []
    for arg in sys.argv:
      if arg != "--spawn": cmd.append(arg)
    cmd.append("--flushlog")

    # Output to log file.
    logfn = flags.arg.logdir + "/" + time.strftime("%Y%m%d-%H%M%S") + ".log"
    logfile = open(logfn, "w")

    # Start background job.
    process = subprocess.Popen(cmd,
                               stdin=None,
                               stdout=logfile,
                               stderr=subprocess.STDOUT,
                               bufsize=1,
                               shell=False,
                               close_fds=True)
    print("Running process", process.pid, "in background logging to", logfn)
    sys.exit(0)

  # Start up workflow system.
  workflow.startup()

  # Run commands.
  for cmd in commands:
    if cmd.name not in flags.arg.COMMAND: continue

    # Load module with command.
    module = importlib.import_module(cmd.package)

    # Run command.
    log.info("Execute command " + cmd.name)
    getattr(module, cmd.function)()

    # Add triggered commands.
    if cmd.triggers is not None:
      for trigger in cmd.triggers:
        flags.arg.COMMAND.append(trigger)

  # Done.
  workflow.shutdown()
  log.info("Done")

if __name__ == '__main__':
  main()

