import sling
import sling.flags as flags
import string

wikidir = "local/data/e/wiki"

stop_words = [
  'of', 'in', 'from', 'at', 'by', 'for', 'to', 'on',
  'the', 'and',
  'le', 'la', 'de', 'des', 'et',
  'der', 'die', 'das', 'und',
  '--',
] + list(string.punctuation)

def item_description(item):
  text = item.id + " " + item.name
  description = item.description
  if description != None: text = text + " (" + description + ")"
  return text

def increment_key(dictionary, key, delta=1):
  dictionary[key] = dictionary.get(key, 0) + delta

def fact_to_text(fact):
  l = []
  for f in fact: l.append(str(f.name))
  return ": ".join(l)

commons = sling.Store()
commons.lockgc()
commons.load(wikidir + "/kb.sling", snapshot=True)
n_is = commons["is"]
extractor = sling.FactExtractor(commons)
phrasetab = sling.PhraseTable(commons, wikidir + "/en/phrase-table.repo")
commons.freeze()

class Name:
  def __init__(self, doc):
    self.doc = doc
    self.store = doc.frame.store()
    self.covered = [False] * len(doc.tokens)
    self.evokes = {}
    self.matched = set()
    self.skip = []
    for t in self.doc.tokens: self.skip.append(t.word in stop_words)

  def overlaps(self, begin, end):
    for i in xrange(begin, end):
      if self.covered[i]: return True
    return False

  def evoke(self, item, begin, end):
    self.evokes[item] = (begin, end)
    self.matched.add(item.resolve())
    for i in xrange(begin, end): self.covered[i] = True


  def match(self, frame, begin, end):
    # Get targets for frame.
    targets = {}
    facts = None
    if frame != None:
      facts = extractor.facts(self.store, frame.resolve())
      for fact in facts:
        target = fact[-1]
        increment_key(targets, target)

    # Try to match subspans.
    subevokes = []
    length = end - begin
    for l in reversed(range(1, length + 1)):
      for b in range(begin, begin + length - l + 1):
        # Skip spans that are already covered.
        if self.overlaps(b, b + l): continue

        # Phrase cannot start or end with a stop word.
        if self.skip[b] or self.skip[b + l -1]: continue

        # Try to find match.
        matches = phrasetab.query(self.doc.phrase(b, b + l))
        for m in matches:
          match = m.item()
          count = m.count()
          if match in self.matched: break
          if frame == None or match in targets:
            # Determine property relation.
            relation = None
            if facts != None:
              props = {}
              for fact in facts:
                target = fact[-1]
                if target == match:
                  increment_key(props, fact)
              for fact in props:
                if relation == None or props[fact] > props[relation]:
                  relation = fact
              #if relation != None:
              #  print "best relation:", fact_to_text(relation)

            # Evoke frame for span.
            subframe = self.store.frame([("is", match)])
            self.doc.evoke(b, b + l, subframe)
            if relation != None and len(relation) == 2:
              # Add relation from frame to subframe.
              frame.append(relation[0], subframe)

            subevokes.append(subframe)
            self.evoke(subframe, b, b + l)
            break

    # Match subspans.
    for subitem in subevokes:
      b, e = self.evokes[subitem]
      for i in xrange(b, e): self.covered[i] = False
      self.match(subitem, b, e)

while True:
  text = raw_input("name: ")

  doc = sling.tokenize(text, store=sling.Store(commons))

  name = Name(doc)
  name.match(None, 0, len(doc.tokens))

  print
  print "Analysis:"
  print doc.tolex()
  print

  phrases = {}
  for m in doc.mentions:
    for f in m.evokes():
      phrases[f] = doc.phrase(m.begin, m.end)

  print "Structure:"
  for m in doc.mentions:
    for f in m.evokes():
      phrase = '"' + doc.phrase(m.begin, m.end) + '"'
      print "  ", phrase, ":", item_description(f.resolve())
      for role, value in f:
        if role == n_is: continue
        target_phrase = '"' + phrases[value] + '"'
        print "    ", role.name, ":", target_phrase
  print

