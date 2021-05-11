// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/nlp/kb/knowledge-service.h"

#include <math.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/frame/object.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/net/http-server.h"
#include "sling/net/web-service.h"
#include "sling/nlp/kb/calendar.h"
#include "sling/string/text.h"
#include "sling/string/strcat.h"
#include "sling/util/md5.h"
#include "sling/util/sortmap.h"

namespace sling {
namespace nlp {

// Property order.
const char *property_order[] = {
  // Types.
  "P31",        // instance of
  "P279",       // subclass of
  "P642",       // of

  // Names.
  "P2561",      // name
  "P7407",      // name (image)
  "P4970",      // alternate names
  "P1705",      // native label
  "P97",        // noble title
  "P1813",      // short name
  "P511",       // honorific prefix
  "P735",       // given name
  "P6978",      // Scandinavian middle family name
  "P1950",      // second family name in Spanish name
  "P734",       // family name
  "P1035",      // honorific suffix
  "P1559",      // name in native language
  "P1477",      // birth name
  "P2562",      // married name
  "P742",       // pseudonym
  "P1786",      // posthumous name
  "P1449",      // nickname

  "P1476",      // title
  "P1680",      // subtitle

  "P138",       // named after
  "P3938",      // named by

  "P5278",      // gender inflection of surname
  "P1533",      // family name identical to this given name
  "P2976",      // patronym or matronym for this name

  "P1814",      // name in kana
  "P2838",      // professional name (Japan)
  "P1448",      // official name
  "P1549",      // demonym
  "P6271",      // demonym of

  // Organization.
  "P122",       // basic form of government
  "P1454",      // legal form
  "P452",       // industry
  "P92",        // main regulatory text
  "P740",       // location of formation
  "P571",       // inception
  "P576",       // dissolved, abolished or demolished
  "P1619",      // date of official opening
  "P36",        // capital
  "P159",       // headquarters location
  "P577",       // publication date
  "P291",       // place of publication
  "P115",       // home venue
  "P1001",      // applies to jurisdiction
  "P2541",      // operating area
  "P1906",      // office held by head of state
  "P1313",      // office held by head of government
  "P2388",      // office held by head of the organization
  "P2389",      // organization directed from the office or person
  "P748",       // appointed by

  "P797",       // authority
  "P208",       // executive body
  "P194",       // legislative body
  "P209",       // highest judicial authority
  "P1304",      // central bank
  "P749",       // parent organization
  "P355",       // subsidiary
  "P199",       // business division
  "P127",       // owned by
  "P1830",      // owner of
  "P361",       // part of
  "P527",       // has part
  "P155",       // follows
  "P156",       // followed by
  "P1365",      // replaces
  "P1366",      // replaced by
  "P7888",      // merged into
  "P272",       // production company
  "P449",       // original network
  "P123",       // publisher
  "P137",       // operator
  "P750",       // distributor
  "P2652",      // partnership with
  "P1327",      // partner in business or sport
  "P664",       // organizer
  "P710",       // participant

  "P1716",      // brand
  "P1056",      // product or material produced
  "P414",       // stock exchange
  "P946",       // ISIN

  "P112",       // founded by
  "P35",        // head of state
  "P6",         // head of government
  "P3975",      // secretary general
  "P1308",      // officeholder
  "P169",       // chief executive officer
  "P1789",      // chief operating officer
  "P1037",      // director/manager
  "P5126",      // assistant director
  "P7169",      // substitute director/manager
  "P2828",      // corporate officer
  "P1075",      // rector
  "P5769",      // editor-in-chief
  "P98",        // editor
  "P3460",      // colonel-in-chief
  "P286",       // head coach
  "P634",       // captain
  "P488",       // chairperson
  "P3320",      // board member
  "P5052",      // supervisory board member
  "P859",       // sponsor

  "P1342",      // number of seats
  "P2124",      // member count
  "P2196",      // students count
  "P5822",      // admission rate
  "P1410",      // number of representatives in an organization/legislature
  "P1128",      // employees
  "P3362",      // operating income
  "P2295",      // net profit
  "P2218",      // net worth
  "P2139",      // total revenue
  "P2403",      // total assets
  "P2137",      // total equity
  "P2133",      // total debt
  "P2138",      // total liabilities
  "P2136",      // total imports
  "P2226",      // market capitalization
  "P2663",      // common equity tier 1 capital ratio (CETI)
  "P2769",      // budget
  "P2130",      // cost
  "P2142",      // box office

  // Building.
  "P149",       // architectural style
  "P1101",      // floors above ground
  "P1139",      // floors below ground
  "P1301",      // number of elevators
  "P88",        // commissioned by
  "P466",       // occupant
  "P287",       // designed by
  "P84",        // architect
  "P170",       // creator
  "P631",       // structural engineer
  "P193",       // main building contractor
  "P126",       // maintained by
  "P1398",      // structure replaces
  "P167",       // structure replaced by

  // Birth and death.
  "P3150",      // birthday
  "P569",       // date of birth
  "P19",        // place of birth
  "P1636",      // date of baptism in early childhood
  "P570",       // date of death
  "P20",        // place of death
  "P4602",      // date of burial or cremation
  "P119",       // place of burial
  "P1196",      // manner of death
  "P509",       // cause of death
  "P157",       // killed by

  // Person.
  "P21",        // sex or gender
  "P91",        // sexual orientation
  "P172",       // ethnic group
  "P27",        // country of citizenship
  "P551",       // residence
  "P937",       // work location

  "P1884",      // hair color
  "P1340",      // eye color
  "P552",       // handedness
  "P423",       // shooting handedness
  "P741",       // playing hand
  "P1853",      // blood type
  "P1050",      // medical condition

  "P3828",      // wears
  "P1576",      // lifestyle

  // Proportions.
  "P2043",      // length
  "P2049",      // width
  "P2048",      // height
  "P2386",      // diameter
  "P2046",      // area
  "P2067",      // mass

  // Family.
  "P53",        // family
  "P22",        // father
  "P25",        // mother
  "P3373",      // sibling
  "P3448",      // stepparent
  "P26",        // spouse
  "P2842",      // place of marriage
  "P451",       // partner
  "P1971",      // number of children
  "P40",        // child
  "P1038",      // relative
  "P1290",      // godparent

  // Career.
  "P106",       // occupation
  "P101",       // field of work
  "P5021",      // test taken
  "P69",        // educated at
  "P803",       // professorship
  "P512",       // academic degree
  "P812",       // academic major
  "P811",       // academic minor
  "P1026",      // doctoral thesis
  "P184",       // doctoral advisor
  "P1066",      // student of
  "P802",       // student
  "P185",       // doctoral student
  "P108",       // employer
  "P1416",      // affiliation
  "P6424",      // affiliation string
  "P39",        // position held
  "P3602",      // candidacy in election
  "P463",       // member of
  "P1344",      // participant of
  "P2868",      // subject has role

  "Q65994327",  // producer of
  "Q65971570",  // director of
  "Q65971578",  // wrote script of
  "Q66318312",  // cast member of
  "Q78522641",  // creator of

  "P1595",      // charge
  "P1399",      // convicted of
  "P2632",      // place of detention

  "P241",       // military branch
  "P410",       // military rank
  "P598",       // commander of
  "P607",       // conflict

  "P2031",      // work period (start)
  "P2032",      // work period (end)
  "P1317",      // floruit

  "P800",       // notable work
  "P1411",      // nominated for
  "P2650",      // interested in

  // Ideology
  "P102",       // member of political party
  "P1387",      // political alignment
  "P1142",      // political ideology
  "P737",       // influenced by
  "P140",       // religion

  // Sports.
  "P641",       // sport
  "P2094",      // competition class
  "P1532",      // country for sport
  "P118",       // league
  "P413",       // position played on team / speciality
  "P1618",      // sport number
  "P647",       // drafted by
  "P505",       // general manager
  "P6087",      // coach of sports team
  "P54",        // member of sports team

  "P1346",      // winner
  "P1350",      // number of matches played/races/starts
  "P6509",      // total goals in career
  "P6543",      // total shots in career
  "P6545",      // total assists in career
  "P6544",      // total points in career
  "P6546",      // penalty minutes in career
  "P6547",      // career plus-minus rating
  "P564",       // singles record
  "P555",       // doubles record
  "P1356",      // number of losses
  "P1363",      // points/goal scored by
  "P1352",      // ranking

  // Art.
  "P136",       // genre
  "P921",       // main subject
  "P1303",      // instrument
  "P412",       // voice type
  "P264",       // record label
  "P358",       // discography
  "P1283",      // filmography
  "P135",       // movement
  "P180",       // depicts

  "P50",        // author
  "P2679",      // author of foreword
  "P2680",      // author of afterword
  "P2093",      // author name string
  "P170",       // creator
  "P58",        // screenwriter
  "P1040",      // film editor
  "P2554",      // production designer
  "P1431",      // executive producer
  "P162",       // producer
  "P57",        // director
  "P5126",      // assistant director
  "P344",       // director of photography
  "P4608",      // scenographer
  "P86",        // composer
  "P676",       // lyrics by
  "P87",        // librettist
  "P3174",      // art director
  "P2515",      // costume designer
  "P4805",      // make-up artist
  "P175",       // performer
  "P161",       // cast member
  "P725",       // voice actor
  "P3092",      // film crew member
  "P674",       // characters
  "P658",       // tracklist
  "P1113",      // number of episodes
  "P2437",      // number of seasons

  "P840",       // narrative location
  "P915",       // filming location

  "P1877",      // after a work by
  "P144",       // based on
  "P4969",      // derivative work
  "P179",       // part of the series
  "P747",       // has edition
  "P186",       // material used

  // Event.
  "P585",       // point in time
  "P580",       // start time
  "P582",       // end time
  "P1619",      // date of official opening
  "P729",       // service entry
  "P2047",      // duration

  // Language.
  "P103",       // native language
  "P1412",      // languages spoken, written or signed
  "P6886",      // writing language
  "P37",        // official language
  "P2936",      // language used

  // Location.
  "P1376",      // capital of
  "P150",       // contains administrative territorial entity
  "P47",        // shares border with
  "P2927",      // water as percent of area

  "P1082",      // population
  "P1540",      // male population
  "P1539",      // female population
  "P2573",      // number of out-of-school children
  "P4841",      // total fertility rate
  "P3270",      // compulsory education (minimum age)
  "P3271",      // compulsory education (maximum age)
  "P2997",      // age of majority
  "P3000",      // marriageable age
  "P3001",      // retirement age
  "P2250",      // life expectancy
  "P3864",      // suicide rate
  "P6897",      // literacy rate

  "P3529",      // median income
  "P2220",      // household wealth
  "P2131",      // nominal GDP
  "P2132",      // nominal GDP per capita
  "P4010",      // GDP (PPP)
  "P2299",      // PPP GDP per capita
  "P2219",      // real gross domestic product growth rate
  "P1279",      // inflation rate
  "P2855",      // VAT-rate
  "P1198",      // unemployment rate
  "P38",        // currency
  "P2134",      // total reserves
  "P1081",      // Human Development Index
  "P1125",      // Gini coefficient
  "P5167",      // vehicles per thousand people

  "P1622",      // driving side
  "P5658",      // railway traffic side
  "P395",       // licence plate code
  "P2884",      // mains voltage
  "P2853",      // electrical plug type
  "P474",       // country calling code
  "P2258",      // mobile country code
  "P473",       // local dialing code
  "P2852",      // emergency phone number

  "P610",       // highest point
  "P1589",      // lowest point
  "P2044",      // elevation above sea level

  // Place.
  "P263",       // official residence
  "P669",       // located on street
  "P6375",      // located at street address
  "P670",       // street number
  "P281",       // postal code
  "P276",       // location
  "P17",        // country
  "P495",       // country of origin
  "P30",        // continent
  "P625",       // coordinate location
  "P5140",      // coordinates of geographic center
  "P1332",      // coordinates of northernmost point
  "P1334",      // coordinates of easternmost point
  "P1333",      // coordinates of southernmost point
  "P1335",      // coordinates of westernmost point
  "P131",       // located in the administrative territorial entity
  "P706",       // located on terrain feature
  "P206",       // located in or next to body of water
  "P421",       // located in time zone

  // Taxon.
  "P225",       // taxon name
  "P1843",      // taxon common name
  "P1420",      // taxon synonym
  "P694",       // replaced synonym
  "P105",       // taxon rank
  "P171",       // parent taxon
  "P427",       // taxonomic type
  "P574",       // year of taxon publication
  "P405",       // taxon author
  "P697",       // ex taxon author
  "P6507",      // taxon author citation

  // Low priority properties.
  "P166",       // award received

  // Media.
  "P18",        // image
  "P154",       // logo image
  "P41",        // flag image
  "P3383",      // film poster
  "P8972",      // small logo or icon
  "P1442",      // image of grave
  "P1801",      // commemorative plaque image
  "P6802",      // related image
  "P2716",      // collage image
  "P3451",      // nighttime view
  "P5252",      // winter view
  "P4291",      // panoramic view
  "P8592",      // aerial view
  "P3311",      // plan view image
  "P2713",      // sectional view
  "P8517",      // view
  "P4640",      // photosphere image
  "P5775",      // image of interior
  "P2910",      // icon
  "P109",       // signature
  "P1543",      // monogram
  "P94",        // coat of arms image
  "P158",       // seal image
  "P4004",      // escutcheon image
  "P5962",      // sail emblem
  "P2425",      // service ribbon image
  "P117",       // chemical structure
  "P8224",      // molecular model or crystal lattice model
  "P692",       // Gene Atlas Image
  "P181",       // taxon range map image
  "P367",       // astronomic symbol image
  "P1766",      // place name sign
  "P8667",      // twin town sign
  "P14",        // traffic sign
  "P8505",      // traffic sign template image
  "P1846",      // distribution map
  "P1943",      // location map
  "P242",       // locator map image
  "P1944",      // relief location map
  "P1621",      // detail map
  "P15",        // route map
  "P491",       // orbit diagram
  "P207",       // bathymetry image

  // Categories.
  "P910",       // topic's main category
  "P7084",      // related category
  "P6365",      // member category
  "P1792",      // category of associated people
  "P4195",      // category for employees of the organization
  "P3876",      // category for alumni of educational institution
  "P1464",      // category for people born here
  "P1465",      // category for people who died here
  "P1791",      // category of people buried here
  "P1740",      // category for films shot at this location
  "P5996",      // category for films in this language
  "P2033",      // category for pictures taken with camera
  "P2517",      // category for recipients of this award
  "P6112",      // category for members of a team
  "P1754",      // category related to list

  nullptr,
};

// Convert geo coordinate from decimal to minutes and seconds.
static string ConvertGeoCoord(double coord, bool latitude) {
  // Compute direction.
  const char *sign;
  if (coord < 0) {
    coord = -coord;
    sign = latitude ? "S" : "W";
  } else {
    sign = latitude ? "N" : "E";
  }

  // Compute degrees.
  double integer;
  double remainder = modf(coord, &integer);
  int degrees = static_cast<int>(integer);

  // Compute minutes.
  remainder = modf(remainder * 60, &integer);
  int minutes = static_cast<int>(integer);

  // Compute seconds.
  remainder = modf(remainder * 60, &integer);
  int seconds = static_cast<int>(integer + 0.5);

  // Build coordinate string.
  return StrCat(degrees, "°", minutes, "′", seconds, "″", sign);
}

// Make Wikimedia Commons url for file.
static string CommonsUrl(Text filename) {
  // Replace spaces with underscores.
  string fn = filename.str();
  for (char &c : fn) {
    if (c == ' ') c = '_';
  }

  // Compute MD5 digest for filename.
  unsigned char digest[16];
  MD5Digest(digest, fn.data(), fn.size());
  char d1 = "0123456789abcdef"[digest[0] >> 4];
  char d2 = "0123456789abcdef"[digest[0] & 0x0f];

  // Commons files are stored in subdirectories based on the MD5 digest of the
  // filename.
  string url = "https://upload.wikimedia.org/wikipedia/commons/";
  url.push_back(d1);
  url.push_back('/');
  url.push_back(d1);
  url.push_back(d2);
  url.push_back('/');
  for (char &c : fn) {
    switch (c) {
      case '?': url.append("%3F"); break;
      case '+': url.append("%2B"); break;
      case '&': url.append("%26"); break;
      default: url.push_back(c);
    }
  }

  return url;
}

void KnowledgeService::Load(Store *kb, const string &name_table) {
  // Bind names and freeze store.
  kb_ = kb;
  CHECK(names_.Bind(kb_));
  docnames_ = new DocumentNames(kb);

  // Get meta data for properties.
  std::vector<PropName> xref_properties;
  for (const Slot &s : Frame(kb, kb->Lookup("/w/entity"))) {
    if (s.name != n_role_) continue;
    Frame property(kb, s.value);
    Property p;

    // Get property id and name.
    p.id = s.value;
    p.name = property.GetHandle(n_name_);

    // Property data type.
    p.datatype = property.GetHandle(n_target_);

    // Collect xref properties.
    if (p.datatype == n_xref_type_) {
      Text name = kb->GetString(p.name)->str();
      xref_properties.emplace_back(name, p.id);
    }

    p.image = false;
    for (const Slot &ps : property) {
      // Get URL formatter for property.
      if (ps.name == n_formatter_url_ && p.url.empty()) {
        Handle formatter = ps.value;
        bool ignore = false;
        if (kb->IsFrame(formatter)) {
          // Resolve qualified formatter url.
          Frame fq(kb, formatter);
          formatter = fq.GetHandle(Handle::is());

          // Skip deprecated and special services.
          if (fq.Has(n_reason_for_deprecation_)) ignore = true;
          if (fq.Has(n_applies_if_regex_matches_)) ignore = true;
        }
        if (!ignore && kb->IsString(formatter)) {
          p.url = String(kb, formatter).value();
        }
      }

      // Check if property is a representative image for the item.
      if (ps.name == n_instance_of_ && ps.value == n_representative_image_) {
        p.image = true;
      }
    }

    // Add property.
    properties_[p.id] = p;

    // Add inverse property item.
    Handle inverse = property.GetHandle(n_inverse_label_item_);
    if (!inverse.IsNil()) {
      Frame inverse_property(kb, inverse);
      Property ip;
      ip.id = inverse;
      ip.name = inverse_property.GetHandle(n_name_);
      ip.datatype = n_item_type_.handle();
      ip.image = false;
      properties_[ip.id] = ip;
    }
  }

  // Order xref properties in alphabetical order.
  std::sort(xref_properties.begin(), xref_properties.end());

  // Set up property order.
  int order = 0;
  for (const char **p = property_order; *p != nullptr; ++p) {
    auto f = properties_.find(kb->Lookup(*p));
    if (f != properties_.end()) {
      f->second.order = order++;
    } else {
      VLOG(1) << "Property not know: " << *p;
    }
  }
  for (auto &pn : xref_properties) {
    auto f = properties_.find(pn.id);
    CHECK(f != properties_.end());
    if (f->second.order == kint32max) {
      f->second.order = order++;
    }
  }

  // Initialize calendar.
  calendar_.Init(kb);

  // Load name table.
  if (!name_table.empty()) {
    LOG(INFO) << "Loading name table from " << name_table;
    aliases_.Load(name_table);
  }
}

void KnowledgeService::LoadXref(const string &filename) {
  xref_.Load(filename);
}

void KnowledgeService::Register(HTTPServer *http) {
  http->Register("/kb/query", this, &KnowledgeService::HandleQuery);
  http->Register("/kb/item", this, &KnowledgeService::HandleGetItem);
  http->Register("/kb/frame", this, &KnowledgeService::HandleGetFrame);
  common_.Register(http);
  app_.Register(http);
  app_.set_index_fallback(true);
}

void KnowledgeService::HandleQuery(HTTPRequest *request,
                                   HTTPResponse *response) {
  WebService ws(kb_, request, response);

  // Get query
  Text query = ws.Get("q");
  bool fullmatch = ws.Get("fullmatch", false);
  int window = ws.Get("window", 5000);
  int limit = ws.Get("limit", 50);
  int boost = ws.Get("boost", 1000);
  VLOG(1) << "Name query: " << query;

  // Lookup name in name table.
  std::vector<Text> matches;
  if (!query.empty()) {
    aliases_.Lookup(query, !fullmatch, window, boost, &matches);
  }

  // Check for exact match with id.
  Handles results(ws.store());
  Handle idmatch = kb_->Lookup(query);
  if (idmatch.IsNil() and xref_.loaded()) {
    // Try looking up in cross-reference.
    Text id = xref_.Map(query);
    if (!id.empty()) idmatch = kb_->Lookup(id);
  }
  if (!idmatch.IsNil()) {
    Frame item(kb_, idmatch);
    if (item.valid()) {
      Builder match(ws.store());
      GetStandardProperties(item, &match);
      results.push_back(match.Create().handle());
    }
  }

  // Generate response.
  Builder b(ws.store());
  for (Text id : matches) {
    if (results.size() >= limit) break;
    Frame item(kb_, kb_->Lookup(id));
    if (item.invalid()) continue;
    Builder match(ws.store());
    GetStandardProperties(item, &match);
    results.push_back(match.Create().handle());
  }
  b.Add(n_matches_,  Array(ws.store(), results));

  // Return response.
  ws.set_output(b.Create());
}

void KnowledgeService::HandleGetItem(HTTPRequest *request,
                                     HTTPResponse *response) {
  WebService ws(kb_, request, response);

  // Look up item in knowledge base.
  Text itemid = ws.Get("id");
  VLOG(1) << "Look up item '" << itemid << "'";
  Handle handle = kb_->LookupExisting(itemid);
  if (handle.IsNil() and xref_.loaded()) {
    // Try looking up in cross-reference.
    itemid = xref_.Map(itemid);
    if (!itemid.empty()) handle = kb_->LookupExisting(itemid);
  }
  if (handle.IsNil()) {
    response->SendError(404, nullptr, "Item not found");
    return;
  }

  // Generate response.
  Frame item(ws.store(), handle);
  if (!item.valid()) {
    response->SendError(404, nullptr, "Invalid item");
    return;
  }
  Builder b(ws.store());
  GetStandardProperties(item, &b);
  Handle datatype = item.GetHandle(n_target_);
  if (!datatype.IsNil()) {
    Frame dt(kb_, datatype);
    if (dt.valid()) {
      b.Add(n_type_, dt.GetHandle(n_name_));
    }
  }

  // Fetch properties.
  Item info(ws.store());
  FetchProperties(item, &info);
  b.Add(n_properties_, Array(ws.store(), info.properties));
  b.Add(n_xrefs_, Array(ws.store(), info.xrefs));
  b.Add(n_categories_, Array(ws.store(), info.categories));
  b.Add(n_gallery_, Array(ws.store(), info.gallery));

  // Add summary.
  if (item.Has(n_lex_)) {
    // Add document URL.
    Text url = item.GetText(n_url_);
    if (!url.empty()) b.Add(n_url_, url);

    // Add document text.
    Document document(ws.store(), docnames_);
    if (lexer_.Lex(&document, item.GetText(n_lex_))) {
      b.Add(n_document_, ToHTML(document));
    }
  }

  // Return response.
  ws.set_output(b.Create());
}

void KnowledgeService::FetchProperties(const Frame &item, Item *info) {
  // Collect properties and values.
  typedef SortableMap<Property *, Handles *> GroupMap;
  GroupMap property_groups;
  std::vector<Handle> external_media;
  std::unordered_set<string> media_urls;
  for (const Slot &s : item) {
    // Collect categories.
    if (s.name == n_category_) {
      Builder b(item.store());
      Frame cat(item.store(), s.value);
      GetStandardProperties(cat, &b);
      info->categories.push_back(b.Create().handle());
      continue;
    }

    // Collect media files.
    if (s.name == n_media_) {
      external_media.push_back(s.value);
    }

    // Look up property. Skip non-property slots.
    auto f = properties_.find(s.name);
    if (f == properties_.end()) continue;
    Property *property = &f->second;

    // Get property list for property.
    Handles *&property_list = property_groups[property];
    if (property_list == nullptr) {
      property_list = new Handles(item.store());
    }

    // Add property value.
    property_list->push_back(s.value);
  }

  // Sort properties in display order.
  property_groups.sort([](const GroupMap::Node *n1, const GroupMap::Node *n2) {
    return n1->first->order < n2->first->order;
  });

  // Build property lists.
  for (auto &group : property_groups.array) {
    const Property *property = group->first;

    // Add property information.
    Builder p(item.store());
    p.Add(n_property_, property->name);
    p.Add(n_ref_, property->id);
    p.Add(n_type_, property->datatype);

    // Add property values.
    if (!property->image) {
      SortChronologically(item.store(), group->second);
    }
    Handles values(item.store());
    for (Handle h : *group->second) {
      // Resolve value.
      Handle value = item.store()->Resolve(h);
      bool qualified = value != h;

      // Add property value based on property type.
      Builder v(item.store());
      if (property->datatype == n_item_type_) {
        if (kb_->IsFrame(value)) {
          // Add reference to other item.
          Frame ref(kb_, value);
          GetStandardProperties(ref, &v);
        } else {
          v.Add(n_text_, value);
        }
      } else if (property->datatype == n_xref_type_) {
        // Add external reference.
        String identifier(kb_, value);
        v.Add(n_text_, identifier);
      } else if (property->datatype == n_property_type_) {
        // Add reference to property.
        Frame ref(kb_, value);
        if (ref.valid()) {
          GetStandardProperties(ref, &v);
        }
      } else if (property->datatype == n_string_type_) {
        // Add string value.
        v.Add(n_text_, value);
      } else if (property->datatype == n_text_type_) {
        // Add text value with language.
        if (kb_->IsString(value)) {
          String monotext(kb_, value);
          Handle qual = monotext.qualifier();
          if (qual.IsNil()) {
            v.Add(n_text_, value);
          } else {
            v.Add(n_text_, monotext.text());
            Frame lang(kb_, qual);
            if (lang.valid()) {
              v.Add(n_lang_, lang.GetHandle(n_name_));
            }
          }
        } else if (kb_->IsFrame(value)) {
          Frame monotext(kb_, value);
          v.Add(n_text_, monotext.GetHandle(Handle::is()));
          Frame lang = monotext.GetFrame(n_lang_);
          if (lang.valid()) {
            v.Add(n_lang_, lang.GetHandle(n_name_));
          }
        } else {
          v.Add(n_text_, value);
        }
      } else if (property->datatype == n_url_type_) {
        // Add URL value.
        v.Add(n_text_, value);
        v.Add(n_url_, value);
      } else if (property->datatype == n_media_type_) {
        // Add image.
        v.Add(n_text_, value);
      } else if (property->datatype == n_geo_type_) {
        // Add coordinate value.
        Frame coord(kb_, value);
        double lat = coord.GetFloat(n_lat_);
        double lng = coord.GetFloat(n_lng_);
        v.Add(n_text_, StrCat(ConvertGeoCoord(lat, true), ", ",
                              ConvertGeoCoord(lng, false)));
        v.Add(n_url_, StrCat("http://maps.google.com/maps?q=",
                              lat, ",", lng));
      } else if (property->datatype == n_quantity_type_) {
        // Add quantity value.
        string text;
        if (kb_->IsFrame(value)) {
          Frame quantity(kb_, value);
          text = AsText(quantity.GetHandle(n_amount_));

          // Get unit symbol, preferably in latin script.
          Frame unit = quantity.GetFrame(n_unit_);
          text.append(" ");
          text.append(UnitName(unit));
        } else {
          text = AsText(value);
        }
        v.Add(n_text_, text);
      } else if (property->datatype == n_time_type_) {
        // Add time value.
        Object time(kb_, value);
        v.Add(n_text_, calendar_.DateAsString(time));
      }

      // Add URL if property has URL formatter.
      if (!property->url.empty() && kb_->IsString(value)) {
        String identifier(kb_, value);
        string url = property->url;
        int pos = url.find("$1");
        if (pos != -1) {
          Text replacement = identifier.text();
          url.replace(pos, 2, replacement.data(), replacement.size());
        }
        if (!url.empty()) v.Add(n_url_, url);
      }

      // Get qualifiers.
      if (qualified) {
        Item qualifiers(item.store());
        FetchProperties(Frame(item.store(), h), &qualifiers);
        if (!qualifiers.properties.empty()) {
          v.Add(n_qualifiers_, Array(item.store(), qualifiers.properties));
        }
      }

      values.push_back(v.Create().handle());

      // Collect media files for gallery.
      if (property->image) {
        Text filename = String(item.store(), value).text();
        Builder m(item.store());
        string url = CommonsUrl(filename);
        media_urls.insert(url);
        m.Add(n_url_, url);
        if (qualified) {
          Frame image(kb_, h);
          Handle legend = image.GetHandle(n_media_legend_);
          if (!legend.IsNil()) m.Add(n_text_, legend);
        }
        info->gallery.push_back(m.Create().handle());
      }
    }
    p.Add(n_values_, Array(item.store(), values));

    // Add property to property list.
    if (property->datatype == n_xref_type_) {
      info->xrefs.push_back(p.Create().handle());
    } else {
      info->properties.push_back(p.Create().handle());
    }

    delete group->second;
  }

  // Add media to gallery.
  for (Handle media : external_media) {
    string url = kb_->GetString(kb_->Resolve(media))->str().str();
    if (media_urls.count(url) > 0) continue;

    Builder m(item.store());
    m.Add(n_url_, url);
    if (kb_->IsFrame(media)) {
      Frame image(kb_, media);
      Handle legend = image.GetHandle(n_media_legend_);
      if (!legend.IsNil()) m.Add(n_text_, legend);

      Handle quality = image.GetHandle(n_has_quality_);
      if (quality.IsNil()) quality = image.GetHandle(n_statement_subject_of_);
      if (quality == n_not_safe_for_work_) m.Add(n_nsfw_, true);
    }
    info->gallery.push_back(m.Create().handle());
    media_urls.insert(url);
  }
}

void KnowledgeService::GetStandardProperties(const Frame &item,
                                             Builder *builder) const {
  builder->Add(n_ref_, item.Id());
  Handle name = item.GetHandle(n_name_);
  if (!name.IsNil()) {
    builder->Add(n_text_, name);
  } else {
    builder->Add(n_text_, item.Id());
  }
  Handle description = item.GetHandle(n_description_);
  if (!description.IsNil()) builder->Add(n_description_, description);
}

void KnowledgeService::SortChronologically(Store *store,
                                           Handles *values) const {
  if (values->size() < 2) return;
  std::stable_sort(values->begin(), values->end(), [&](Handle a, Handle b) {
    if (!store->IsFrame(b)) return true;
    if (!store->IsFrame(a)) return false;

    Frame a_frame(store, a);
    int a_order = GetCanonicalOrder(a_frame);
    bool a_ordered = a_order != kint32max;
    Date a_date = GetCanonicalDate(a_frame);
    bool a_dated = a_date.precision != Date::NONE;

    Frame b_frame(store, b);
    int b_order = GetCanonicalOrder(b_frame);
    bool b_ordered = b_order != kint32max;
    Date b_date = GetCanonicalDate(b_frame);
    bool b_dated = b_date.precision != Date::NONE;

    if (a_ordered && b_ordered) {
      // Compare by series ordinal.
      return a_order < b_order;
    } else if (a_dated || b_dated) {
      // Compare by date.
      if (!b_dated) return true;
      if (!a_dated) return false;
      return a_date < b_date;
    } else {
      return false;
    }
  });
}

Date KnowledgeService::GetCanonicalDate(const Frame &frame) const {
  Object start = frame.Get(n_start_time_);
  if (start.valid()) return Date(start);

  Object end = frame.Get(n_end_time_);
  if (end.valid()) {
    // Subtract one from end dates to make them sort before start dates.
    Date end_date(end);
    end_date.day--;
    return end_date;
  }

  Object time = frame.Get(n_point_in_time_);
  if (time.valid()) return Date(time);

  return Date();
}

int64 KnowledgeService::GetCanonicalOrder(const Frame &frame) const {
  Text ordinal = frame.GetText(n_series_ordinal_);
  if (ordinal.empty()) return kint32max;
  int64 number = 0;
  for (char c : ordinal) {
    if (c >= '0' && c <= '9') {
      number = number * 10 + (c - '0');
    } else {
      number = number * 128 + c;
    }
  }
  return number;
}

string KnowledgeService::AsText(Handle value) {
  value = kb_->Resolve(value);
  if (value.IsInt()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value.AsInt());
    return buf;
  } else if (value.IsFloat()) {
    float number = value.AsFloat();
    char buf[32];
    if (floorf(number) == number) {
      snprintf(buf, sizeof(buf), "%.f", number);
    } else if (number > 0.001) {
      snprintf(buf, sizeof(buf), "%.3f", number);
    } else {
      snprintf(buf, sizeof(buf), "%g", number);
    }
    return buf;
  } else {
    return ToText(kb_, value);
  }
}

string KnowledgeService::UnitName(const Frame &unit) {
  // Check for valid unit.
  if (!unit.valid()) return "";

  // Find best unit symbol, preferably in latin script.
  Handle best = Handle::nil();
  Handle fallback = Handle::nil();
  for (const Slot &s : unit) {
    if (s.name != n_unit_symbol_) continue;
    Frame symbol(kb_, s.value);
    if (!symbol.valid()) {
      if (fallback.IsNil()) fallback = s.value;
      continue;
    }

    // Prefer latin script.
    Handle script = symbol.GetHandle(n_writing_system_);
    if (script == n_latin_script_ && best.IsNil()) {
      best = s.value;
    } else {
      // Skip language specific names.
      if (symbol.Has(n_language_) || symbol.Has(n_name_language_)) continue;

      // Fall back to symbols with no script.
      if (script == Handle::nil() && fallback.IsNil()) {
        fallback = s.value;
      }
    }
  }
  if (best.IsNil()) best = fallback;

  // Try to get name of best unit symbol.
  if (!best.IsNil()) {
    Handle unit_name = kb_->Resolve(best);
    if (kb_->IsString(unit_name)) {
      return String(kb_, unit_name).value();
    }
  }

  // Fall back to item name of unit.
  return unit.GetString(n_name_);
}

void KnowledgeService::HandleGetFrame(HTTPRequest *request,
                                      HTTPResponse *response) {
  WebService ws(kb_, request, response);

  // Look up frame in knowledge base.
  Text id = ws.Get("id");
  Handle handle = kb_->LookupExisting(id);
  if (handle.IsNil()) {
    response->SendError(404, nullptr, "Frame not found");
    return;
  }

  // Return frame as response.
  ws.set_output(Object(kb_, handle));
}

}  // namespace nlp
}  // namespace sling

