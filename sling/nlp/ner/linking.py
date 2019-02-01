import sling
import sling.flags as flags

flags.define("--lex")
flags.parse()

formname = ["  ", "UP", "lo", "Ca"]
stopwords = [
  "it", "its", "It", "I",
  "he", "his", "He", "His",
  "she", "her", "She", "Her",
  "they", "their", "They", "Their",
  "we", "our", "We",
  "have", "had", "has", "been", "be", "being",
  "is", "was", "do", "did", "does",
  "where", "for", "but", "also", "while", "that", "from", "who",
  "or", "and", "not", "no",
  "On", "on",
  "This", "That",
]

def compatible(form1, form2):
  if form1 == sling.CASE_NONE: return True
  if form2 == sling.CASE_NONE: return True
  return form1 == form2

def item_text(item):
  s = item.id
  name = item.name
  if name != None: s = s + " " + name
  descr = item.description
  if descr != None:
    if len(descr) > 40:
      s += " (" + descr[:40] + "...)"
    else:
      s += " (" + descr + ")"
  return s

kb = sling.Store()
kb.lockgc()
kb.load("local/data/e/wiki/kb.sling")
extractor = sling.FactExtractor(kb)
taxonomy = extractor.taxonomy()
phrasetab = sling.PhraseTable(kb, "local/data/e/wiki/en/phrase-table.repo")
n_id = kb["id"]
n_fanin = kb["/w/item/fanin"]
n_popularity = kb["/w/item/popularity"]
n_links = kb["/w/item/links"]
kb.freeze()

store = sling.Store(kb)
doc = sling.lex(open(flags.arg.lex).read(), store=store)

context = {}

for mention in doc.mentions:
  evokes = list(mention.evokes())
  phrase = doc.phrase(mention.begin, mention.end)
  if phrase in stopwords: continue
  if len(evokes) == 0:
    print phrase
    matches = phrasetab.query(phrase)
    form = phrasetab.form(phrase)
    best = None
    for m in matches:
      if not compatible(form, m.form()): continue;
      item = m.item()
      popularity = item[n_popularity]
      fanin = item[n_fanin]
      if popularity == None: popularity = 0
      if fanin == None: fanin = 0
      item_type = taxonomy.classify(item)
      support = context.get(item, 0)
      if best == None and support > 0: best = item

      context_score = 0.0
      for fact in extractor.facts(store, item):
        target = fact[-1]
        target_fanin = target[n_fanin]
        if target_fanin == None: target_fanin = 1
        target_prominence = context.get(target, 0)
        if target_prominence > 0:
          score = target_prominence / target_fanin
          context_score += score
          #if score > 1e-4:
          #  print "        fact context:", score, item_text(target)

      print "  %5d %s %s %s [%s] %s pop: %d fanin: %d sup: %d ctx: %f" % (
        m.count(),
        "*" if support > 0 else " ",
        formname[m.form()],
        item_text(item),
        item_type.name if item_type != None else "?",
        "" if m.reliable else "(noisy)",
        popularity,
        fanin,
        support,
        context_score)

      #links = item[n_links]
      #if links:
      #  for l,c in links:
      #    if l in context: print "        link context:", item_text(l)

    if best != None:
      context[best] = context.get(best, 0.0) + 100.0
      for fact in extractor.facts(store, best):
        target = fact[-1]
        if target != item:
          context[target] = context.get(target, 0.0) + 1.0

  else:
    for f in evokes:
      if type(f) is sling.Frame and n_id in f:
        print phrase, ":", item_text(f)
        context[f] = context.get(f, 0.0) + 500.0
        for fact in extractor.facts(store, f):
          target = fact[-1]
          if target != f:
            context[target] = context.get(target, 0.0) + 5.0
        links = f[n_links]
        if links:
          for l, c in links:
            #print "link", item_text(l)
            context[l] = context.get(l, 0.0) + 3.0
      else:
        print phrase, ":", f

