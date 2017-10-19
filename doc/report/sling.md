# SLING: A framework for frame semantic parsing

We describe SLING, a framework for parsing natural language into
semantic frames. SLING supports general transition-based, neural-network parsing
with bidirectional LSTM input encoding and a Transition Based Recurrent
Unit (TBRU) for output decoding. The parsing model is
trained end-to-end using only the text tokens as input. The
transition system has been designed to output frame graphs directly without
any intervening symbolic representation.
The SLING framework includes an efficient and scalable frame store
implementation as well as a neural network JIT compiler for fast inference
during parsing.
SLING is implemented in C++ and it is available for download on GitHub.

## Introduction

Recent advances in machine learning make it practical to train
recurrent multi-level neural network classifiers, allowing us to rethink the
design and implementation of natural language
understanding (NLU) systems.

Earlier machine-learned NLU systems were commonly organized as pipelines of
separately trained stages for syntactic and semantic annotation of text.
A typical  pipeline would start with part-of-speech (POS) tagging, followed by
constituency or dependency parsing for syntactic analysis.
Using the POS tags and parse trees as feature inputs, later stages in the
pipeline could then derive semantically relevant annotations such as entity and
concept mentions, entity types, coreference relationships, and semantic roles
(SRL).

For simplicity and efficiency, each stage in a practical NLU pipeline would just
output its best hypothesis and pass it on to the next stage [Finkel et al., 2006].
Obviously, errors could then accumulate
throughout the pipeline making it much harder for the system to perform
accurately. For instance, F1 on SRL drops by more than 10% when going from gold
to system parse trees [Toutanova and Manning, 2005].

However, applications may not need the intermediate annotations produced
by the earlier stages of a NLU pipeline, so it would be preferable if all stages
could be trained together to optimize an objective based on the output
annotations needed for a particular application.

Earlier NLU pipelines often used linear classifiers for each stage.
Linear classifiers achieve simplicity and training efficiency at the expense of
feature complexity, requiring elaborate feature
extraction, many different feature types, and
feature combinations to achieve reasonable accuracy.
With deep learning, we can use embeddings, multiple layers, and recurrent
network connections to reduce the need for complex
feature design. The internal learned representations in model hidden layers
replace the hand-crafted feature combinations and intermediate representations
in pipelined systems.


The SLING parser exploits deep learning to bypass those limitations of classic
pipelined systems.
It is a transition-based parser that outputs frame graphs directly without any
intervening symbolic representation. Transition-based
parsing is often associated with dependency parsing, but we have designed a
specialized transition system that outputs frame graphs instead of dependency
trees.

We use a recurrent feed-forward unit for predicting the actions in the
transition sequence, where the hidden activations from predicting each
transition step are fed back into subsequent steps.
A bidirectional LSTM (biLSTM) encodes the input into a sequence of vectors.
This neural network architecture has been implemented using DRAGNN [Kong et al., 2017]
and TensorFlow [Abadi et al., 2016].

