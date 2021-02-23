import sling

mbpath = "data/c/musicbrainz/mbdump"

def intval(s):
  return None if s == "\N" else int(s)

def getdate(fields, start):
  y = intval(fields[start])
  m = intval(fields[start + 1])
  d = intval(fields[start + 2])
  if y != None and m != None and d != None: return y * 10000 + m * 100 + d
  if y != None and m != None: return y * 100 + m
  return y

class Builder:
  def __init__(self, item):
    self.item = item
    self.slots = []

  def add(self, prop, value):
    if value == None: return
    if self.item != None:
      if value == self.item: return
      if self.item[prop] != None: return
    self.slots.append((prop, value))

  def empty(self):
    return len(self.slots) == 0

  def create(self, store):
    return store.frame(self.slots)


print "Read KB"
commons = sling.Store()
commons.load("data/e/kb/kb.sling")
n_id = commons["id"]
n_name = commons["name"]
n_artist_mbid = commons["P434"]
n_instance_of = commons["P31"]
n_human = commons["Q5"]
n_band = commons["Q215380"]
n_character = commons["Q95074"]
n_orchestra = commons["Q42998"]
n_choir = commons["Q131186"]
n_gender = commons["P21"]
n_male = commons["Q6581097"]
n_female = commons["Q6581072"]
n_birth = commons["P569"]
n_death = commons["P570"]
n_inception = commons["P571"]
n_disolved = commons["P576"]
n_birth_place = commons["P19"]
n_death_place = commons["P20"]
n_location_of_formation = commons["P740"]

artist_types = {
  "1": n_human,
  "2": n_band,
  "4": n_character,
  "5": n_orchestra,
  "6": n_choir,
}

print "Read Wikidata ids"
wikidata_url_prefix = "https://www.wikidata.org/wiki/"
wikidata_items = {}
f = open(mbpath + "/url")
for line in f.readlines():
  fields = line.rstrip('\n').split("\t")
  id = int(fields[0])
  url = fields[2]
  if url.startswith(wikidata_url_prefix):
    qid = url[len(wikidata_url_prefix):]
    wikidata_items[id] = commons[qid]
f.close()
print len(wikidata_items), "Wikidata items with MB ids"

def read_links(itemtype):
  items = {}
  f = open(mbpath + "/l_" + itemtype + "_url")
  for line in f.readlines():
    fields = line.rstrip('\n').split("\t")
    entity_id = int(fields[2])
    url_id = int(fields[3])
    if url_id in wikidata_items:
      items[entity_id] = wikidata_items[url_id]
  f.close()
  print len(items), "Wikidata", itemtype, "items"
  return items

def read__inverted_links(itemtype):
  items = {}
  f = open(mbpath + "/l_url_" + itemtype)
  for line in f.readlines():
    fields = line.rstrip('\n').split("\t")
    url_id = int(fields[2])
    entity_id = int(fields[3])
    if url_id in wikidata_items:
      items[entity_id] = wikidata_items[url_id]
  f.close()
  print len(items), "Wikidata", itemtype, "items"
  return items

print "Read entity QIDs"
wikidata_area_items = read_links("area")
wikidata_artist_items = read_links("artist")
wikidata_label_items = read_links("label")
wikidata_work_items = read__inverted_links("work")

commons.freeze()

output = sling.RecordWriter("data/e/dataset/musicbrainz.sling")

print "Read artists"
f = open(mbpath + "/artist")
num_new_artists = 0
num_updated_artists = 0
for line in f.readlines():
  fields = line.rstrip('\n').split("\t")
  artist_id = int(fields[0])
  mbid = fields[1]
  name = fields[2]
  begin = getdate(fields, 4)
  end = getdate(fields, 7)
  artist_type = fields[10]
  gender = fields[12]
  ended = fields[16]
  begin_area = intval(fields[17])
  end_area = intval(fields[18])

  #for i in range(len(fields)):
  #  print str(i) + ": " + fields[i],
  #print

  store = sling.Store(commons)
  item = wikidata_artist_items.get(artist_id)
  if item == None:
    key = "/mb/" + mbid
  else:
    key = item.id

  b = Builder(item)
  if item == None: b.add(n_id, key)
  b.add(n_name, name)
  b.add(n_artist_mbid, mbid)

  kind = artist_types.get(artist_type)
  if kind != None: b.add(n_instance_of, kind)

  begin_area_item = wikidata_area_items.get(begin_area)
  end_area_item = wikidata_area_items.get(end_area)

  if kind == n_human:
    if gender == "1": b.add(n_gender, n_male)
    if gender == "2": b.add(n_gender, n_female)

    if begin != None: b.add(n_birth, begin)
    if end != None: b.add(n_death, end)

    if begin_area_item != None: b.add(n_birth_place, begin_area_item)
    if end_area_item != None: b.add(n_death_place, end_area_item)
  else:
    if begin != None: b.add(n_inception, begin)
    if end != None: b.add(n_disolved, end)
    if begin_area_item != None: b.add(n_location_of_formation, begin_area_item)

  if not b.empty():
    frame = b.create(store)
    output.write(key, frame.data(binary=True))
    if item == None:
      num_new_artists += 1
    else:
      num_updated_artists += 1
    #print key, frame.data(utf8=True)

f.close()
print num_new_artists, "new artists"
print num_updated_artists, "updated artists"

print "Read artist credits"
artist_credits = {}
f = open(mbpath + "/artist_credit_name")
for line in f.readlines():
  fields = line.rstrip('\n').split("\t")
  credit_id = int(fields[0])
  artist_id = int(fields[2])
  if credit_id != artist_id:
    if credit_id not in artist_credits:
      artist_credits[credit_id] = []
    artist_credits[credit_id].append(artist_id)
f.close()
print len(artist_credits), "artist credits"

output.close()
