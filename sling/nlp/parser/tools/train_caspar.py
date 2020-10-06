import sling
import sling.flags as flags
import sling.task.workflow as workflow

flags.define("--accurate", default=False,action='store_true')
flags.define("--gpu_friendly", default=False,action='store_true')
flags.define("--conll", default=False,action='store_true')
flags.define("--pretrained_embeddings", default=False,action='store_true')

flags.parse()

if flags.arg.conll:
  basefn = "data/e/conll/caspar"
else:
  basefn = "data/e/caspar/caspar"

if flags.arg.accurate:
  modelfn = basefn + "-accurate.flow"
  rnn_layers = 3
  rnn_dim = 192
else:
  modelfn = basefn + ".flow"
  rnn_layers = 1
  rnn_dim = 128

# Start up workflow system.
workflow.startup()

# Create workflow.
wf = workflow.Workflow("caspar-trainer")

# Parser trainer inputs and outputs.
if flags.arg.conll:
  # CoNLL-2003 corpus.
  training_corpus = wf.resource(
    "data/c/conll2003/train.rec",
    format="record/document"
  )

  evaluation_corpus = wf.resource(
    "data/c/conll2003/eval.rec",
    format="record/document"
  )
else:
  # OntoNotes corpus.
  training_corpus = wf.resource(
    "data/c/caspar/train_shuffled.rec",
    format="record/document"
  )

  evaluation_corpus = wf.resource(
    "data/c/caspar/dev.rec",
    format="record/document"
  )

parser_model = wf.resource(modelfn, format="flow")

# Parser trainer task.
trainer = wf.task("parser-trainer")

params = {
  "encoder": "lexrnn",
  "decoder": "caspar",

  "rnn_type": 1,
  "rnn_dim": rnn_dim,
  "rnn_highways": True,
  "rnn_layers": rnn_layers,
  "dropout": 0.2,
  "ff_l2reg": 0.0001,

  "learning_rate": 1.0,
  "learning_rate_decay": 0.8,
  "clipping": 1,
  "optimizer": "sgd",
  "batch_size": 32,
  "rampup": 120,
  "report_interval": 1000,
  "learning_rate_cliff": 40000,
  "epochs": 50000,
}

if flags.arg.gpu_friendly:
  params["rnn_type"] = 3
  params["link_dim_token"] = 0
  params["link_dim_step"] = 0

trainer.add_params(params)

trainer.attach_input("training_corpus", training_corpus)
trainer.attach_input("evaluation_corpus", evaluation_corpus)

if flags.arg.pretrained_embeddings:
  word_embeddings = wf.resource(
    "data/c/caspar/word2vec-32-embeddings.bin",
    format="embeddings"
  )
  trainer.attach_input("word_embeddings", word_embeddings)

trainer.attach_output("model", parser_model)

# Run parser trainer.
workflow.run(wf)

# Shut down.
workflow.shutdown()