The SLING framework and a semantic parser built in it are now available as
open-source code on [GitHub](https://github.com/google/sling).

|![SLING neural network architecture.](network.svg)|
|:---|
|Neural network architecture of the SLING parser. The input is encoded by a bi-directional LSTM and fed into a recurrent feed-forward (FF) unit that proposes transition system actions. The hidden layer activations and the transition system state are combined to create the input feature vector for the next step. The FF unit is run repeatedly until the transition system has reached a final state.|

## Frame semantics

While frames in SLING are not tied to any particular linguistic theory or
knowledge ontology, they are inspired by *frame semantics*, the
theory of linguistic meaning originally developed by Charles Fillmore [Fillmore, 1982].
Frame semantics connects linguistic semantics to encyclopedic knowledge, with the
central idea that understanding the meaning of a word requires access to all
the essential knowledge that relates to that word. A word *evokes* a frame
representing the specific concept it refers to.

A semantic frame is a set of statements that give "characteristic
features, attributes, and functions of a denotatum, and its characteristic
interactions with things necessarily or typically associated with it." [Alan, 2001].
A semantic frame can also be viewed as a coherent group of concepts
such that complete knowledge of one of them requires knowledge of all of them.

Frame semantics is not just for individual concepts, but can be generalized
to phrases, entities, constructions, and other larger and more complex linguistic
and ontological units. Semantic frames can also model world knowledge and inferential relationships
in common sense,
metaphor [Narayanan, 1999a],
metonymy,
action [Narayanan, 1999b],
and perspective [Chang et al., 2002].

## Frames in SLING

SLING represents frames with data structures consisting of a list of slots, where each
slot has a name (role) and a value. The slot values can be literals like numbers
and strings, or links to other frames. A collection of interlinked frames can thus be seen as a directed
graph where the frames are the (typed) nodes and the slots are the (labeled)
edges. A frame graph can also be viewed as a feature structure [Carpenter, 2005]
and unification can be used for induction of new frames from existing frames.
Frames can also be used to represent more basic data
structures such as a C struct with fields, a JSON object, or a record in a
database.

SLING frames live inside a *frame store*. A store is a container that
tracks all the frames that have been allocated in the store, and serves as a
memory allocation arena for them. When making a new frame, one
specifies the store where the frame should be allocated. The frame will live in
this store until the store is deleted or the frame is garbage collected because
there no remaining live references to it.
See the [SLING Guide](https://github.com/google/sling/blob/master/frame/README.md)
for a detailed description of the SLING frame store implementation.

SLING frames are externally represented in a superset of JSON that allows
references between frames (JSON objects) with the `#n` syntax. Frames can
be assigned identifiers (*ids*) using the `=#n` syntax. SLING frames
can  have both numeric and named ids and both slot names and values can be frame
references. Where JSON objects can only represent trees, SLING frames can be
used for representing arbitrary graphs. SLING has special syntax for built-in
slot names:


| Syntax | Symbol   | RDF             |
|--------|----------|-----------------|
| =name  | id:name  | rdf:ID          |
| :name  | isa:name | rdf:InstanceOf  |
| +name  | is:name  | rdfs:subClassOf |

Documents are also represented using frames, where the document frame has slots
for the document text, the tokens, and the mention phrases and the frames they
evoke.

```
{
  :/s/document
  /s/document/text: "John hit the ball"
  /s/document/tokens: [
    {/s/token/text: "John" /s/token/start: 0  /s/token/length: 4},
    {/s/token/text: "hit"  /s/token/start: 5  /s/token/length: 3},
    {/s/token/text: "the"  /s/token/start: 9  /s/token/length: 3},
    {/s/token/text: "ball" /s/token/start: 13 /s/token/length: 4}
  ]
  /s/document/mention: {
    :/s/phrase /s/phrase/begin: 0
    /s/phrase/evokes: {=#1 :/saft/person }
  }
  /s/document/mention: {
    :/s/phrase /s/phrase/begin: 1
    /s/phrase/evokes: {
      :/pb/hit-01
      /pb/arg0: #1
      /pb/arg1: #2
    }
  }
  /s/document/mention: {
    :/s/phrase /s/phrase/begin: 3
    /s/phrase/evokes: {=#2 :/saft/consumer_good }
  }
}
```

The text "John hit the ball" in SLING frame notation. The document
itself is represented by a frame that has the text, an array of tokens and
the mentions that evoke frames. There are three frames: a person frame (John),
a consumer good frame (bat) and a hit-01 frame. The hit frame has the person
frame as the agent (arg0) and the ball frame as the object (arg1).}

## Attention

The SLING parser is a kind of sequence-to-sequence model that first encodes the
input text token sequence with a bidirectional LSTM encoder and then runs
the transition system on that encoding to produce a sequence of transitions,
where each transition updates the system state that combined with the input
encoding form the input for the transition feed-forward cell that predicts the
next transition.

Sequence-to-sequence models often rely on an "attention" mechanism to focus
the decoder on the parts of the input most relevant for producing the next
output symbol. In this work, however, we use a somewhat difference attention
mechanism, loosely inspired on neuroscience models of attention and awareness
[Nelson et al., 2017; Graziano, 2013]. In our model, attention focuses on parts of the
frame representation that the parser has created so far, rather than focusing
on (encodings of) input tokens as is common for other sequence-to-sequence
attention mechanisms.

We maintain an *attention buffer* as part of the transition system state.
This an ordered list of frames, where the order represents closeness to the
center of attention. Transition system actions maintain the attention buffer, bringing
a frame to the front when the frame is evoked or re-evoked by the input text.
When a new frame is evoked, it will merge the concept and its roles into a new
coherent chunk of meaning, which is represented by the new frame and its
relations to other frames, and this will become the new center of attention.
Our hypothesis is that by maintaining this attention mechanism, we only need to
look at a few recent frames brought into attention to build the desired
frame graph.

## Transition system

*Transition systems* are widely
used in parsing to build dependency parse trees as a side effect of performing a sequence *state transitions*
(s<sub>i</sub>,a<sub>i</sub>) where s<sub>i</sub> is a *state* and a<sub>i</sub> is an *action*. Action a<sub>i</sub> computes the new state
s<sub>i+1</sub> from state s<sub>i</sub>. For example, the *arc-standard*
transition system [Nivre, 2006] uses a sequence of **SHIFT**, **LEFT-ARC(label)**, and
**RIGHT-ARC(label)** actions, operating on a state whose main component is a stack, to build a dependency parse tree.

We use the same idea to construct a frame graph where frames can be
evoked by phrases in the input. But instead of using a stack in the state, we use the  attention
buffer introduced in the previous section that keeps track of the most salient
frames in the discourse.

The attention buffer is a priority list of all the
frames evoked so far. The front of the buffer serves as the working
memory for the parser. Actions operate on the front of the buffer and in some cases other frames in the buffer. The transition
system simultaneously builds the frame graph and maintains the attention buffer
by moving the frame involved involved in an action to the front of the attention
buffer. At any time, each evoked frame has a unique position in the attention
buffer.

The transition system consists of the following actions:

* **SHIFT** – Moves to next input token. Only valid when not at the
  end of the input buffer.
* **STOP** – Signals that we have reach the end of the parse. This is
  only valid when at the end of the input buffer. Multiple STOP actions
  can be added to the transition sequence, e.g. to make all sequences in a
  beam have the same length. After a STOP is issued, no other actions are
  permitted except more STOP actions.
* **EVOKE(type, n)** – Evokes a frame of type **type** from
  the next **n** tokens in the input. The evoked frame is inserted at the front of the attention
  buffer, becoming the new center of attention.
* **REFER(frame, n)** – Makes a new mention from the next **n** tokens
  in the input evoking an existing frame in the attention buffer. This
  frame is moved to the front of the attention buffer and will become the
  new center of attention.
* **CONNECT(source, role, target)** – Adds slot to **source** frame
  in the attention buffer with name **role** and value **target**
  where **target** is an existing frame in the attention buffer. The
  **source** frame become the new center of attention.
* **ASSIGN(source, role, value)** – Adds slot to **source** frame in
  the attention buffer with name **role** and constant value **value**
  and moves the frame to the front of the buffer. This action
  is only used for assigning a constant value to a slot, in contrast to
  **CONNECT** where the value is another frame in the attention buffer.
* **EMBED(target, role, type)** – Creates a new frame with
  type **type** and adds a slot to it with name **role** and value
  **target** where **target** is an existing frame in the attention
  buffer. The new frame becomes the center of attention.
* **ELABORATE(source, role, type)** – Creates a new frame with type
  **type** and adds a slot to an existing frame **source** in the
  attention buffer with **role** set to the new frame. The new frame
  becomes the center of attention.

In summary, **EVOKE** and **REFER** are used to evoke frames from text
mentions, while **ELABORATE** and **EMBED** are used to create frames not
directly evoked by text.

This transition system can generate any connected frame graph where the frames
are either directly on indirectly evoked by phrases in the text. A frame
can be evoked by multiple mentions and the graph can have cycles.

The transition system can potentially have an unbounded number of actions since
it is parameterized by phrase length and attention buffer indices which can be
arbitrarily large. In the current implementation, we only consider the
top k frames in the attention buffer (k=5) and we do not consider any phrases
longer than those in the training corpus.

Multiple transition sequences can generate the same frame annotations, but we
have implemented an oracle sequence generator that takes a document and converts
it to a canonical transition sequence in a way similar to how this is done
for transition-based dependency parsing [Nivre, 2006]. For example, the sentence
"John hit the ball" generates the following transition sequence:

```
  EVOKE(/saft/person, 1)
  SHIFT
  EVOKE(/pb/hit-01, 1)
  CONNECT(0, /pb/arg0, 1)
  SHIFT
  SHIFT
  EVOKE(/saft/consumer_good, 1)
  CONNECT(1, /pb/arg1, 0)
  SHIFT
  STOP
```

## Features

The biLSTM uses only lexical features based on the current input word:

* The current word itself. During training we initialize the embedding
  for this feature from pre-trained word embeddings [Mikolov et al., 2013] for all
  the words in the the training data.
* The prefixes and suffixes of the current input word. We use only
  prefixes up to three characters in our experiments.
* Word shape features based on the characters in the current input word:
  hyphenation, capitalization, punctuation, quotes, and digits. Each of these
  features has its own embedding matrix.

The TBRU is a simple feed-forward unit with a single hidden layer.
It takes the hidden activations from the biLSTM as well as the activations from
the hidden layer from the previous steps as raw input features, and maps them
through embedding matrices to get the input vector for the  hidden layer. More specifically,
the inputs to the TBRU are as follows:

* The left-to-right and right-to-left LSTMs supply their activations
  for the current token in the parser state.
* The attention feature looks at the top-k frames in the attention buffer
  and finds the phrases in the text (if any) that evoked them. The activations
  from the left-to-right and right-to-left LSTMs for the last token of each of those
  phrases are are included as TBRU inputs, serving as continuous lexical
  representations of the top-k frames in the attention buffer.
* The hidden layer activations of the transition steps which evoked or
  brought into focus the top-k frames in the attention buffer are also inputs to the TBRU,
  providing a
  continuous representation for the semantic frame contexts that evoked those frames most recently.
* The history feature uses the hidden activations in the feed-forward
  unit from the previous k steps as feature inputs to the current step.
* Embeddings of triples of the form (s<sub>i</sub>, r<sub>i</sub>, t<sub>i</sub>), 0 &lt; s<sub>i</sub>, t<sub>i</sub> &le; k, encode the fact that the frame at position s<sub>i</sub> in the attention buffer has a role r<sub>i</sub> with
  the frame at position t<sub>i</sub> in the attention buffer as its value. Back-off
  features are added for the source roles (s<sub>i</sub>,r<sub>i</sub>), target role (r<sub>i</sub>, t<sub>i</sub>),
  and unlabeled roles (s<sub>i</sub>,t<sub>i</sub>).

## Experiments

We derived a corpus annotated with semantic frames from the OntoNotes corpus [Pradhan and Xue, 2009]. We took the
PropBank SRL layer [Palmer et al., 2005] and converted the predicate-argument
structures into frame annotations. We also annotated the corpus with
entity frames based on entity types from a state-of-the-art entity tagger.
We determined the head token of each argument span and if this coincided
with the span of an existing frame, then we used it as the evoking span for the
argument frame, otherwise we just used the head token as the evoking span of the
argument frame.

The various frame types mentioned above are listed in the table below. 
They include 7 conventional entity types,
6 top-level non-entity types (e.g. date), 13 measurement types, and
more than 5400 PropBank frame types. All the frame roles are collapsed onto
/pb/arg0, /pb/arg1, and so on. Our training corpus size was 111,006
sentences, 2,206,274 tokens.

| Type set |Details |
|--------|----------|
| Entity types | /saft/{person, location, organization, art, consumer_good, event, other} |
| Top-level non-entity types | /s/{thing, date, price, measure, time, number} |
| Fine-grained measure types | /s/measure/{area, data, duration, energy, frequency, fuel, length, mass, power, speed, temperate, voltage, volume} ||
|PropBank SRL types | 5426 types, e.g. /pb/write-01, /pb/tune-02  |

The table below shows action statistics for the transition sequences that
generate the gold frames in the training corpus. As expected, there is one
SHIFT action per training token, and one STOP action per training sentence.
The EVOKE action occurred with 5,532 unique (length, type) arguments in the
corpus, for a raw count of roughly 1.08 million action tokens. Overall our action space
had 6968 action types, which is also the size of the softmax layer of our TBRU
decoder.

| Action Type | # Unique Args | Raw Count |
|-------------|--------------:|----------:|
| SHIFT       |             1 | 2,206,274 |
| STOP        |             1 |   111,006 |
| EVOKE       |         5,532 | 1,080,365 |
| CONNECT     |         1,421 |   635,734 |
| ASSIGN      |            13 |     5,430 |
| Total       |         6,968 | 4,038,809 |

**Hyperparameters:** Our final set of hyperparameters after
grid search with a dev corpus was: learning_rate} = 0.0005,
optimizer = Adam [Kingma and Ba, 2014] with beta<sub>1</sub> = 0.01, beta<sub>2</sub> = 0.999,
epsilon = 1e-5, no dropout, gradient clipping at 1.0, exponential moving
average, no layer normalization, and a training batch size of 8.

