import sling
import sling.flags as flags
import sling.task.workflow as workflow

flags.define("--crf", default=False,action='store_true')

flags.parse()

# Start up workflow system.
workflow.startup()

# Create workflow.
wf = workflow.Workflow("bio-training")

# Parser trainer inputs and outputs.
kb = wf.resource(
  #"local/data/e/wiki/kb.sling",
  "data/dev/types.sling",
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

parser_model = wf.resource(
  "local/data/e/knolex/bio-en.flow",
  format="flow"
)

# Parser trainer task.
trainer = wf.task("parser-trainer")

trainer.add_params({
  "encoder": "lexrnn",
  "decoder": "bio",

  "rnn_type": 1,
  "rnn_dim": 128,
  "rnn_highways": True,
  "rnn_layers": 1,
  "dropout": 0.2,

  "ff_dims": [128],
  "crf": flags.arg.crf,

  "skip_section_titles": True,
  "word_dim": 64,
  "normalization": "ln",

  "learning_rate": 1.0,
  "learning_rate_decay": 0.8,
  "clipping": 1,
  "optimizer": "sgd",
  "batch_size": 64,
  "warmup": 20 * 60,
  "rampup": 5 * 60,
  "report_interval": 100,
  "learning_rate_cliff": 9000,
  "epochs": 10000,
  "checkpoint_interval": 1000,
})

trainer.attach_input("commons", kb)
trainer.attach_input("training_corpus", training_corpus)
trainer.attach_input("evaluation_corpus", evaluation_corpus)
trainer.attach_input("vocabulary", vocabulary)
trainer.attach_output("model", parser_model)

# Run parser trainer.
workflow.run(wf)

# Shut down.
workflow.shutdown()

