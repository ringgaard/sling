import sys
sys.path.append(".")

import python.pysling as sling

print sling.helloworld()

commons = sling.Store()

commons.load("data/nlp/schemas/meta-schema.sling")

fin = open("data/nlp/schemas/document-schema.sling", "r")
commons.load(fin)
fin.close()

commons.freeze()

store = sling.Store(commons)

print "commons", len(commons)
print "store", len(store)