We stopped training after 120,000 steps, where each step corresponds to
processing one training batch, and evaluated on the dev corpus
(15,084 sentences) after every checkpoint (= 2,000 steps).


[This figure](dev-eval.pdf) shows the how the various evaluation metrics evolve
as training progresses. The next section contains the details of these
metrics are evaluated. We picked the checkpoint with the best *Slot F1* score.

## Evaluation

An annotated document consists of a number of connected frames as well as
phrases (token spans) that evoked these frames. We evaluated
annotation quality by comparing the generated frames with the gold standard frame
annotations from the evaluation corpus.

Two documents are matched by constructing a virtual graph where the document
is the start node. The document node is then connected to the spans and the
spans are connected to the frames that the spans evoke. This graph is then
extended by following the frame-to-frame links via the roles. Quality is
computed by aligning the golden and predicted graphs and computing precision,
recall, and F1. Those scores are separately computed for spans, frames,
frame types, roles that link to other frames (referred to as 'roles'),
and roles that link to global constants (referred to as 'labels').

We also report two aggregate quality scores: (a) *Slot*, which is
an aggregate of *Type*, *Role*, and *Label*, and (b) *Combined*,
which is an aggregate of *Span*, *Frame*, *Type*, *Role*, and
*Label*.

We rated the checkpoints using the Slot-F1 metric and selected the checkpoint with
the best Slot-F1. Intuitively, a high *Slot* score reflects that the
right type of frames are being evoked, along with the right set of slots and
links to other frames.

