import sling.pysling as api

from sling.log import *
from sling.nlp.document import *
from sling.nlp.parser import *

VERSION="3.0.0"

Store=api.Store
Frame=api.Frame
Array=api.Array
String=api.String

RecordReader=api.RecordReader
RecordDatabase=api.RecordDatabase
RecordWriter=api.RecordWriter

PhraseTable=api.PhraseTable
Calendar=api.Calendar
Date=api.Date

WikiConverter=api.WikiConverter
FactExtractor=api.FactExtractor
PlausibilityModel=api.PlausibilityModel
WebArchive=api.WebArchive

MILLENNIUM=api.MILLENNIUM
CENTURY=api.CENTURY
DECADE=api.DECADE
YEAR=api.YEAR
MONTH=api.MONTH
DAY=api.DAY

CASE_INVALID=api.CASE_INVALID
CASE_NONE=api.CASE_NONE
CASE_UPPER=api.CASE_UPPER
CASE_LOWER=api.CASE_LOWER
CASE_TITLE=api.CASE_TITLE

# Print out version and location of SLING Python API.
def which():
  import os
  import time
  location = __path__[0]
  if (os.path.islink(location)): location += " -> " + os.readlink(location)
  ts = time.ctime(os.path.getmtime(sling.api.__file__))
  print("SLING API version %s (%s) in %s" % (VERSION, ts, location))

