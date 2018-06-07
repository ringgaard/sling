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

from math import sin, cos, atan2, sqrt, pow
import sling

cardinals = [
  'N', 'NNE', 'NE', 'ENE',
  'E', 'ESE', 'SE', 'SSE',
  'S', 'SSW', 'SW', 'WSW',
  'W', 'WNW', 'NW', 'NNW'
]

def radians(deg):
  return deg * math.pi / 180

def degrees(rad):
  return rad * 180 / math.pi

def square(x):
  return x * x

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
    self.coord = store['P625']
    self.earth = store['Q2']

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

  def si(self):
    siunit = self.unit[self.schema.si_unit]
    return Quantity((self.amount * siunit[self.schema.amount], siunit), schema)


class Globe:
  def __init__(self, body=None, store=None, schema=None):
    if body != None: store = body.store()
    if store == None: store = sling.Store()
    if schema == None: schema = MeasureSchema(store)
    if body == None: body = schema.earth
    self.body = body
    self.schema = schema

    # Determine radius of globe and convert to metres.
    radius = body[schema.radius]
    if radius != None:
      self.radius = Quantity(radius.resolve(), schema).si().amount
    else:
      diameter = body[schema.diameter]
      if diameter != None:
        self.radius = Quantity(diameter.resolve(), schema).si().amount / 2
      else:
        self.radius = 6371000  # radius of Earth in metres

  def coord(self, location):
    if location[self.schema.isa] == self.schema.geo:
      return location
    else:
      return location[self.schema.coord].resolve()

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

  def bearing(self, source, destination):
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

  def compass(self, source, destination, precision = 3):
    b = self.bearing(source, destination)
    n = 4 * pow(2, precision - 1)
    direction = int(round(bearing * n / 360) % n * (16 / n))
    return cardinals[direction]