[This figure](dev-eval.pdf) shows that as training progresses,
the model learns to output the spans and frames evoked from those spans with
fairly good quality (SPAN F1 	&asymp; FRAME F1 	&asymp; 93.81%). It also
gets the type of those frames right with a TYPE F1 of 85.88%. ROLE F1
though is lower at just 69.65%. ROLE F1 measures the accuracy of correctly
getting the frame-frame link, including the label of the link. Further error
analysis will be required to understand how frame-frame links are missed by
the model. Also note that currently the *roles* feature is the only one
that captures inter-frame link information.
Augmenting this with more features should help improve ROLE quality, as we will
investigate in future work.

Finally, we took the best checkpoint, with SLOT F1 = 79.95% (at 118,000 steps),
and evaluated it on the test corpus.
The table below lists the quality of this model on the test and dev
corpora.
With the exception of LABEL accuracies, all the other metrics exhibit less than
half a percent difference between the test and dev corpora. This illustrates
that despite the lack of dropout, the model generalizes well to unseen text.
As for the disparity on LABEL F1 (95.73 on dev
against 92.81 on test), we observe from [this figure](dev-eval.pdf)
that the LABEL accuracies follow a different improvement pattern
during training. On the dev set, LABEL F1 peaked at 96.18 at 100,000 steps,
and started degrading slightly from there on to 95.73 at 118,000 steps,
possibly showing signs of overfitting which are absent in the other metrics.

