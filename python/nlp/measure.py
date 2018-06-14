# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
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

"""Classes for measures"""

# See: https://www.movable-type.co.uk/scripts/latlong.html

from math import sin, cos, atan2, sqrt, pow, pi
from collections import deque
import sling

cardinals = [
  'N', 'NNE', 'NE', 'ENE',
  'E', 'ESE', 'SE', 'SSE',
  'S', 'SSW', 'SW', 'WSW',
  'W', 'WNW', 'NW', 'NNW'
]

def radians(deg):
  return deg * pi / 180

def degrees(rad):
  return rad * 180 / pi

def square(x):
  return x * x

# Topological sorting using DFS and gray/black colors.
GRAY = 0
BLACK = 1

def topological(graph):
  order = deque()
  enter = set(graph)
  state = {}

  def dfs(node):
    state[node] = GRAY
    for k in graph.get(node, ()):
      sk = state.get(k, None)
      if sk == GRAY:
        print "cycle", node, k
        continue
        #raise ValueError("cycle")
      if sk == BLACK: continue
      enter.discard(k)
      dfs(k)
    order.appendleft(node)
    state[node] = BLACK

  while enter: dfs(enter.pop())
  return order

class MeasureSchema:
  def __init__(self, store):
    self.isa = store['isa']

    self.quantity = store['/w/quantity']
    self.amount = store['/w/amount']
    self.unit = store['/w/unit']
    self.si_unit = store['P2370']

    self.geo = store['/w/geo']
    self.lat = store['/w/lat']
    self.lng = store['/w/lng']
    self.globe = store['/w/globe']

    self.radius = store['P2120']
    self.diameter = store['P2386']
    self.coordinate_location = store['P625']
    self.shares_border_with = store['P47']
    self.end_time = store['P582']
    self.applies_to_part = store['P518']
    self.located_in = store['P131']
    self.location = store['P276']
    self.part_of = store['P361']
    self.instance_of = store['P31']
    self.subclass_of = store['P279']
    self.country = store['P17']
    self.continent = store['P30']
    self.astronomical_body = store['P376']
    self.containment = [
      self.located_in,
      self.location,
      self.country,
      self.continent,
      self.part_of,
      self.astronomical_body,
    ]

    self.n_earth = store['Q2']
    self.n_scandinavia = store['Q21195']
    self.n_astronomical_body = store['Q6999']

  def has_type(self, item, cls):
    queue = []
    for t in item(self.instance_of):
      t = t.resolve()
      if t == cls: return True
      queue.append(t)
    i = 0
    while i < len(queue):
      current = queue[i]
      i += 1
      for t in current(self.subclass_of):
        t = t.resolve()
        if t == cls: return True
        if t not in queue: queue.append(t)
    return False

  def coord(self, location):
    if location == None: raise ValueError("No location")
    l = location.resolve()
    if self.coordinate_location in l: l = l[self.coordinate_location].resolve()
    if l[self.isa] == self.geo: return l
    raise ValueError("Cannot determine location for " + str(l))

class Quantity:
  def __init__(self, item, schema=None):
    self.schema = schema
    if self.schema == None: self.schema = MeasureSchema(item.store())
    if type(item) is sling.Frame:
      self.amount = item[self.schema.amount]
      self.unit = item[self.schema.unit]
    else:
      self.amount = item[0]
      self.unit = item[1]

  def convert_to(self, unit):
    amount = self.amount * unit[self.schema.amount]
    return Quantity((amount, unit), self.schema)

  def si(self):
    siunit = self.unit[self.schema.si_unit]
    return self.convert_to(siunit)


