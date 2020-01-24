import sling
import sling.flags as flags
import sling.task.workflow as workflow

flags.define("--accurate", default=False,action='store_true')

flags.parse()

if flags.arg.accurate:
  modelfn = "local/data/e/knolex/knolex-accurate-en.flow"
  rnn_layers = 3
  rnn_dim = 192
else:
  modelfn = "local/data/e/knolex/knolex-en.flow"
  rnn_layers = 1
  rnn_dim = 128

# Start up workflow system.
workflow.startup()

# Create workflow.
wf = workflow.Workflow("knolex-training")

# Parser trainer inputs and outputs.
kb = wf.resource(
  "local/data/e/wiki/kb.sling",
  format="store/frame"
)

training_corpus = wf.resource(
  "local/data/e/silver/en/train@10.rec",
  format="record/document"
)

evaluation_corpus = wf.resource(
  "local/data/e/silver/en/eval.rec",
  format="record/document"
)

vocabulary = wf.resource(
  "local/data/e/silver/en/vocabulary.map",
  format="textmap/word"
)

word_embeddings = wf.resource(
  "local/data/corpora/caspar/word2vec-32-embeddings.bin",
  format="embeddings"
)

parser_model = wf.resource(modelfn, format="flow")

# Parser trainer task.
trainer = wf.task("knolex-trainer")

trainer.add_params({
  "rnn_type": 1,
  "rnn_dim": rnn_dim,
  "rnn_highways": True,
  "rnn_layers": rnn_layers,
  "dropout": 0.2,
  "ff_l2reg": 0.0001,
  "skip_section_titles": True,

  "learning_rate": 1.0,
  "learning_rate_decay": 0.8,
  "clipping": 1,
  "optimizer": "sgd",
  "batch_size": 128,
  "rampup": 20 * 60,
  "report_interval": 100,
  "learning_rate_cliff": 4000,
  "epochs": 5000,
})

trainer.attach_input("commons", kb)
trainer.attach_input("training_corpus", training_corpus)
trainer.attach_input("evaluation_corpus", evaluation_corpus)
trainer.attach_input("vocabulary", vocabulary)
trainer.attach_input("word_embeddings", word_embeddings)
trainer.attach_output("model", parser_model)

# Run parser trainer.
workflow.run(wf)

# Shut down.
workflow.shutdown()

