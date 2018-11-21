# -*- coding: utf-8 -*-

import sling
import re
import string

MAXSPAN = 10

stop_words = set([
  'of', 'in', 'from', 'at', 'by', 'for', 'to', 'on', 'as',
  'is', 'a', 'an', 'the', 'and', 'or',
  'le', 'la', 'de', 'des', 'et',
  'der', 'die', 'das', 'und',
  "``", "''", "'s",
])

punctuation_pattern = re.compile("[" + re.escape(string.punctuation) + "]+$")

def ispunct(word):
  return punctuation_pattern.match(word)

def has_lower(matches):
  for m in matches:
    if m.form() == sling.api.CASE_LOWER: return True
  return False

class Span:
  def __init__(self, begin, end, matches = None):
    self.begin = begin
    self.end = end
    self.matches = matches
    self.cost = None if matches == None else 1
    self.left = None
    self.right = None

  def length(self):
    return self.end - self.begin

  def extract(self, spans):
    if self.matches != None:
      spans.append(self)
    else:
      if self.left != None: self.left.extract(spans)
      if self.right != None: self.right.extract(spans)


class Chart:
  def __init__(self, size):
    self.size = size
    self.elements = [None] * (size * size)

  def index(self,  i, j):
    return i * self.size + j - 1  # second index is 1-based

  def assign(self, i, j, span):
    self.elements[self.index(i, j)] = span

  def get(self, i, j):
    return self.elements[self.index(i, j)]

commons = sling.Store()
commons.lockgc()
commons.load("local/data/e/wiki/kb.sling")
phrasetab = sling.PhraseTable(commons, "local/data/e/wiki/en/phrase-table.repo")
docschema = sling.DocumentSchema(commons)
commons.freeze()

documentids = [
  'Q5945076', 'Q23883660', 'Q43287478', 'Q2147524',
  'Q57652',     # Helle Thorning-Schmidt
  'Q1636974',   # Danske Bank
  'Q186285',    # University of Copenhagen
  'Q1687170',   # Jens Christian Skou
]

articles = sling.RecordDatabase("local/data/e/wiki/en/documents@10.rec")
output = sling.RecordWriter("/tmp/chunked.rec")

for docid in documentids:
  # Read document from article database.
  store = sling.Store(commons)
  if docid.startswith("Q"):
    record = articles.lookup(docid)
    article = store.parse(record)
    document = sling.Document(article, schema=docschema)
    document.remove_annotations()
    document.update()
  else:
    document = sling.tokenize(docid, store=store, schema=docschema)

  print document.frame["title"]

  begin = 0
  while begin < len(document.tokens):
    # Find next sentence.
    end = begin + 1
    while end < len(document.tokens) and \
          document.tokens[end].brk < sling.SENTENCE_BREAK:
      end += 1
    print "s:", document.phrase(begin, end)
    length = end - begin

    # Find punctuations and lowercase tokens.
    is_punct = [False] * length
    is_lower = [False] * length
    for i in xrange(length):
      word = document.tokens[begin + i].word
      is_punct[i] = ispunct(word)
      is_lower[i] = word.islower();

    # Consider the first token in the sentence to be lower case if the
    # following token is lower case.
    if length > 1: is_lower[0] = is_lower[1]

    # Find all matching spans.
    chart = Chart(length)
    for b in xrange(begin, end):
      if is_punct[b - begin]: continue
      if document.tokens[b].word in stop_words: continue
      for e in xrange(b + 1, min(b + 1 + MAXSPAN, end + 1)):
        if is_punct[e - 1 - begin]: continue
        if document.tokens[e - 1].word in stop_words: continue
        phrase = document.phrase(b , e)
        matches = phrasetab.query(phrase)
        if len(matches) > 0:
          if e - b == 1 and is_lower[b - begin] and not has_lower(matches):
            print "Discard:", phrase
          else:
            print phrase, "(", matches[0].item().id, matches[0].item().name, ")"
            span = Span(b, e, matches)
            chart.assign(b - begin, e - b, span)

    # Find non-overlapping span covering with minimum cost.
    for l in xrange(2, length + 1):
      # Find best covering for all spans of length l.
      for s in xrange(0, length - l + 1):
        #print "s", s, "l", l
        span = chart.get(s, l)
        if span != None and span.matches != None:
          #print s, l, "has match"
          continue

        # Find best split of span [s;s+l).
        span = Span(s, l)
        chart.assign(s, l, span)
        for n in xrange(1, l):
          # Consider the split [s;s+n) and [s+n;s+l).
          #print "compute", s, n, l
          left = chart.get(s, n)
          right = chart.get(s + n, l - n)

          cost = 0
          if left != None and left.cost != None:
            cost += left.cost
          else:
            cost += n
          if right != None and right.cost != None:
            cost += right.cost
          else:
            cost += l - n

          if span.cost == None or cost <= span.cost:
            span.cost = cost
            span.left = left
            span.right = right

    # Extract best covering.
    solution = chart.get(0, length)
    if solution != None:
      spans = []
      solution.extract(spans)
      for s in spans:
        if s.matches != None:
          print "**", document.phrase(s.begin, s.end), "(", s.matches[0].item().id, s.matches[0].item().name, ")"

          mention = document.add_mention(s.begin, s.end)
          total = 0
          for m in s.matches: total += m.count()
          count = 0
          for m in s.matches:
            mention.evoke(m.item())
            count += m.count()
            if count * 2 >= total: break

    begin = end

  document.update()
  output.write(docid, document.frame.data(binary=True))

output.close()

