import sling
import string
import traceback

wikidir = "local/data/e/wiki"

stop_words = [
  'of', 'in', 'from', 'at', 'by', 'for', 'to', 'on',
  'the', 'and',
  'le', 'la', 'de',
  '--',
] + list(string.punctuation)

english_subject_types = {
  "people" : "Q5",
  "players" : "Q5",
  "alumni" : "Q5",
  "expatriates": "Q5",
  "establishments": "Q43229",
}

def fact_to_text(fact):
  l = []
  for f in fact: l.append(str(f.name))
  return ": ".join(l)

def increment_key(dictionary, key, delta=1):
  dictionary[key] = dictionary.get(key, 0) + delta

def stem(phrase):
  # Poor mans lemmatizer :)
  if phrase.endswith("ies"): return phrase[:-3] + "y"
  if phrase.endswith("es"): return phrase[:-2]
  if phrase.endswith("s"): return phrase[:-1]
  return phrase

class Phrase:
  def __init__(self, begin, end, item, count):
    self.begin = begin
    self.end = end
    self.item = item
    self.count = count

class Category:
  def __init__(self, cats, catid):
    # Get category from knowledge base.
    self.cats = cats
    self.store = sling.Store(cats.commons)
    self.category = cats.commons[catid]
    if self.category == None: raise Exception("unknown item")
    if not cats.is_category(self.category): raise Exception("not a category")

    # Tokenize category title.
    # TODO(ringgaard): Use language-depedendent category title.
    name = self.category.name
    colon = name.find(':')
    if colon != -1: name = name[colon + 1:]
    self.title = name
    self.doc = sling.tokenize(name, store=self.store)

    # Read members.
    self.members = self.store.parse(cats.member_db.lookup(catid))

    # Build fact distribution for members.
    self.facts = {}
    self.targets = {}
    for member in self.members(cats.n_item_member):
      if cats.is_category(member): continue
      member_facts = self.cats.extractor.extract_facts(self.store, member)
      for fact in member_facts:
        target = fact[-1]
        increment_key(self.facts, fact)
        increment_key(self.targets, target)

  def build_phrase_spans(self):
    # Find stop word tokens.
    skip = []
    for t in self.doc.tokens: skip.append(t.text in stop_words)

    # Find matching phrases.
    self.phrases = []
    length = len(self.doc.tokens)
    for l in reversed(range(1, length + 1)):
      for b in range(0, length - l + 1):
        # Phrase cannot start or end with a stop word.
        if skip[b] or skip[b + l -1]: continue

        # Find matches for sub-phrase.
        subphrase = self.doc.phrase(b, b + l)
        items = self.cats.phrasetab.query(subphrase)
        matched = False
        for item, count in items:
          if item in self.targets:
            self.phrases.append(Phrase(b, b + l, item, count))
            matched = True

        lemma = stem(subphrase)
        if not matched and lemma != subphrase:
          # Try to match stemmed phrase.
          items = self.cats.phrasetab.query(lemma)
          for item, count in items:
            if item in self.targets:
              self.phrases.append(Phrase(b, b + l, item, count))
              matched = True

        if not matched:
          # Try to match special category subjects.
          item = self.cats.subjects.get(subphrase.lower())
          if item != None and item in self.targets:
            self.phrases.append(Phrase(b, b + l, item, 1))

  def build_span_cover(self):
    covered = [False] * len(self.doc.tokens)
    for phrase in self.phrases:
      skip = False
      for i in xrange(phrase.begin, phrase.end):
        if covered[i]: skip = True
      if skip: continue
      frame = self.store.frame({"is": phrase.item})
      self.doc.evoke(phrase.begin, phrase.end, frame)
      for i in xrange(phrase.begin, phrase.end): covered[i] = True

class Categories:
  def __init__(self):
    # Initialize commons store with knowledge base.
    self.commons = sling.Store()
    self.commons.load(wikidir + "/kb.sling")
    self.n_item_member = self.commons['/w/item/member']
    self.n_instance_of = self.commons['P31']
    self.n_wikimedia_category = self.commons['Q4167836']
    self.extractor = sling.FactExtractor(self.commons)
    self.commons.freeze()

    # Add category subject types.
    self.subjects = {}
    for subject, item in english_subject_types.iteritems():
      self.subjects[subject] = self.commons[item]

    # Load phrase table.
    # TODO(ringgaard): Load language-dependent phrase table.
    self.phrasetab = sling.PhraseTable(self.commons, wikidir + "/en/phrase-table.repo")

    # Open category member database.
    self.member_db = sling.RecordDatabase(wikidir + "/wikipedia-members.rec")

  def analyze(self, catid):
    cat = Category(self, catid)
    return cat

  def is_category(self, item):
    return self.n_wikimedia_category in item(self.n_instance_of)


print "Load categories"
categories = Categories()

while True:
  try:
    catid = raw_input("category: ")
    category = categories.analyze(catid)
    category.build_phrase_spans()
    category.build_span_cover()

    print "Analysis:", category.doc.tolex()

    #print "Fact distribution:"
    #for fact, count in sorted(category.facts.iteritems(), reverse=True, key=lambda (k,v): v):
    #  if count > 1:
    #    print "%d: %s %s" % (count, fact_to_text(fact), str(fact))
    #print

    #print "Targets:"
    #for target, count in sorted(category.targets.iteritems(), reverse=True, key=lambda (k,v): v):
    #  if count > 1:
    #    print "%d: %s %s" % (count, target.name, str(target))
    #print

    print "Span matches:"
    for phrase in category.phrases:
      text = category.doc.phrase(phrase.begin, phrase.end)
      print "  '%s' %5d %-10s %s (%s)" % (text, phrase.count, phrase.item.id, phrase.item.name, str(phrase.item.description))

  except KeyboardInterrupt:
    print "Stop"
    break
  except:
    print traceback.format_exc()
