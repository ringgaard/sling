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
factex = sling.FactExtractor(kb)
n_page_item = kb["/wp/page/item"]
n_popularity = kb["/w/item/popularity"]
n_fanin = kb["/w/item/fanin"]
kb.freeze()

form_penalty = 0.1
base_context_score = 1e-3
thematic_weight = 0.0

num_docs = 0
num_mentions = 0
num_unknown = 0
num_first = 0
num_at_rank = [0] * 20
for doc in sling.Corpus("local/data/e/wiki/en/documents@10.rec", commons=kb):
  num_docs += 1
  print "Document", num_docs, ":", doc.frame["title"]
  store = doc.store

  # Add document entity to context model.
  context = {}
  item = doc.frame[n_page_item]
  context[item] = context.get(item, 0.0) + 1.0
  for fact in factex.facts(store, item):
    target = fact[-1]
    fanin = target[n_fanin]
    if fanin == None: fanin = 1
    context[target] = context.get(target, 0.0) + 1.0 / fanin

  # Add unanchored links to context model.
  if thematic_weight > 0:
    for theme in doc.themes:
      if type(theme) is not sling.Frame: continue
      theme = theme.resolve()
      popularity = item[n_popularity]
      if popularity == None: popularity = 1
      context[theme] = context.get(theme, 0.0) + thematic_weight / popularity

  # Try to score and rank all linked mentions.
  for mention in doc.mentions:
    # Get phrase and case form.
    phrase = doc.phrase(mention.begin, mention.end)
    initial = doc.tokens[mention.begin].brk >= sling.SENTENCE_BREAK
    form = phrasetab.form(phrase)
    if initial and form == sling.CASE_TITLE: form = sling.CASE_NONE

    matches = phrasetab.query(phrase)
    for item in mention.evokes():
      if item.isanonymous(): continue;

      # Score all matches.
      scores = []
      score_sum = 0.0
      found = False
      for m in matches:
        # Compute score for match.
        score = context.get(m.item(), base_context_score)
        for fact in factex.facts(store, m.item()):
          target = fact[-1]
          score += context.get(target, 0.0)
        if not compatible(form, m.form()): score *= form_penalty
        #scores.append((m, score * m.count(), score))
        scores.append((m, 1.0 * m.count(), score))
        if m.item() == item: found = True

      # Ignore mention if it is not in the prase table.
      if not found:
        num_unknown += 1
        continue
      num_mentions += 1

      # Sort scores.
      scores.sort(key=lambda t: t[1], reverse=True)

      # Output ranked entities.
      rank = 0
      if scores[0][0].item() != item:
        #print phrase, item.id
        for s in scores:
          m = s[0]
          candidate = m.item()
          score = s[1]
          cscore = s[2]
          #print "  %g %s %d %g %s" % (score, formname[m.form()], m.count(),
          #                            cscore, item_text(candidate))
          if candidate == item: break
          rank += 1

      if rank >= len(num_at_rank): rank = len(num_at_rank) - 1
      num_at_rank[rank] += 1

      # Update context model.
      popularity = item[n_popularity]
      if popularity == None: popularity = 1
      context[item] = context.get(item, 0.0) + 1.0 / popularity
      for fact in factex.facts(store, item):
        target = fact[-1]
        fanin = target[n_fanin]
        if fanin == None: fanin = 1
        context[target] = context.get(target, 0.0) + 1.0 / fanin

  if num_docs == 1000: break
  #print

print num_mentions, "mentions"
print num_unknown, "unknown"
total_mentions = num_mentions + num_unknown
print 100.0 * num_mentions / total_mentions, "% coverage"

cummulative = 0.0
for r in range(0, len(num_at_rank)):
  if num_at_rank[r] == 0: continue
  cummulative += num_at_rank[r]
  print "p@%d: %.2f%%" % (r + 1, 100.0 * cummulative / num_mentions)

