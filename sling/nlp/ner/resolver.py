import sling
import sling.flags as flags

flags.parse()

formname = ["  ", "UP", "lo", "Ca"]
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
phrasetab = sling.PhraseTable(kb, "local/data/e/wiki/en/phrase-table.repo")
kb.freeze()

num_docs = 0
num_mentions = 0
num_unknown = 0
num_first = 0
for doc in sling.Corpus("local/data/e/wiki/en/documents@10.rec", commons=kb):
  print "Document:", doc.frame["title"]
  for mention in doc.mentions:
    phrase = doc.phrase(mention.begin, mention.end)
    matches = phrasetab.query(phrase)
    form = phrasetab.form(phrase)
    for f in mention.evokes():
      if f.isanonymous(): continue;
      rank = 0
      found = False
      for m in matches:
        #if not compatible(form, m.form()): continue;
        item = m.item()
        if item == f:
          found = True
          break
        else:
          rank += 1
      if found:
        num_mentions += 1
        if rank == 0:
          num_first += 1
        else:
          print rank, phrase, f.id
          for i in range(rank + 1):
            m = matches[i]
            print "  ", formname[m.form()], m.count(), item_text(m.item())
      else:
        #print "N/A", phrase, item_text(f)
        num_unknown += 1

  num_docs += 1
  if num_docs == 1000: break

print
print num_docs, "docs"
print num_mentions, "mentions"
print num_first, "first"
print num_unknown, "unknown"
print 100.0 * num_first / num_mentions, "% precision"
print 100.0 * num_unknown / (num_mentions + num_unknown), "% coverage"

