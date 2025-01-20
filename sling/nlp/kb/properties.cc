// Copyright 2021 Ringgaard Research ApS
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

#include "sling/nlp/kb/properties.h"

namespace sling {
namespace nlp {

// Property display order.
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

  // Person.
  "P21",        // sex or gender
  "P91",        // sexual orientation
  "P172",       // ethnic group

  // Birth and death.
  "P3150",      // birthday
  "P569",       // date of birth
  "P19",        // place of birth
  "P1636",      // date of baptism in early childhood
  "P551",       // residence
  "P937",       // work location
  "P570",       // date of death
  "P20",        // place of death
  "P4602",      // date of burial or cremation
  "P119",       // place of burial
  "P1196",      // manner of death
  "P509",       // cause of death
  "P157",       // killed by
  "P27",        // country of citizenship

  // Family.
  "P53",        // family
  "P22",        // father
  "P25",        // mother
  "P3448",      // stepparent
  "P3373",      // sibling
  "P451",       // unmarried partner
  "P26",        // spouse
  "P1971",      // number of children
  "P40",        // child
  "P1038",      // relative
  "P1290",      // godparent

  "PLOV",       // lover
  "PFRND",      // friend
  "PACQ",       // acquaintance
  "P3342",      // significant person

  // Career.
  "P69",        // educated at
  "P101",       // field of work
  "P106",       // occupation
  "P5021",      // test taken
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
  "Q66796038",  // award received by
  "Q101072499", // award conferred

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

  // Publicity.
  "P166",       // award received
  "P2522",      // victory
  "P1441",      // present in work
  "PFEIN",      // featured in

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
  "Q67205181",  // business division of
  "P127",       // owned by
  "P1830",      // owner of
  "P361",       // part of
  "P527",       // has part
  "P155",       // follows
  "P156",       // followed by
  "P1365",      // replaces
  "P1366",      // replaced by
  "P807",       // separated from
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
  "Q65972149",  // founder of
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
  "P8571",      // external auditor
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

  // Proportions.
  "P2043",      // length
  "P2049",      // width
  "P2048",      // height
  "P2386",      // diameter
  "P2046",      // area
  "P2067",      // mass

  // Physical attributes.
  "P1884",      // hair color
  "P1340",      // eye color
  "P552",       // handedness
  "P423",       // shooting handedness
  "P741",       // playing hand
  "P1853",      // blood type
  "P1050",      // medical condition
  "P3828",      // wears
  "P1576",      // lifestyle

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
  "P1319",      // earliest date
  "P1326",      // latest date

  // Language.
  "P103",       // native language
  "P1412",      // languages spoken, written or signed
  "P6886",      // writing language
  "P37",        // official language
  "P2936",      // language used

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

}  // namespace nlp
}  // namespace sling
