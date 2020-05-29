import sling
import sling.flags as flags

flags.define("--input", help="CoNLL input file")
flags.define("--output", help="SLING output file")
flags.parse()

fin = open(flags.arg.input, "r")
fout = sling.RecordWriter(flags.arg.output)

document = None
store = sling.Store()

brk = None
begin = None
kind = None

def end_span():
  global document, begin, kind
  if document is None: return
  if begin is None: return
  if kind is None: return
  end = len(document.tokens)
  document.evoke_type(begin, end, store[kind])
  begin = None
  kind = None

def end_document():
  global document, brk, begin
  if document is not None:
    end_span()
    document.update()
    fout.write(None, document.frame.data(binary=True))
  document = sling.Document(store=store)
  brk = sling.NO_BREAK
  begin = None
  kind = None

for line in fin:
  fields = line.strip().split(" ")
  if fields[0] == "-DOCSTART-":
    end_document()
  elif fields[0] == "":
    brk = sling.SENTENCE_BREAK
  else:
    word = fields[0]
    tag = fields[3]
    if tag[0] == 'O':
      end_span()
    elif tag[0] == 'I':
      if kind != tag[2:]: end_span()
      if begin is None: begin = len(document.tokens)
      kind = tag[2:]
    elif tag[0] == 'B':
      end_span()
      begin = len(document.tokens)
      kind = tag[2:]
    else:
      print("error:", fields)
    document.add_token(word=word, brk=brk)
    brk = None

end_document()

fin.close()
fout.close()

