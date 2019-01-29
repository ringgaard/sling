import sling
import sling.flags as flags

flags.parse()

kb = sling.Store()
kb.lockgc()
kb.load("local/data/e/wiki/kb.sling")
extractor = sling.FactExtractor(kb)
n_id = kb["id"]
n_fanin = kb["/w/item/fanin"]
kb.freeze()

n = 0
fanin = {}
for item in kb:
  store = sling.Store(kb)
  facts = extractor.facts(store, item)
  for fact in facts:
    target = fact[-1]
    qid = str(target[n_id])
    fanin[qid] = fanin.get(qid, 0) + 1
  n += 1
  if n % 1000000 == 0: print n

store = sling.Store(kb)
recout = sling.RecordWriter("local/data/e/wiki/fanin.rec")
for target, count in fanin.iteritems():
  f = store.frame({n_fanin: count})
  recout.write(target, f.data(binary=True))