class Globe:
  def __init__(self, body=None, store=None, schema=None):
    if body != None: store = body.store()
    if store == None: store = sling.Store()
    if schema == None: schema = MeasureSchema(store)
    if body == None: body = schema.n_earth
    self.body = body
    self.schema = schema

    # Determine radius of globe and convert to metres.
    radius = self.body[self.schema.radius]
    if radius != None:
      self.radius = Quantity(radius.resolve(), schema).si().amount
    else:
      diameter = body[schema.diameter]
      if diameter != None:
        self.radius = Quantity(diameter.resolve(), schema).si().amount / 2
      else:
        self.radius = 6371000  # radius of Earth in metres

  def coord(self, location):
    return self.schema.coord(location)

  def distance(self, source, destination):
    # Get lat/lng for source and destination.
    coord1 = self.coord(source)
    coord2 = self.coord(destination)
    lat1 = coord1[self.schema.lat]
    lng1 = coord1[self.schema.lng]
    lat2 = coord2[self.schema.lat]
    lng2 = coord2[self.schema.lng]

    # Use the haversine formula to calculate the great-circle distance.
    phi1 = radians(lat1)
    phi2 = radians(lat2)
    dphi = radians(lat2 - lat1)
    dlambda = radians(lng2 - lng1)
    a = square(sin(dphi / 2)) + cos(phi1) * cos(phi2) * square(sin(dlambda / 2))
    c = 2 * atan2(sqrt(a), sqrt(1 - a))
    return self.radius * c

  def initial_bearing(self, source, destination):
    # Get lat/lng for source and destination.
    coord1 = self.coord(source)
    coord2 = self.coord(destination)
    lat1 = coord1[self.schema.lat]
    lng1 = coord1[self.schema.lng]
    lat2 = coord2[self.schema.lat]
    lng2 = coord2[self.schema.lng]

    # Compute the initial bearing (aka forward azimuth).
    phi1 = radians(lat1)
    lambda1 = radians(lng1)
    phi2 = radians(lat2)
    lambda2 = radians(lng2)
    dlambda = radians(lng2 - lng1)
    y = sin(dlambda) * cos(phi2)
    x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dlambda)
    return (degrees(atan2(y, x)) + 360) % 360

  def final_bearing(self, source, destination):
    return (self.initial_bearing(destination, source) + 180) % 360

  def bearing(self, source, destination):
    coord1 = self.coord(source)
    coord2 = self.coord(destination)
    initial = self.initial_bearing(coord1, coord2)
    final = self.final_bearing(coord1, coord2)
    return (initial + final) / 2

  def compass(self, source, destination, precision = 3):
    b = self.bearing(source, destination)
    n = 4 * pow(2, precision - 1)
    direction = int(round(b * n / 360) % n * (16 / n))
    return cardinals[direction]


class Universe:
  def __init__(self, store=None, schema=None):
    if store == None: store = sling.Store()
    if schema == None: schema = MeasureSchema(store)
    self.schema = schema
    self.earth = Globe(self.schema.n_earth)
    self.globes = {}
    self.globes[self.schema.n_earth] = self.earth

  def globe(self, coord):
    g = coord[self.schema.globe]
    if g == None: return self.earth
    if not self.schema.has_type(g, self.schema.n_astronomical_body):
      print g, "is not a globe"
      return self.earth
    if g not in self.globes: self.globes[g] = Globe(g, schema=self.schema)
    return self.globes[g]

  def located(self, location):
    if location == None: return False
    l = location.resolve()
    if self.schema.coordinate_location in l:
      l = l[self.schema.coordinate_location].resolve()
    return l[self.schema.isa] == self.schema.geo

  def locate(self, source, destination):
    coord1 = self.schema.coord(source)
    coord2 = self.schema.coord(destination)
    globe1 = self.globe(coord1)
    globe2 = self.globe(coord2)
    if globe1 != globe2:
      raise ValueError(str(source) + " and " + str(destination) +
                       " are not the same globe, " +
                       str(globe1.body) + " vs " + str(globe2.body))
    return globe1, coord1, coord2

  def distance(self, source, destination):
    globe, coord1, coord2 = self.locate(source, destination)
    return globe.distance(coord1, coord2)

  def bearing(self, source, destination):
    globe, coord1, coord2 = self.locate(source, destination)
    return globe.bearing(coord1, coord2)

  def compass(self, source, destination):
    globe, coord1, coord2 = self.locate(source, destination)
    return globe.compass(coord1, coord2)

  def containment_graph(self, location):
    queue = [location]
    graph = {}
    current = 0
    while current < len(queue):
      loc = queue[current]
      current += 1
      if loc == self.schema.n_earth: continue
      if loc == self.schema.n_scandinavia: continue  # hack due to country: cycle
      edges = graph.get(loc)
      if edges == None:
        edges = []
        graph[loc] = edges

      for name, value in loc:
        if name not in self.schema.containment: continue
        if value[self.schema.end_time]: continue
        if value[self.schema.applies_to_part]: continue
        l = value.resolve()
        if l != loc: edges.append(l)
        if l not in queue: queue.append(l)
    return graph

  def containment(self, location):
    return topological(self.containment_graph(location))

  def inside(self, part, whole):
    return whole in self.containment(part)

  def common(self, source, destination):
    sc = self.containment(source)
    dc = self.containment(destination)
    for s in sc:
      if s in dc: return s
    return None

  def borders(self, source, destination):
    for neighbor in source(self.schema.shares_border_with):
      if neighbor.resolve() == destination: return True
    for neighbor in destination(self.schema.shares_border_with):
      if neighbor.resolve() == source: return True
    return False

