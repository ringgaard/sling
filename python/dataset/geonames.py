import sling
import os

# Location of GeoNames dump.
gn_dump_date = "2018-10-21"
gn_base_path = "data/c/geonames"
gn_path = gn_base_path + "/" + gn_dump_date
output_dir = "data/e/dataset/geonames.sling"
output_file = output_dir + "/geonames.sling"
if not os.path.exists(output_dir): os.makedirs(output_dir)

# Read knowledge base.
print "Load KB"
commons = sling.Store()
commons.load("data/e/kb/kb.sling")
n_id = commons["id"]
n_isa = commons["isa"]
n_name = commons["name"]
n_alias = commons["alias"]
n_instance_of = commons["P31"]
n_geonames_id = commons["P1566"]
n_geonames_feature_code = commons["P2452"]
n_coordinate_location = commons["P625"]
n_country = commons["P17"]
n_located_in = commons["P131"]
n_iso_3166_code = commons["P297"]
n_geo = commons["/w/geo"]
n_lat = commons["/w/lat"]
n_lng = commons["/w/lng"]

# Build mapping from GeoNames IDs to QIDs.
print "Build GeoNames to Wikidata mapping"
gnmap = {}
ccmap = {}
for item in commons:
  gid = item[n_geonames_id]
  if gid != None:
    if type(gid) == sling.Frame: gid = gid.resolve()
    gnmap[int(gid)] = item
  cc = item[n_iso_3166_code]
  if cc != None:
    if type(cc) == sling.Frame: cc = cc.resolve()
    ccmap[cc] = item

print len(gnmap), "items with GeoNames IDs"
print len(ccmap), "items with country code"

# Read feature code mapping from file.
fcmap = {}
print "Read feature code mapping"
f = open("data/geonames/feature-code-map.txt")
for line in f.readlines():
  line = line.strip()
  if len(line) == 0: continue
  fields = line.split(" ")
  if len(fields) < 2: print "bad line:", fields
  feature_code = fields[0]
  item = commons[fields[1]]
  if item == None: print "Unknown feature item:", fields
  fcmap[feature_code] = item
f.close()

print len(fcmap), "GeoNames feature codes"

# Read codes for administrative division.
admin1 = {}
print "Read administrative division codes"
f = open(gn_path + "/admin1CodesASCII.txt")
for line in f.readlines():
  fields = line.rstrip("\n").split("\t")
  code = fields[0]
  gnid = int(fields[3])
  admin1[code] = gnid
f.close()

admin2 = {}
f = open(gn_path + "/admin2Codes.txt")
for line in f.readlines():
  fields = line.rstrip("\n").split("\t")
  code = fields[0]
  gnid = int(fields[3])
  admin2[code] = gnid
f.close()

print len(admin1), "Level 1 codes"
print len(admin2), "Level 2 codes"

commons.freeze()

# GeoNames locations:
#   0 geonameid         : integer id of record in geonames database
#   1 name              : name of geographical point (utf8)
#   2 asciiname         : name of geographical point in plain ascii characters
#   3 alternatenames    : alternatenames, comma separated, ascii names
#                         automatically transliterated, convenience attribute
#                         from alternatename table
#   4 latitude          : latitude in decimal degrees (wgs84)
#   5 longitude         : longitude in decimal degrees (wgs84)
#   6 feature class     : see http://www.geonames.org/export/codes.html
#   7 feature code      : see http://www.geonames.org/export/codes.html
#   8 country code      : ISO-3166 2-letter country code, 2 characters
#   9 cc2               : alternate country codes, comma separated, ISO-3166
#                         2-letter country code
#  10 admin1 code       : fipscode (subject to change to iso code)
#  11 admin2 code       : code for the second administrative division
#  12 admin3 code       : code for third level administrative division
#  13 admin4 code       : code for fourth level administrative division
#  14 population        : bigint
#  15 elevation         : in meters, integer
#  16 dem               : digital elevation model, srtm3 or gtopo30
#  17 timezone          : the iana timezone id
#  18 modification date : date of last modification in yyyy-MM-dd format

# Add slot if not defined by item.
def add_slot(slots, item, prop, value):
  if item == None or item[prop] == None and value != None and value != item:
    slots.append((prop, value))

# Convert GeoNames locations to frames.
print "Converting GeoNames locations"
output = sling.RecordWriter(output_file)
f = open(gn_path + "/allCountries.txt")
num_new_items = 0
num_updated_items = 0
for line in f.readlines():
  fields = line.rstrip("\n").split("\t")
  try:
    gid = int(fields[0])
    name = fields[1]
    asciiname = fields[2]
    alternatenames = fields[3].split(",")
    latitude = float(fields[4])
    longitude = float(fields[5])
    feature_class = fields[6]
    feature_code = fields[7]
    country_code = fields[8]
    admin_level1 = fields[10]
    admin_level2 = fields[11]
    fcc = feature_class + "." + feature_code
    l1 = country_code + "." +  admin_level1
    l2 = l1 + "." + admin_level2
  except:
    print fields
    raise

  # Lookup item in Wikidata.
  store = sling.Store(commons)
  item = gnmap.get(gid)
  slots = []

  if item == None:
    # Use /geo/ id for new items.
    key = "/geo/" + str(gid)
    slots.append((n_id, key))

    # Add name and aliases.
    slots.append((n_name, name))
    aliases = [name]
    if asciiname not in aliases: aliases.append(asciiname)
    for alternate in alternatenames:
      if len(alternate) == 0: continue
      if alternate not in aliases: aliases.append(alternate)
    for alias in aliases:
      slots.append((n_alias, store.frame({n_name: alias})))
  else:
    # Known item.
    key = item.id

  # Add location type.
  kind = fcmap.get(fcc)
  add_slot(slots, item, n_instance_of, kind)

  # Add country.
  if len(country_code) > 0:
    country = ccmap.get(country_code)
    add_slot(slots, item, n_country, country)

  # Add location in administrative division.
  location = admin2.get(l2)
  if location == None: location = admin1.get(l1)
  add_slot(slots, item, n_located_in, gnmap.get(location))

  # Add coordinate.
  coord = store.frame([(n_isa, n_geo), (n_lat, latitude), (n_lng, longitude)])
  add_slot(slots, item, n_coordinate_location, coord)

  # Add GeoNames id.
  add_slot(slots, item, n_geonames_id, fields[0])

  # Create item.
  if len(slots) > 0:
    frame = store.frame(slots)
    output.write(key, frame.data(binary=True))

    if item != None:
      num_updated_items += 1
    else:
      num_new_items += 1

f.close()
output.close()

print num_new_items, "new items"
print num_updated_items, "updated items"

