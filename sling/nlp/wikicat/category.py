import sling
import sling.flags as flags
import string
import time

wikidir = "local/data/e/wiki"

prior_weight = 0.5

stop_words = [
  'of', 'in', 'from', 'at', 'by', 'for', 'to', 'on',
  'the', 'and',
  'le', 'la', 'de', 'des', 'et',
  'der', 'die', 'das', 'und',
  '--',
] + list(string.punctuation)

english_subject_types = {
  # Humans.
  "people" : "Q5",
  "births": "Q5",
  "deaths": "Q5",
  "players" : "Q5",
  "alumni" : "Q5",
  "expatriates": "Q5",

  "women": "Q6581072",         # female
  "establishments": "Q43229",  # organization
}

subject_properties = [
  'P31',   # instance of
  'P106',  # occupation
  'P39',   # position held
  #'P21',   # sex or gender
]

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

  for suffix, replacement in (("ies ", "y "), ("es ", " "), ("s ", " ")):
    pos = phrase.find(suffix)
    if pos != -1: return phrase.replace(suffix, replacement)

  return phrase

def item_description(item):
  text = item.id + " " + item.name
  description = item.description
  if description != None: text = text + " (" + description + ")"
  return text

class Phrase:
  def __init__(self, begin, end, item, score):
    self.begin = begin
    self.end = end
    self.item = item
    self.score = score

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
    self.num_members = 0
    for member in self.members(cats.n_item_member):
      if cats.is_category(member): continue
      member_facts = self.cats.extractor.extract_facts(self.store, member)
      member_targets = set()
      for fact in member_facts:
        target = fact[-1]
        increment_key(self.facts, fact)
        member_targets.add(target)
      for target in member_targets: increment_key(self.targets, target)
      self.num_members += 1

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
        matches = []
        subphrase = self.doc.phrase(b, b + l)
        items = self.cats.phrasetab.query(subphrase)
        matched = False
        for item, count in items:
          if item in self.targets:
            matches.append(Phrase(b, b + l, item, count))
            matched = True

        lemma = stem(subphrase)
        if not matched and lemma != subphrase:
          # Try to match stemmed phrase.
          items = self.cats.phrasetab.query(lemma)
          for item, count in items:
            if item in self.targets:
              matches.append(Phrase(b, b + l, item, count))
              matched = True

        # Try to match special category subjects.
        item = self.cats.subjects.get(subphrase.lower())
        if item != None and item in self.targets:
          matches.append(Phrase(b, b + l, item, 1000))

        # Normalize phrase scores.
        if len(matches) > 0:
          total = 0.0
          for p in matches: total += p.score
          for p in matches: p.score /= total
          matches.sort(key=lambda x: x.score, reverse=True)
          self.phrases.extend(matches)

  def build_span_cover(self):
    self.subject  = None
    self.frames = {}
    covered = [False] * len(self.doc.tokens)
    for phrase in self.phrases:
      skip = False
      for i in xrange(phrase.begin, phrase.end):
        if covered[i]: skip = True
      if skip: continue
      frame = self.store.frame({"is": phrase.item})
      self.doc.evoke(phrase.begin, phrase.end, frame)
      self.frames[phrase.item] = frame
      for i in xrange(phrase.begin, phrase.end): covered[i] = True

  def target_facts(self, item):
    dist = {}
    for fact, count in self.facts.iteritems():
      if len(fact) != 2: continue  # TODO: handle complex facts
      if fact[-1] == item: increment_key(dist, fact[0], count)
    return dist

  def find_subject(self):
    best = None
    highscore = 0
    for item, frame in self.frames.iteritems():
      score = 0
      for property, count in self.target_facts(item).iteritems():
        if property in self.cats.subject_properties:
          score += count
      if best == None or score > highscore:
        best = frame
        highscore = score

    self.subject = best
    if best != None:
      best.append("isa", self.cats.n_subject)

  def annotate_relations(self):
    if self.subject == None: return
    for item, frame in self.frames.iteritems():
      # Determine most common relation between members and item
      property_scores = {}
      for prop, count in self.target_facts(item).iteritems():
        increment_key(property_scores, prop, count)

      best = None
      highscore = 0
      for prop, score in property_scores.iteritems():
        if best == None or score > highscore:
          best = prop
          highscore = score
      if best == None: continue

      if frame == self.subject:
        self.subject.append(best, item)
      else:
        self.subject.append(best, frame)

  def find_relations(self):
    self.relations = {}
    for item, frame in self.frames.iteritems():
      # Determine most common relation between members and item
      property_scores = {}
      for prop, count in self.target_facts(item).iteritems():
        increment_key(property_scores, prop, count)

      best = None
      highscore = 0
      for prop, score in property_scores.iteritems():
        if best == None or score > highscore:
          best = prop
          highscore = score
      if best == None: continue

      #self.relations[best] = item
      self.relations[best] = frame

    self.doc.add_theme(self.store.frame(self.relations))

  def target_score(self, item):
    return self.targets.get(item, 0.0) / float(self.num_members)

