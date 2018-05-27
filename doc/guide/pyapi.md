# SLING Python API

A number of components in SLING can be accessed through the Python SLING API.
You can install the SLING Python wheel using pip:
```
sudo pip install http://www.jbox.dk/sling/sling-2.0.0-cp27-none-linux_x86_64.whl
```
or you can [clone the repo and build SLING from sources](install.md). You can
then link the `sling` Python module directly to the source directory to use it
in "developer mode":
```
sudo ln -s $(realpath python) /usr/lib/python2.7/dist-packages/sling
```
# Table of contents

* [Frame stores](#frame-stores)
* [Phrase tables](#phrase-tables)
* [Record files](#record-files)
* [Documents](#documents)
* [Parsing](#parsing)
* [Miscellaneous](#miscellaneous)

# Frame stores

The SLING [frame store](../../sling/frame/) can be used from Python. See the
[SLING Frames Guide](frames.md) for an introduction to semantic frames and the
SLING frame store concepts.

SLING frames live in a store, so you create a new global store this way:
```
import sling
commons = sling.Store()
```
Loading frames into the store:
```
commons.load("data/nlp/schemas/meta-schema.sling")
commons.load("data/nlp/schemas/document-schema.sling")
```
Loading binary encoded frames in legacy encoding:
```
actions = commons.load("local/sempar/out/table", binary=True)
```
Freezing a store to makes it read-only and allows local stores to be created based on the store:
```
commons.freeze()
```
Looking up frames in the store:
```
name = commons['name']
```
Create a local store:
```
store = sling.Store(commons)
```
Frames in the global store are now accessible from the local store:
```
doc = store['/s/document']
```
Role values for frames can be accessed as attributes:
```
print doc.name
```
or using indexing:
```
print doc['name']
```
You can also use a frame value to access roles:
```
print doc[name]
```
You can test if a frame has a role:
```
if 'name' in doc: print "doc has 'name'"
if name in doc: print "doc has name"
```
The `parse()` method can be used for adding new frames to the store:
```
f = store.parse('{a:10 b:10.5 c:"hello" d:{:/s/thing} e:[1,2,3]}')
```
The `frame()` method can be used to create a new frame from a dictionary:
```
f = store.frame({'a': 10, 'b': 10.5, 'c': "hello"})
```
or a list of slot tuples:
```
f = store.frame([('a', 10), ('b': 10.5), ('c': "hello")])
```
Slots can be added or modified using attribute assignment:
```
f.c = "hello world"
```
or using index assignment:
```
f[name] = "The F frame"
f['a'] = 20
```
New slots can be added using the `append()` method:
```
f.d.append(name, "A thing")
```
Multiple slot can be added using the `extend()` method:
```
f.extend({'foo': 10, 'bar': 20})
```
or using a list of slot tuples:
```
f.extend([('foo', 10), ('bar': 20)])
```
All the slots in a frame can be iterated:
```
for name, value in f:
  print "slot", name,"=", value
```
or just the roles with a particular name:
```
for r in doc('role'):
  print "doc role", r
```
Frames can be encoded in text format with the `data()` method:
```
print f.data()
```
and with indentation:
```
print f.data(pretty=True)
```
or with binary encoding:
```
print len(f.data(binary=True))
```
Arrays can be created with the `array()` method:
```
a = store.array([1, 2, 3])
```
Arrays can also be created with nil values that can be assigned later:
```
a = store.array(3)
a[0] = 1
a[1] = 2
a[2] = 3
```
SLING arrays work much in the same way as Python lists except that they have
a fixed size:
```
print len(a)
print a[1]
for item in a: print item
```
Finally, a store can be save to a file in textual encoding:
```
store.save("/tmp/txt.sling")
```
or in binary encoding:
```
store.save("/tmp/bin.sling", binary=True)
```

## Phrase tables

A phrase table contains a mapping from _names_ to frames. A phrase table for
Wikidata entities is constructed by the `phrase-table` task, and can be used for
fast retrieval of all entities having a (normalized) name matching a phrase:
```
import sling

# Load knowledge base and phrase table.
kb = sling.Store()
kb.load("local/data/e/wiki/kb.sling")
names = sling.PhraseTable(kb, "local/data/e/wiki/en/phrase-table.repo")
kb.freeze()

# Lookup entities with name 'Annette Stroyberg'.
for entity in names.lookup("Annette Stroyberg"):
  print entity.id, entity.name

# Query all entities named 'Funen' with freqency counts.
for entity, count in names.query("Funen"):
  print count, entity.id, entity.name, "(", entity.description, ")"
```

## Record files

[Record files](../../sling/file/recordio.h) are files with variable-size records
each having a key and a value. Individual records are (optionally) compressed
and records are stores in chunks which can be read independently.
The default chunk size is 64 MB.

A `RecordReader` is used for reading records from a record file and supports
iteration over all the records in the record file:
```
import sling

recin = sling.RecordReader("test.rec")
for key,value in recin:
  print key, value
recin.close()
```
The `RecordReader` class has the following methods:
* `close()`<br>
  Closes the record reader.
* `read()`<br>
  Reads next record and returns the key and value for the record.
* `tell()`<br>
  Returns the current file position in the record file.
* `seek(pos)`<br>
  Seek to new file position `pos` in record file.
* `rewind()`<br>
  Seeks back to beginning of record file.
* `done()`<br>
  Checks for end-of-file.

To write a record file, you can use a `RecordWriter`:
```
recout = sling.RecordWriter("/tmp/test.rec")
recout.write("key1", "value1")
recout.write("key2", "value2")
recout.close()
```

The `RecordWriter` class has the following methods:
* `close()`<br>
  Closes the record writer.
* `write(key, value)`<br>
  Writes record to record file.
* `tell()`<br>
  Returns the current file position in the record file.

## Documents

Example: read all document from a corpus:
```
import sling

commons = sling.Store()
docschema = sling.DocumentSchema(commons)
commons.freeze()

num_docs = 0
num_tokens = 0
corpus = sling.RecordReader("local/data/corpora/sempar/train.rec")
for _,rec in corpus:
  store = sling.Store(commons)
  doc = sling.Document(store.parse(rec), store, docschema)
  num_docs += 1
  num_tokens += len(doc.tokens)

print "docs:", num_docs, "tokens:", num_tokens
```

Example: read text from a file and create a corpus of tokenized documents:
```
import sling

# Create global store for common definitions.
commons = sling.Store()
docschema = sling.DocumentSchema(commons)
commons.freeze()

# Open input file.
fin = open("local/news.txt", "r")

# Create record output writer.
fout = sling.RecordWriter("/tmp/news.rec")

recno = 0
for text in fin.readlines():
  # Create local store.
  store = sling.Store(commons)

  # Read text from input file and tokenize.
  doc = sling.tokenize(text, store=store, schema=docschema)

  # add you frames and mentions here...

  # Update underlying frame for document.
  doc.update()

  # Write document to record file.
  fout.write(str(recno), doc.frame.data(binary=True))
  recno += 1

fin.close()
fout.close()
```

Example: write document with annotations for "John loves Mary":
```
import sling

# Create global store for common definitions.
commons = sling.Store()
docschema = sling.DocumentSchema(commons)

# Create global schemas.
isa = commons["isa"]
love01 = commons["/pb/love-01"]
arg0 = commons["/pb/arg0"]
arg1 = commons["/pb/arg1"]
person = commons["/saft/person"]

commons.freeze()

# Create record output writer.
fout = sling.RecordWriter("/tmp/john.rec")

# Add annotated "John loves Mary" example.
store = sling.Store(commons)
doc = sling.tokenize("John loves Mary", store=store, schema=docschema)
john = store.frame({isa: person})
mary = store.frame({isa: person})
loves = store.frame({isa: love01, arg0: john, arg1: mary})
doc.add_mention(0, 1).evoke(john)
doc.add_mention(1, 2).evoke(loves)
doc.add_mention(2, 3).evoke(mary)
doc.update()
fout.write("0001", doc.frame.data(binary=True))

fin.close()
fout.close()
```

The `Document` class has the following methods and properties:
* `__init__(frame=None, store=None, schema=None)`<br>
* `text` (r/w property)<br>
* `tokens` (r/o property)<br>
* `mentions` (r/o property)<br>
* `themes` (r/o property)<br>
* `add_token(text=None, start=None, length=None, brk=SPACE_BREAK)`<br>
* `add_mention(begin, end)`<br>
* `add_theme(theme)`<br>
* `update()`<br>
* `phrase(begin, end)`<br>
* `refresh_annotations()`<br>

The `Token` class has the following properties:
* `index` (r/w int property)<br>
* `text` (r/w string property)<br>
* `start` (r/w int property)<br>
* `length` (r/w int property)<br>
* `end` (r/o int property)<br>
* `brk` (r/w int property)<br>

The `Mention` class has the following methods and properties:
* `begin` (r/w int property)<br>
* `length` (r/w int property)<br>
* `end` (r/o int property)<br>
* `evokes()`<br>
* `evoke(frame)`<br>

## Parsing

## Miscellaneous

You can log messages to the SLING logging module:
```
import sling.log as log

log.info("Informational message")
log.warning("Warning message")
log.error("Error message")
log.fatal("Fatal error")
```

The SLING [command line flag module](../../sling/base/flags.h) is integrated
with the Python flags module, so the SLING flags can be set though a standard
Python [argparse.ArgumentParser](https://docs.python.org/2/library/argparse.html).
Flags are defined using the `flags.define()` method, e.g.:
```
import sling.flags as flags

flags.define("--verbose",
             help="output extra information",
             default=False,
             action='store_true')
```
The `flags.define()` function takes the same arguments as the standard Python
[add_argument()](https://docs.python.org/2/library/argparse.html#the-add-argument-method)
method. You can then access the flags as variables in the flags module, e.g.:
```
  if flags.verbose:
    print "verbose output..."
```

The flags parser must be initialized in the main method of your Python program:
```
if __name__ == '__main__':
  # Parse command-line arguments.
  flags.parse()
```
