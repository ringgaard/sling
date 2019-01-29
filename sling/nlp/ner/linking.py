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
      print "  %5d %s %s %s [%s] %s pop: %d fanin: %d support: %d" % (
        m.count(),
        "*" if support > 0 else " ",
        formname[m.form()],
        item_text(item),
        item_type.name if item_type != None else "?",
        "" if m.reliable else "(noisy)", popularity, fanin, support)

    if best != None:
      context[best] = context.get(best, 0) + 100
      for fact in extractor.facts(store, best):
        target = fact[-1]
        context[target] = context.get(target, 0) + 1

  else:
    for f in evokes:
      if type(f) is sling.Frame and n_id in f:
        print phrase, ":", item_text(f)
        context[f] = context.get(f, 0) + 500
        for fact in extractor.facts(store, f):
          target = fact[-1]
          context[target] = context.get(target, 0) + 5
      else:
        print phrase, ":", f

