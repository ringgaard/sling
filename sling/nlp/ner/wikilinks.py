import sling
import sling.flags as flags
import sling.task.workflow as workflow
import sling.task.corpora as corpora

flags.parse()

wf = workflow.Workflow("wiki-links")

documents = []
for l in flags.arg.languages:
  documents.extend(wf.resource("documents@10.rec",
                   dir=corpora.wikidir(l),
                   format="records/document"))

links = wf.resource("links@10.rec",
                    dir=flags.arg.workdir + "/wiki",
                    format="records/frame")
wf.mapreduce(
  input=documents,
  output=links,
  mapper="wikipedia-link-extractor",
  reducer="wikipedia-link-merger",
  format="message/frame"
)

workflow.startup()
workflow.run(wf)
workflow.shutdown()

