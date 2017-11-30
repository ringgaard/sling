# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License")

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

"""Convert WordNet to SLING.
"""

import sys
from nltk.corpus import wordnet as wn

def PosName(p):
  if p == 'n': return 'noun'
  if p == 'v': return 'verb'
  if p == 'a': return 'adjective'
  if p == 's': return 'adjective'
  if p == 'r': return 'adverb'
  return p

def StringLiteral(str):
  return '"' + str.replace('\\', '\\\\').replace('"', '\\"') + '"'

def SynsetId(synset):
  pos = synset.pos
  if pos == 's': pos = 'a'
  return '/wn/' + pos + str(synset.offset)

def SenseNumber(lemma):
  i = 1
  for l in wn.lemmas(lemma.name):
    if l is lemma: return i
    if l.synset.pos == lemma.synset.pos: i = i + 1
  return 9999

def SenseId(lemma):
  name = lemma.name.replace('\'', '_').replace('/', '_').replace('.', '_')
  number = SenseNumber(lemma)
  pos = lemma.synset.pos
  if pos == 's': pos = 'a'
  return '/wn/' + pos + '/' + name + '/' + str(number)

sense_mids = {}
synset_mids = {}
fin = open("data/nlp/wordnet/sense-mids.txt", "r")
for line in fin.readlines():
  line = line.strip().replace("/s/", "/a/").replace(".", "_").replace('\'', '_')
  f = line.split('\t')
  sense_mids[f[0]] = f[1]
  synset_mids[f[0]] = f[2]
fin.close()

freebase_mapping = {}
mapping_files = [
  'data/nlp/wordnet/original-wordnet-mapping',
  'data/nlp/wordnet/wordnet-mapping'
]
for mapfile in mapping_files:
  fin = open(mapfile, "r")
  for line in fin.readlines():
    line = line.strip()
    if len(line) == 0 or line[0] == '#': continue
    f = line.split(' ')
    freebase_mapping[f[0]] = f[1]
  fin.close()

synsets = []
words = {}
for s in wn.all_synsets():
  pos = PosName(s.pos)
  ssid = SynsetId(s)
  synsets.append(ssid)

  print '{=' + ssid + ' :/wn/synset'
  print '  /wn/synset/pos: ' + pos
  print '  /wn/synset/gloss: ' + StringLiteral(s.definition)

  names = s.lemma_names

  if len(s.lemmas) > 0:
    sid = SenseId(s.lemmas[0])
    synset_mid = synset_mids.get(sid.lower())
    if synset_mid != None:
      print '  mid: ' + synset_mid
      mapping = freebase_mapping.get(synset_mid)
      if mapping != None and mapping != "-":
        if mapping[0] == '-':
          print '  /wn/synset/broader_freebase_topic: ' + mapping[1:]
        else:
          print '  /wn/synset/equivalent_freebase_topic: ' + mapping

  for l in s.lemmas:
    print '  /wn/synset/sense: ' + SenseId(l)

  # Add hypernyms as parent types
  for h in s.hypernyms(): print '  +' + SynsetId(h)

  for h in s.hypernyms():
    print '  /wn/synset/hypernym: ' + SynsetId(h)
  for h in s.hyponyms():
    print '  /wn/synset/hyponym: ' + SynsetId(h)
  for h in s.instance_hypernyms():
    print '  /wn/synset/instance_hypernym: ' + SynsetId(h)
  for h in s.instance_hyponyms():
    print '  /wn/synset/instance_hyponym: ' + SynsetId(h)
  for h in s.member_holonyms():
    print '  /wn/synset/member_holonym: ' + SynsetId(h)
  for h in s.substance_holonyms():
    print '  /wn/synset/substance_holonym: ' + SynsetId(h)
  for h in s.part_holonyms():
    print '  /wn/synset/part_holonym: ' + SynsetId(h)
  for h in s.member_meronyms():
    print '  /wn/synset/member_meronym: ' + SynsetId(h)
  for h in s.substance_meronyms():
    print '  /wn/synset/substance_meronym: ' + SynsetId(h)
  for h in s.part_meronyms():
    print '  /wn/synset/part_meronym: ' + SynsetId(h)
  for h in s.topic_domains():
    print '  /wn/synset/topic_domain: ' + SynsetId(h)
  for h in s.region_domains():
    print '  /wn/synset/region_domain: ' + SynsetId(h)
  for h in s.usage_domains():
    print '  /wn/synset/usage_domain: ' + SynsetId(h)
  for h in s.attributes():
    print '  /wn/synset/attribute: ' + SynsetId(h)
  for h in s.entailments():
    print '  /wn/synset/entailment: ' + SynsetId(h)
  for h in s.causes():
    print '  /wn/synset/cause: ' + SynsetId(h)
  for h in s.also_sees():
    print '  /wn/synset/see_also: ' + SynsetId(h)
  for h in s.verb_groups():
    print '  /wn/synset/verb_group: ' + SynsetId(h)
  for h in s.similar_tos():
    print '  /wn/synset/similar_to: ' + SynsetId(h)

  print '}'

  i = 0
  names = s.lemma_names
  for l in s.lemmas:
    word = names[i].replace('_', ' ')
    sid = SenseId(l)

    print '{=' + sid + ' :/wn/sense'
    print '  /wn/sense/pos: ' + pos
    print '  /wn/sense/lemma: ' + StringLiteral(word)
    print '  /wn/sense/number: ' + str(SenseNumber(l))
    print '  /wn/sense/key: ' + StringLiteral(l.key)
    print '  /wn/sense/synset: ' + ssid

    sense_mid = sense_mids.get(sid.lower())
    if sense_mid != None:
      print '  mid: ' + sense_mid

    for h in l.antonyms():
      print '  /wn/sense/antonym: ' + SenseId(h)
    for h in l.pertainyms():
      print '  /wn/sense/pertainym: ' + SenseId(h)
    for h in l.derivationally_related_forms():
      print '  /wn/sense/derivationally_related_form: ' + SenseId(h)

    print '}'
    i = i + 1
    if not word in words: words[word] = []
    words[word].append(sid)

  print ''

print '{=/wn/all'
print '  /wn/all/synsets: ['
for s in synsets: print "    " + s
print '  ]'
print '  /wn/all/lemmas: ['
for w in words:
  print '    {:/wn/lemma /wn/lemma/word: ' + StringLiteral(w),
  words[w].sort()
  for s in words[w]: print '/wn/lemma/sense: ' + s,
  print '}'

print '  ]'
print '}'

