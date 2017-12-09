import sling

commons = sling.Store()
commons.load("data/nlp/schemas/meta-schema.sling")
commons.load("data/nlp/schemas/document-schema.sling")
actions = commons.load("local/sempar/out/table", binary=True)

commons.freeze()
id = commons['id']
name = commons['name']

store = sling.Store(commons)

doc = store['/s/document']

print "doc", doc
print "len(doc)", len(doc)
print "doc hash", hash(doc)
print "doc.store()", doc.store()
print "doc.id", doc.id
print "doc.name", doc.name
print "doc[name]", doc[name]
print "type(doc)", type(doc)

for v in doc('role'):
  print "doc role", v, v.data()

if 'name' in doc: print "doc has 'name'"
if name in doc: print "doc has name"

family = doc['family']
print "family", family

f1 = store.parse('{a:10 b:10.5 c:"hello" d:{:test} e:[1,2,3]}')
f1.c = "hello world"
f1['name'] = "HELLO"
print "f1", f1

f1.d.append(name, "I am d")
f1.append("r10", f1)

print "f1.data()", f1.data()
print "len(f1.data(binary=True))", len(f1.data(binary=True))

for n, v in f1:
  print "slot", n,"=", v

for i in range(len(f1.e)):
  print "e", i, f1.e[i]

f1.e[1] = "hello"

for item in f1.e:
  print "item", item

f2 = store.parse(f1.data())
print "f2", f2

f3 = store.parse(f1.data(binary=True))
print "f3", f3

f4 = store.frame({
  name: 'f4',
  'value': 7,
  'bool': True,
  'doc': doc,
  'arr' : [1, 2, 3, 'four'],
  'sub' : {name: 'foo', 'a': 10},
})
print "f4", f4

f5 = store.frame([
  (id, "f5"),
  (name, 'f5'),
  ('value', 7),
  ('bool', True),
  ('doc', doc)
])
print "f5", f5.data()
f5.extend({'foo': 10, 'bar': 20})

arr = store.array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
print "arr", type(arr), arr
if 5 in arr: print "5 is in arr"
if 100 in arr: print "100 is in arr!!!!!!!!!!"
if f5 == store["f5"]: print "f5 is f5"
if f5 == doc: print "f5 is doc!!!"

f5.list = arr
f5.empty = store.array(5)
print "f5 extended", f5.data()

for frame in store:
  print "symbol:", frame

store.save("/tmp/xxx", pretty=True)


