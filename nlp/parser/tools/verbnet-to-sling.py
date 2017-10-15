# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License")

# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Convert VerbNet to SLING schemas.
"""

from nltk.corpus import wordnet as wn
import xml.etree.ElementTree as ET
import os
import sys

# VerbNet can be downloaded from here:
# http://verbs.colorado.edu/verbnet_downloads/downloads.html
verbnet_path = '../vn/'
schemas = []

def StringLiteral(s):
  """Returns string literal for text."""
  return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def VerbNetId(name):
  """Get schema name for VN class."""
  parts = name.replace('-', '.').split('.')
  hier = []
  for p in parts:
    if p.isdigit(): hier.append(p)
  return '/vn/' + '-'.join(hier)

def SenseNumber(lemma):
  """Get WordNet sense number for lemma."""
  i = 1
  for l in wn.lemmas(lemma.name):
    if l is lemma: return i
    if l.synset.pos == lemma.synset.pos: i = i + 1
  return 9999

def SenseId(lemma):
  """Get SLING id for sense."""
  name = lemma.name.replace('\'', '_').replace('.', '_').lower()
  number = SenseNumber(lemma)
  return '/wn/' + lemma.synset.pos + '/' + name + '/' + str(number)

# Build mapping from WordNet sense keys to sense ids.
sense_mapping = {}
for s in wn.all_synsets():
  for l in s.lemmas:
    sense_mapping[l.key] = SenseId(l)

# Parse VerbNet class.
def parse_vn_class(vnclass, parent, parent_rolemap):
  vnid = vnclass.get('ID')
  schema_name = VerbNetId(vnid)
  schemas.append(schema_name)
  rolemap = parent_rolemap.copy()

  # Output schema for VerbNet class.
  print '{=' + schema_name + ' :schema' + ' +' + parent
  print '  name: ' + StringLiteral(vnid)
  print '  family: /schema/verbnet'

  # Output predicates triggers including WordNet senses.
  members = vnclass.find('MEMBERS')
  if members != None:
    for member in members.iterfind('MEMBER'):
      lemma = member.get('name').replace('_', ' ')
      wn = member.get("wn")
      print '  trigger: {'
      print '    lemma: ' + StringLiteral(lemma)
      print '    pos: verb'
      if len(wn) > 0:
        for sense in wn.replace('\n', ' ').split(' '):
          wnsense = sense_mapping.get(sense + '::')
          if wnsense != None:
            print '    sense: ' + wnsense

      print '  }'

  # Output roles.
  roles = vnclass.find('THEMROLES')
  specialized_roles = set()
  if roles != None:
    for role in roles.iterfind('THEMROLE'):
      role_type = role.get('type')
      role_name = schema_name + '/' + role_type.lower()
      if role_type in rolemap:
        role_base = rolemap[role_type]
      else:
        role_base = '/vn/class/' + role_type.lower()

      rolemap[role_type] = role_name
      specialized_roles.add(role_type)

      restricts = []
      selrestrs = role.find('SELRESTRS')
      if selrestrs != None:
        for restr in selrestrs.iterfind('SELRESTR'):
          if restr.get('Value') == '+':
            r = '/vn/' + restr.get('type')
            restricts.append(r)

      target = ''
      if len(restricts) == 1:
        target = ' target: ' + restricts[0]

      print '  role: {=' + role_name + ' :slot +' + role_base + ' name: ' + \
            StringLiteral(role_type) + ' source: ' + schema_name + target +'}'

  # Output inherited roles from parent class.
  for role_type in parent_rolemap:
    if role_type not in specialized_roles:
      role_name = schema_name + '/' + role_type.lower()
      role_base = parent_rolemap[role_type]
      print '  role: {=' + role_name + ' :slot +' + role_base + ' name: ' + \
            StringLiteral(role_type) + ' source: ' + schema_name +'}'
      rolemap[role_type] = role_name

  print '}'
  print

  # Output subclasses.
  subclasses = vnclass.find('SUBCLASSES')
  if subclasses != None:
    for e in subclasses.iterfind('VNSUBCLASS'):
      parse_vn_class(e, schema_name, rolemap)

# Parse all the VerbNet files.
for filename in os.listdir(verbnet_path):
  if not filename.endswith('.xml'): continue
  sourcefn = verbnet_path + filename

  tree = ET.parse(sourcefn)
  root = tree.getroot()

  parse_vn_class(root, '/vn/class', {})

# Output schema catalog.
print '{=/schema/verbnet :schema_family'
print '  name: "VerbNet schemas"'
print '  precompute_templates: 1'
print '  precompute_projections: 1'
print
for schema in schemas: print '  member_schema: ' + schema
print '}'