class Categories:
  def __init__(self):
    # Initialize commons store with knowledge base.
    start = time.time()
    self.commons = sling.Store()
    self.commons.lockgc()
    self.commons.load(wikidir + "/kb.sling", snapshot=True)
    self.n_item_member = self.commons['/w/item/member']
    self.n_instance_of = self.commons['P31']
    self.n_wikimedia_category = self.commons['Q4167836']
    self.n_subject = self.commons['subject']
    self.extractor = sling.FactExtractor(self.commons)

    # Add category subject types.
    self.subjects = {}
    for subject, item in english_subject_types.iteritems():
      self.subjects[subject] = self.commons[item]

    # Add properties for subjects.
    self.subject_properties = []
    for p in subject_properties: self.subject_properties.append(self.commons[p])

    self.commons.freeze()
    end = time.time()
    print end - start, "secs loading commons"

    # Load phrase table.
    # TODO(ringgaard): Load language-dependent phrase table.
    start = time.time()
    self.phrasetab = sling.PhraseTable(self.commons,
                                       wikidir + "/en/phrase-table.repo")
    end = time.time()
    print end - start, "secs loading phrase table"

    # Open category member database.
    self.member_db = sling.RecordDatabase(wikidir + "/wikipedia-members.rec")

  def analyze(self, catid):
    category = Category(self, catid)
    category.build_phrase_spans()
    category.build_span_cover()
    #category.find_subject()
    #category.annotate_relations()
    category.find_relations()
    return category

  def is_category(self, item):
    return self.n_wikimedia_category in item(self.n_instance_of)


flags.parse()

print "Load categories"
categories = Categories()
n_is = categories.commons["is"]
n_isa = categories.commons["isa"]

while True:
  catid = raw_input("category: ")
  category = categories.analyze(catid)
  print len(category.members), "members", len(category.targets), "targets"
  print

  print "Title:", category.doc.text
  print "Analysis:", category.doc.tolex()
  print
  if category.subject != None:
    for role, value in category.subject:
      if role == n_is:
        print "subject:", item_description(value)
      elif role != n_isa:
        print role.id, role.name, ":", item_description(value.resolve())
    print

  if category.relations != None:
    for role, value in category.relations.iteritems():
      print role.name + " (" + role.id + "): " + item_description(value.resolve())
    print

  print category.doc.frame.data(pretty=True)

  print "Span matches:"
  for phrase in category.phrases:
    text = category.doc.phrase(phrase.begin, phrase.end)
    ps = phrase.score
    ts = category.target_score(phrase.item)
    f1 = 2 * ps * ts / (ps + ts)
    print "  '%s' prior: %.1f%% target: %.1f%% f1: %.1f%% item: %s" % (text, ps * 100.0, ts * 100.0, f1 * 100.0, item_description(phrase.item))
  print

  print "Frames:"
  for item, frame in category.frames.iteritems():
    print "  ", item.id, item.name
    dist = category.target_facts(item)
    for property, count in sorted(dist.iteritems(), reverse=True, key=lambda (k,v): v):
      print "    ", count, property.id, property.name
  print

