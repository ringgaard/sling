# Copyright 2023 Ringgaard Research ApS
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

# Analyze document using BERT NER pipeline.

import sling
import sling.flags as flags
import sling.log as log

import torch
from transformers import AutoModelForTokenClassification
from transformers import AutoTokenizer
from transformers import AutoConfig

flags.define("--port", default=8123, type=int, metavar="PORT")
flags.define("--clear", default=False, action="store_true")
flags.define("--cluster", default=False, action="store_true")
flags.define("--debug", default=False, action="store_true")

commons = sling.Store()
n_is = commons["is"]
n_isa = commons["isa"]
n_person = commons["Q5"]
n_location = commons["Q109377685"]
n_organization = commons["Q43229"]
n_misc = commons["Q35120"]

schema = sling.DocumentSchema(commons)

commons.freeze()

BEGIN = 0
INSIDE = 1
OUTSIDE = 2

bio_tags = [
  (OUTSIDE, None, "O"),
  (BEGIN, n_person, "B-PER"),
  (INSIDE, n_person, "I-PER"),
  (BEGIN, n_location, "B-LOC"),
  (INSIDE, n_location, "I-LOC"),
  (BEGIN, n_organization, "B-ORG"),
  (INSIDE, n_organization, "I-ORG"),
  (BEGIN, n_misc, "B-MISC"),
  (INSIDE, n_misc, "I-MISC"),
]

def valid(prev, next):
  if prev[0] == OUTSIDE:
    return next[0] != INSIDE
  if next[0] == INSIDE:
    return prev[1] == next[1] and prev[0] == BEGIN or prev[0] == INSIDE
  return True

def add_mention(doc, markables, begin, end, label):
  if begin == end: return
  if flags.arg.debug: print("SPAN", begin, end, doc.phrase(begin, end))

  mention = doc.add_mention(begin, end)
  if markables is not None:
    phrase = doc.phrase(begin, end)
    markable = markables.get(phrase)
    if markable:
      evokes = list(markable.evokes())
      if len(evokes) == 0:
        referent = doc.store.frame([])
        markable.evoke(referent)
      else:
        referent = evokes[0]
      mention.evoke(doc.store.frame({n_is: referent}))
    markables[phrase] = mention

class BertModel:
  def __init__(self, name):
    self.name = name
    self.model = None
    self.tokenizer = None
    self.uncased = False
    self.cls = None
    self.sep = None
    self.labelmap = [-1] * len(bio_tags)

  def load(self):
    if self.model is not None: return
    print("loading", self.name)
    self.model = AutoModelForTokenClassification.from_pretrained(self.name)
    self.tokenizer = AutoTokenizer.from_pretrained(self.name)
    self.config = AutoConfig.from_pretrained(self.name)
    self.uncased = self.tokenizer.do_lower_case

    self.subtokenizer = sling.Subtokenizer(self.vocab())
    self.cls = self.subtokenizer("[CLS]")
    self.sep = self.subtokenizer("[SEP]")

    for i in range(len(self.labelmap)):
      labelid = self.config.label2id[bio_tags[i][2]]
      self.labelmap[labelid] = i

  def vocab(self):
    vocabulary = [""] * self.config.vocab_size
    for subword, index in self.tokenizer.vocab.items():
        vocabulary[index] = subword
    return "\n".join(vocabulary) + "\n"

  def analyze(self, doc):
    markables = {} if flags.arg.cluster else None
    for start, end in doc.sentences():
      # Build subword tokens and token mapping.
      tokens = [self.cls]
      tokenmap = [start]
      for t in range(start, end):
        word = doc.tokens[t].word
        if self.uncased: word = word.lower()
        subwords = self.subtokenizer.split(word)
        tokens.extend(subwords)
        for _ in range(len(subwords)): tokenmap.append(t)
      tokens.append(self.sep)
      tokenmap.append(end)

      # Run model to get predictions.
      inputs = torch.tensor(tokens).unsqueeze(0)
      outputs = self.model(inputs)[0]

      # Decode valid BIO tag sequence from predictions.
      rankings = torch.topk(outputs, len(bio_tags), dim=2).indices[0]
      predictions = []
      prev = bio_tags[0]
      for ranking in rankings:
        for label in ranking:
          next = bio_tags[self.labelmap[label]]
          if valid(prev, next):
            predictions.append(next)
            prev = next
            break

      if flags.arg.debug:
        print(doc.phrase(start, end))
        s = []
        for t in range(len(predictions)):
          action, label, tag = predictions[t]
          if action == OUTSIDE:
            s.append(self.subtokenizer(tokens[t]))
          else:
            s.append(self.subtokenizer(tokens[t]) + "/" + tag)
        print("NER:", " ".join(s))

      begin = None
      tag = None
      for t in range(len(predictions)):
        action, label, _ = predictions[t]
        if action == BEGIN:
          if begin is not None:
            add_mention(doc, markables, tokenmap[begin], tokenmap[t], tag)
          begin = t
          tag = label
        elif action == OUTSIDE:
          if begin is not None:
            b = tokenmap[begin]
            e = tokenmap[t]
            if t > 0 and e == tokenmap[t - 1]: e += 1
            add_mention(doc, markables, b, e, tag)
          begin = None

      if begin is not None:
        add_mention(doc, markables, tokenmap[begin], end, tag)

models = {
  "en": BertModel("dslim/bert-base-NER"),
  "da": BertModel("alexandrainst/da-ner-base"),
}

if __name__ == "__main__":
  import sling.net

  flags.parse()
  app = sling.net.HTTPServer(flags.arg.port, cors=True)

  @app.route("/analyze", method="POST")
  def media_request(request):
    store = sling.Store(commons)
    doc = sling.lex(request.body.decode(), store, schema)
    language = request["Content-Language"]
    if language is None: language = "en"
    model = models.get(language)
    if model is None: return 406
    model.load()

    if flags.arg.clear: doc.remove_annotations()
    model.analyze(doc)

    return doc.tolex()

  log.info("running")
  app.run()
  log.info("stopped")

