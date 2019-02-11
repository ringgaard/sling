import sling
import sling.flags as flags

flags.define("--lex")
flags.define("--lang", default="en")
flags.parse()

formname = ["  ", "UP", "lo", "Ca"]

stopwords = ["he", "his", "He", "His", "she", "She", "her", "Her"]

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

def item_name(item):
  name = item.name
  if name == None: name = item.id
  if name == None: name = "?"
  return name

kb = sling.Store()
kb.lockgc()
kb.load("local/data/e/wiki/kb.sling")
langdir = "local/data/e/wiki/" + flags.arg.lang
phrasetab = sling.PhraseTable(kb, langdir + "/phrase-table.repo")
n_page_item = kb["/wp/page/item"]
n_popularity = kb["/w/item/popularity"]
n_links = kb["/w/item/links"]
kb.freeze()

form_penalty = 0.1
base_context_score = 1e-3
topic_weight = 1.0
mention_weight = 100.0
context_threshold = 0.01

store = sling.Store(kb)
doc = sling.lex(open(flags.arg.lex).read(), store=store)
context = {}

def add_mention(item):
  popularity = item[n_popularity]
  if popularity == None: popularity = 1
  context[item] = context.get(item, 0.0) + mention_weight / popularity

  links = item[n_links]
  if links != None:
    for link, count in links:
      popularity = link[n_popularity]
      if popularity == None: popularity = 1
      context[link] = context.get(link, 0.0) + float(count) / popularity

# Build context model.
num_mentions = 0
for mention in doc.mentions:
  # Add annotated phrases to context model.
  first = True
  for f in mention.evokes():
    if type(f) is sling.Frame and f.isnamed():
      add_mention(f)
      if first:
        context[f] = context.get(f) + 1.0
        first = False

# Score unannotated spans.
for mention in doc.mentions:
  # Skip mentions that have already been annotated.
  annotated = False
  for f in mention.evokes(): annotated = True
  if annotated: continue

  # Get phrase and case form.
  phrase = doc.phrase(mention.begin, mention.end)
  initial = mention.begin < len(doc.tokens) and \
            doc.tokens[mention.begin].brk >= sling.SENTENCE_BREAK
  form = phrasetab.form(phrase)
  if initial and form == sling.CASE_TITLE: form = sling.CASE_NONE

  if phrase in stopwords: continue
  matches = phrasetab.query(phrase)

  # Score all matches.
  scores = []
  for m in matches:
    # Compute score for match.
    cscore = context.get(m.item(), base_context_score)
    links = m.item()[n_links]
    if links != None:
      for link,count in links:
        weight = context.get(link, 0.0)
        cscore += weight * count
    if not compatible(form, m.form()): cscore *= form_penalty
    score = cscore * m.count()
    scores.append((m, score, cscore))

  # Ignore mention if it is not in the prase table.
  if len(scores) == 0:
    print "N/A", phrase
    continue

  # Sort scores.
  scores.sort(key=lambda t: t[1], reverse=True)

  # Output ranked entities.
  winner = scores[0][0].item()
  winner_cscore = scores[0][2]
  if winner_cscore < 1.0:
    print "Skip", phrase
  else:
    print phrase
    rank = 0
    for s in scores:
      m = s[0]
      candidate = m.item()
      score = s[1]
      cscore = s[2]

      print "%s%11.4f %s %5d %8.4f %s" % ("*" if cscore >= context_threshold else " ",
                                          score, formname[m.form()],
                                          m.count(), cscore,
                                          item_text(candidate))
      rank += 1
      if rank > 3: break

  # Update context model.
  #if form != sling.CASE_LOWER or winner_cscore >= context_threshold:
  #  print "Update ", item_text(winner)
  #  add_mention(winner)