| Metric    |           |   Dev   | Test    |
|-----------|-----------|--------:|--------:|
| Tokens    |           | 291,746 | 216,473 |
| Sentences |           |  15,084 |  11,623 |
| Span      | Precision |   93.42 |   93.04 |
|           | Recall    |   94.21 |   94.34 |
|           | F1        |   93.81 |  93.69  |
| Frame     | Precision |   93.47 |   93.20 |
|           | Recall    |   94.16 |   94.08 |
|           | F1        |   93.81 |   93.64 |
| Type      | Precision |   85.56 |   85.67 |
|           | Recall    |   86.20 |   86.49 |
|           | F1        |   85.88 |   86.08 |
| Role      | Precision |   70.21 |   69.59 |
|           | Recall    |   69.11 |   69.20 |
|           | F1        |   69.65 |   69.39 |
| Label     | Precision |   96.51 |   95.02 |
|           | Recall    |   94.97 |   90.70 |
|           | F1        |   95.73 |   92.81 |
| Slot      | Precision |   80.00 |   79.81 |
|           | Recall    |   79.90 |   80.10 |
|           | F1        |   79.95 |   79.96 |
| Combined  | Precision |   87.46 |   87.20 |
|           | Recall    |   87.79 |   87.91 |
|           | F1        |   87.63 |   87.55 |

