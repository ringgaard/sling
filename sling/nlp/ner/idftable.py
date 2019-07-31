import sling
import sling.flags as flags
import sling.task.workflow as workflow
import sling.task.corpora as corpora

flags.parse()

lang = flags.arg.language
wf = workflow.Workflow(lang + "-idf")

documents = wf.resource("documents@10.rec",
                        dir=corpora.wikidir(lang),
                        format="records/document")

wordcounts = wf.shuffle(
  wf.map(documents, "vocabulary-mapper", format="message/count",
         params={"min_document_length": 200, "only_lowercase": True})
)

builder = wf.task("idf-table-builder", params={"threshold": 30})
wf.connect(wordcounts, builder)

builder.attach_output("repository",
  wf.resource("idf.repo", dir=corpora.wikidir(lang), format="repository")
)

workflow.startup()
workflow.run(wf)
workflow.shutdown()
