import sys
import struct

# Convert word embeddings in GloVe format to binary word2vec format.
glove_filename = sys.argv[1]
w2c_filename = sys.argv[2]

# Count number of words and dimensions.
print "Reading Glove embedding vectors from", glove_filename
fin = open(glove_filename, "r")
num_words = 0
num_dims = 0
for line in fin.readlines():
  num_words += 1
  if num_dims == 0:
    num_dims = len(line.split(" ")) - 1
fin.close()

print num_words, "words"
print num_dims, "dims"

# Write embeddings in binary word2vec format.
print "Writing word2vec embedding vectors to", w2c_filename
fout = open(w2c_filename, "w")
fout.write("%d %d\n" % (num_words, num_dims))
fin = open(glove_filename, "r")
for line in fin.readlines():
  f = line.rstrip().split(" ")
  fout.write(f[0])
  fout.write(" ")
  for i in range(1, num_dims + 1):
    fout.write(struct.pack("f", float(f[i])))
  fout.write("\n")
fin.close()
fout.close()