## Parser runtime

The SLING parser uses TensorFlow [Abadi et al., 2016] for training but it also
supports annotating text with frame annotations at runtime. It can take
advantage of batching and multi-threading to speed up parsing. However, in
practical applications of the parser, it may not be convenient to
batch documents for processing, so to have a realistic benchmark, we
set the batch size to one at runtime. In this configuration, the
TensorFlow-based SLING parser runs at 200 tokens per CPU second.

To speed up parsing, we have created *Myelin*, a
just-in-time compiler for neural networks that compiles network cells into
x64 machine code at runtime. The generated code exploits such
specialized CPU features as SSE, AVX, and FMA3, if available.
Tensor shapes and model parameters are fixed at runtime.
This allows us to optimize the network by folding constants, unrolling
loops, and pre-computing embeddings, among other transformations. The JIT compiler can also fix the data
instance layout at compile-time to speed up runtime data access.

The Myelin-based SLING parser runs at 2500 tokens per CPU second, more
than ten times faster than the TensorFlow-based version. Speed is measured as tokens parsed per CPU second, i.e. user+sys in time(1).

| Runtime    | Speed     | Runtime size | Load time |
|------------|----------:|-------------:|----------:|
| TF         |  200 TPS  |    37.000 KB | 10 secs   |
| Myelin     | 2500 TPS  |    500 KB    | 0.5 secs  |

The Myelin-based SLING parser is independent of TensorFlow so it only needs to
link with the Myelin runtime (less than 500 KB) instead of the TensorFlow
runtime library (37 MB), and it is also much faster to initialize (0.5 seconds
including compilation time) than the TensorFlow-based parser (10 seconds).
This figure shows a breakdown of the CPU time for the Myelin-based
parser runtime:

