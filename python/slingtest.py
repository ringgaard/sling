import sys
sys.path.append(".")

import python.pysling as sling

#print sling.helloworld()

commons = sling.Store()
commons.load("data/nlp/schemas/meta-schema.sling")
commons.load("data/nlp/schemas/document-schema.sling")

#fin = open("data/nlp/schemas/document-schema.sling", "r")
#commons.load(fin)
#fin.close()

commons.freeze()
#print "commons size", len(commons)

#id = commons['id']

store = sling.Store(commons)

doc = store['/s/document']
#print "doc", doc
#print "len(doc)", len(doc)
#print "doc.handle()", doc.handle()
#print "doc.store()", doc.store()
#print "doc.id", doc.id
#print "doc.name", doc.name

for v in doc('role'):
  print "doc role", v, v.data()

#if 'name' in doc: print "doc has name"
#if id in doc: print "doc has id"
#if doc in doc: print "doc has doc"
#if 'xxx' in doc: print "doc has xxx"

#print "name", doc['name']

#family = doc['family']
#print "family", family[id]

#if '/s/token' in commons: print "commons has token"
#if '/s/token' in store: print "store has token"
#if 'xxx' in store: print "store has xxx"

f1 = store.parse('{a:10 b:10.5 c:"hello" d:{:test} e:[1,2,3]}')
f1.c = "hello world"
f1['name'] = "HELLO"
print "f1", f1
#print "f1.data()", f1.data()
#print "len(f1.data(binary=True))", len(f1.data(binary=True))
#print "f1.d", f1.d
#print "f1.e", f1.e

for n, v in f1:
  print "slot", n,"=", v

#print "save"
#commons.save("/tmp/xxx", pretty=True)

#ea = f1.e
#ea = f1['e']
#e = store.parse('[1,2,3]')
#print "type", type(ea)
#n = len(ea)
#print "range", range(n)
#for index in range(n):
#  print index
#  v = ea[index]
#  print "ea", index, ":", v
#print "done"

#ea = f1.e
#ea = f1['e']
#ea = store.parse('[1,2,3]')
#for i in [0, 1, 2]:
#  print "e", i, ea[i]
#print "done"

for i in range(len(f1.e)):
  print "e", i, f1.e[i]

f1.e[1] = "hello"

for item in f1.e:
  print "item", item

#f2 = store.parse(f1.ascii())
#print "f2", f2

#f3 = store.parse(f1.binary())
#print "f3", f3

for frame in store:
  print "store:", frame.id