|![Myelin runtime.](runtime.svg)|
|:---|
|Runtime profile for running the Myelin-based SLING parser with the left-to-right LSTM (LR LSTM), right-to-left LSTM (RL LSTM), Feed-forward excluding logits (FF), Logits for output actions (LOGITS), and transition system and feature extraction (TS).|

Half the time is spent computing the logits for the output
actions. This is expensive because the OntoNotes-based corpus has 6968 actions,
where the vast majority of the actions are of a form like
**EVOKE(/pb/hit-01, 1)**, one for each PropBank roleset predicate in the
training data. Only about 26% of all the
actions are EVOKE actions. The output layer of the FF unit could be turned into
a cascaded classifier, where if the first classifier predicts a generic
**EVOKE(/pb/predicate, 1)** action, it would use a secondary classifier to
predict the predicate type. This could almost double the speed of the parser.

## References

* Martin Abadi et al. 2016. *Tensorflow: Largescale machine learning on heterogeneous
  distributed systems*. CoRR abs/1603.04467. http://arxiv.org/abs/1603.04467.
* Keith Alan. 2001. *Natural Language Semantics*, Blackwell Publishers Ltd, Oxford, page 251.
* Bob Carpenter. 2005. *The logic of typed feature structures: with applications to unification grammars,
  logic programs and constraint resolution*, volume 32. Cambridge University Press.
* Nancy Chang, Srini Narayanan, and Miriam R. L. Petruck. 2002. *Putting frames in perspective*. In
  Proceedings of the 19th international conference on Computational linguistics-Volume 1. Association for
  Computational Linguistics, pages 1–7.
* Charles J. Fillmore. 1982. *Frame semantics*. Linguistics in the Morning Calm pages 111–138.
* Jenny Rose Finkel, Christopher D. Manning, and Andrew Y. Ng. 2006. *Solving the problem of cascading
  errors: Approximate bayesian inference for linguistic annotation pipelines*. In Proceedings of the
  2006 Conference on Empirical Methods in Natural Language Processing. Association for Computational
  Linguistics, pages 618–626.
* Michael S.A. Graziano. 2013. *Consciousness and the Social Brain*. Oxford University Press.
* D. P. Kingma and J. Ba. 2014. Adam: *A Method for Stochastic Optimization*. ArXiv e-prints
  arXiV:1402.6980.
* Lingpeng Kong, Chris Alberti, Daniel Andor, Ivan Bogatyy, and David Weiss. 2017. *DRAGNN: A
  transition-based framework for dynamically connected neural networks*. CoRR abs/1703.04474.
  http://arxiv.org/abs/1703.04474.
* Tomas Mikolov, Kai Chen, Greg Corrado, and Jeffrey Dean. 2013. *Efficient estimation of word
  representations in vector space*. arXiv preprint arXiv:1301.3781.
* Srinivas Narayanan. 1999a. *Moving right along: A computational model of metaphoric reasoning about
  events*. Proceedings of the National Conference on Artificial Intelligence pages 121–128.
* Srinivas Narayanan. 1999b. *Reasoning about actions in narrative understanding*. In Proceedings of the
  International Joint Conference on Artificial Intelligence.
  Morgan Kaufmann, volume 99, pages 350–357.
* Matthew J. Nelson, et al. 2017. *Neurophysiological dynamics of phrase-structure building during
  sentence processing*. Proceedings of the National Academy of Sciences page 201701590.
* Joakim Nivre. 2006. *Inductive dependency parsing*, volume 34. Springer.
* Martha Palmer, Daniel Gildea, and Paul Kingsbury. 2005. *The proposition bank: An annotated corpus of
  semantic roles*. Computational linguistics 31(1):71–106.
* Sameer S. Pradhan and Nianwen Xue. 2009. *Ontonotes: The 90% solution*. In HLT-NAACL
  (Tutorial Abstracts). pages 11–12.
* Kristina Nikolova Toutanova and Christopher D. Manning. 2005. *Effective statistical models for syntactic
  and semantic disambiguation*. Stanford University.

