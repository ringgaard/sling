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

#include <sstream>

#include "sling/pyapi/pywiki.h"

#include "sling/frame/object.h"
#include "sling/frame/reader.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/nlp/document/document-tokenizer.h"
#include "sling/nlp/wiki/wiki-annotator.h"
#include "sling/nlp/wiki/wiki-parser.h"
#include "sling/nlp/wiki/wikipedia-map.h"
#include "sling/pyapi/pyarray.h"
#include "sling/pyapi/pyframe.h"
#include "sling/stream/memory.h"

namespace sling {

// Python type declarations.
PyTypeObject PyWikiConverter::type;
PyMethodTable PyWikiConverter::methods;
PyTypeObject PyFactExtractor::type;
PyMethodTable PyFactExtractor::methods;
PyTypeObject PyTaxonomy::type;
PyMethodTable PyTaxonomy::methods;
PyTypeObject PyPlausibility::type;
PyMethodTable PyPlausibility::methods;
PyTypeObject PyWikipedia::type;
PyMethodTable PyWikipedia::methods;
PyTypeObject PyWikipediaPage::type;
PyMethodTable PyWikipediaPage::methods;

void PyWikiConverter::Define(PyObject *module) {
  InitType(&type, "sling.WikiConverter", sizeof(PyWikiConverter), true);
  type.tp_init = method_cast<initproc>(&PyWikiConverter::Init);
  type.tp_dealloc = method_cast<destructor>(&PyWikiConverter::Dealloc);

  methods.Add("convert_wikidata", &PyWikiConverter::ConvertWikidata);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "WikiConverter");
}

int PyWikiConverter::Init(PyObject *args, PyObject *kwds) {
  // Get store argument.
  pycommons = nullptr;
  converter = nullptr;
  const char *language = "en";
  if (!PyArg_ParseTuple(args, "O|s", &pycommons, &language)) return -1;
  if (!PyStore::TypeCheck(pycommons)) return -1;

  // Initialize converter.
  Py_INCREF(pycommons);
  converter = new nlp::WikidataConverter(pycommons->store, language);
  s_entities = pycommons->store->Lookup("entities");

  return 0;
}

void PyWikiConverter::Dealloc() {
  delete converter;
  if (pycommons) Py_DECREF(pycommons);
  Free();
}

PyObject *PyWikiConverter::ConvertWikidata(PyObject *args, PyObject *kw) {
  // Get store and Wikidata JSON string.
  PyStore *pystore = nullptr;
  Py_buffer json;
  if (!PyArg_ParseTuple(args, "Os*", &pystore, &json)) return nullptr;
  if (!PyStore::TypeCheck(pystore)) return nullptr;

  // Parse JSON.
  ArrayInputStream stream(json.buf, json.len);
  Input input(&stream);
  Reader reader(pystore->store, &input);
  reader.set_json(true);
  Object obj = reader.Read();
  PyBuffer_Release(&json);
  if (reader.error()) {
    PyErr_SetString(PyExc_ValueError, reader.error_message().c_str());
    return nullptr;
  }
  if (!obj.valid() || !obj.IsFrame()) {
    PyErr_SetString(PyExc_ValueError, "Not a valid frame");
    return nullptr;
  }

  // Skip the "entities" level added in Wikidata JSON requests.
  Frame item = obj.AsFrame();
  Frame entities = item.GetFrame(s_entities);
  if (entities.valid() && entities.size() == 1) {
    Frame subitem(item.store(), entities.value(0));
    if (subitem.valid()) item = subitem;
  }

  // Convert Wikidata JSON to SLING frame.
  uint64 revision = 0;
  const Frame &wikiitem = converter->Convert(item, &revision);

  // Return Wikidata item frame and revision.
  PyObject *pair = PyTuple_New(2);
  PyTuple_SetItem(pair, 0, pystore->PyValue(wikiitem.handle()));
  PyTuple_SetItem(pair, 1, PyLong_FromLong(revision));
  return pair;
}

void PyFactExtractor::Define(PyObject *module) {
  InitType(&type, "sling.FactExtractor", sizeof(PyFactExtractor), true);
  type.tp_init = method_cast<initproc>(&PyFactExtractor::Init);
  type.tp_dealloc = method_cast<destructor>(&PyFactExtractor::Dealloc);

  methods.Add("facts", &PyFactExtractor::Facts);
  methods.Add("facts_for", &PyFactExtractor::FactsFor);
  methods.Add("types", &PyFactExtractor::Types);
  methods.Add("taxonomy", &PyFactExtractor::Taxonomy);
  methods.Add("in_closure", &PyFactExtractor::InClosure);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "FactExtractor");
}

int PyFactExtractor::Init(PyObject *args, PyObject *kwds) {
  // Get store argument.
  pycommons = nullptr;
  catalog = nullptr;
  if (!PyArg_ParseTuple(args, "O", &pycommons)) return -1;
  if (!PyStore::TypeCheck(pycommons)) return -1;

  // Initialize fact extractor catalog.
  Py_INCREF(pycommons);
  catalog = new nlp::FactCatalog();
  catalog->Init(pycommons->store);

  return 0;
}

void PyFactExtractor::Dealloc() {
  delete catalog;
  if (pycommons) Py_DECREF(pycommons);
  Free();
}

PyObject *PyFactExtractor::Facts(PyObject *args, PyObject *kw) {
  // Get store and Wikidata item.
  PyStore *pystore = nullptr;
  PyFrame *pyitem = nullptr;
  bool closure = true;
  if (!PyArg_ParseTuple(args, "OO|b", &pystore, &pyitem, &closure)) {
    return nullptr;
  }
  if (!PyStore::TypeCheck(pystore)) return nullptr;
  if (!PyFrame::TypeCheck(pyitem)) return nullptr;

  // Extract facts.
  nlp::Facts facts(catalog);
  facts.set_closure(closure);
  facts.Extract(pyitem->handle());

  // Return array of facts.
  return pystore->PyValue(facts.AsArrays(pystore->store));
}

PyObject *PyFactExtractor::FactsFor(PyObject *args, PyObject *kw) {
  // Get store and Wikidata item.
  PyStore *pystore = nullptr;
  PyFrame *pyitem = nullptr;
  PyObject *pyproperties = nullptr;
  bool closure = true;
  if (!PyArg_ParseTuple(
      args, "OOO|b", &pystore, &pyitem, &pyproperties, &closure)) {
    return nullptr;
  }
  if (!PyStore::TypeCheck(pystore)) return nullptr;
  if (!PyFrame::TypeCheck(pyitem)) return nullptr;
  if (!PyList_Check(pyproperties)) return nullptr;

  HandleSet properties;
  int size = PyList_Size(pyproperties);
  for (int i = 0; i < size; ++i) {
    PyObject *item = PyList_GetItem(pyproperties, i);
    if (!PyFrame::TypeCheck(item)) return nullptr;
    Handle handle = reinterpret_cast<PyFrame *>(item)->handle();
    properties.insert(handle);
  }

  // Extract facts.
  nlp::Facts facts(catalog);
  facts.set_closure(closure);
  facts.ExtractFor(pyitem->handle(), properties);

  // Return array of facts.
  return pystore->PyValue(facts.AsArrays(pystore->store));
}

PyObject *PyFactExtractor::InClosure(PyObject *args, PyObject *kw) {
  // Get store and Wikidata item.
  PyFrame *pyproperty = nullptr;
  PyFrame *pycoarse = nullptr;
  PyFrame *pyfine = nullptr;
  if (!PyArg_ParseTuple(args, "OOO", &pyproperty, &pycoarse, &pyfine)) {
    return nullptr;
  }
  if (!PyFrame::TypeCheck(pyproperty)) return nullptr;
  if (!PyFrame::TypeCheck(pycoarse)) return nullptr;
  if (!PyFrame::TypeCheck(pyfine)) return nullptr;

  bool subsumes = catalog->ItemInClosure(pyproperty->handle(),
                                         pycoarse->handle(),
                                         pyfine->handle());

  return PyBool_FromLong(subsumes);
}

PyObject *PyFactExtractor::Types(PyObject *args, PyObject *kw) {
  // Get store and Wikidata item.
  PyStore *pystore = nullptr;
  PyFrame *pyitem = nullptr;
  if (!PyArg_ParseTuple(args, "OO", &pystore, &pyitem)) return nullptr;
  if (!PyStore::TypeCheck(pystore)) return nullptr;
  if (!PyFrame::TypeCheck(pyitem)) return nullptr;

  // Extract types.
  Handles types(pystore->store);
  catalog->ExtractItemTypes(pyitem->handle(), &types);

  // Return array of types.
  const Handle *begin = types.data();
  const Handle *end = begin + types.size();
  return pystore->PyValue(pystore->store->AllocateArray(begin, end));
}

PyObject *PyFactExtractor::Taxonomy(PyObject *args, PyObject *kw) {
  // Get type list from arguments.
  PyObject *pytypes = nullptr;
  if (!PyArg_ParseTuple(args, "|O", &pytypes)) return nullptr;

  // Return taxonomy.
  PyTaxonomy *taxonomy = PyObject_New(PyTaxonomy, &PyTaxonomy::type);
  taxonomy->Init(this, pytypes);
  return taxonomy->AsObject();
}

void PyTaxonomy::Define(PyObject *module) {
  InitType(&type, "sling.Taxonomy", sizeof(PyTaxonomy), false);
  type.tp_dealloc = method_cast<destructor>(&PyTaxonomy::Dealloc);

  methods.AddO("classify", &PyTaxonomy::Classify);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "Taxonomy");
}

int PyTaxonomy::Init(PyFactExtractor *extractor, PyObject *typelist) {
  // Keep reference to extractor to keep fact catalog alive.
  Py_INCREF(extractor);
  pyextractor = extractor;
  taxonomy = nullptr;

  if (typelist == nullptr) {
    // Create default taxonomy.
    taxonomy = pyextractor->catalog->CreateDefaultTaxonomy();
  } else {
    // Build type list.
    if (!PyList_Check(typelist)) {
      PyErr_BadArgument();
      return -1;
    }
    int size = PyList_Size(typelist);
    std::vector<Text> types;
    for (int i = 0; i < size; ++i) {
      PyObject *item = PyList_GetItem(typelist, i);
      if (!PyUnicode_Check(item)) {
        PyErr_BadArgument();
        return -1;
      }
      const char *name = PyUnicode_AsUTF8(item);
      if (name == nullptr) {
        PyErr_BadArgument();
        return -1;
      }
      types.emplace_back(name);
    }

    // Create taxonomy from type list.
    taxonomy = new nlp::Taxonomy(pyextractor->catalog, types);
  }

  return 0;
}

void PyTaxonomy::Dealloc() {
  delete taxonomy;
  if (pyextractor) Py_DECREF(pyextractor);
  Free();
}

PyObject *PyTaxonomy::Classify(PyObject *item) {
  // Get item frame.
  if (!PyFrame::TypeCheck(item)) return nullptr;
  PyFrame *pyframe = reinterpret_cast<PyFrame *>(item);

  // Classify item.
  Handle type = taxonomy->Classify(pyframe->AsFrame());

  return pyframe->pystore->PyValue(type);
}


void PyPlausibility::Define(PyObject *module) {
  InitType(&type, "sling.PlausibilityModel", sizeof(PyPlausibility), true);
  type.tp_init = method_cast<initproc>(&PyPlausibility::Init);
  type.tp_dealloc = method_cast<destructor>(&PyPlausibility::Dealloc);

  methods.Add("score", &PyPlausibility::Score);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "PlausibilityModel");
}

int PyPlausibility::Init(PyObject *args, PyObject *kwds) {
  // Get fact extractor and model file name arguments.
  pyextractor = nullptr;
  model = nullptr;
  char *filename = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &pyextractor, &filename)) return -1;
  if (!PyFactExtractor::TypeCheck(pyextractor)) return -1;

  // Initialize plausibility model.
  Py_INCREF(pyextractor);
  model = new nlp::PlausibilityModel();
  model->Load(pyextractor->pycommons->store, filename);

  return 0;
}

void PyPlausibility::Dealloc() {
  delete model;
  if (pyextractor) Py_DECREF(pyextractor);
  Free();
}

PyObject *PyPlausibility::Score(PyObject *args) {
  // Get item, property and value.
  PyFrame *pyitem = nullptr;
  PyFrame *pyprop = nullptr;
  PyObject *pyval = nullptr;
  if (!PyArg_ParseTuple(args, "OOO", &pyitem, &pyprop, &pyval)) return nullptr;
  if (!PyFrame::TypeCheck(pyitem)) return nullptr;
  if (!PyFrame::TypeCheck(pyprop)) return nullptr;
  Handle item = pyitem->handle();
  Handle prop = pyprop->handle();
  Handle value = pyextractor->pycommons->Value(pyval);

  // Get facts for item.
  nlp::Facts premise(pyextractor->catalog);
  premise.Extract(item);

  // Remove fact from premise.
  int g = premise.FindGroup(prop, value);
  if (g != -1) premise.RemoveGroup(g);

  // Expand fact property and value.
  nlp::Facts hypothesis(pyextractor->catalog);
  hypothesis.Expand(prop, value);

  // Score fact.
  float score = model->Score(premise, hypothesis);

  return PyFloat_FromDouble(score);
}

class WikipediaExtractor : public nlp::WikiLinkResolver {
 public:
  WikipediaExtractor(Store *store, const string &lang):
      store_(store), lang_(lang) {
    // Get language settings.
    Frame langinfo(store, "/lang/" + lang);
    if (langinfo.valid()) {
      category_prefix_ = langinfo.GetString("/lang/wikilang/wiki_category");
      template_prefix_ = langinfo.GetString("/lang/wikilang/wiki_template");
      image_prefix_ = langinfo.GetString("/lang/wikilang/wiki_image");
    }

    // Load Wikipedia mappings.
    if (!lang_.empty()) {
      string dir = "data/e/wiki/" + lang_;
      if (File::Exists(dir + "/redirects.sling")) {
        wikimap_.LoadRedirects(dir + "/redirects.sling");
      }
      if (File::Exists(dir + "/mapping.sling")) {
        wikimap_.LoadMapping(dir + "/mapping.sling");
      }
    }
    wikimap_.Freeze();

    // Initialize templates.
    Frame template_config(store, "/wp/templates/" + lang_);
    if (template_config.valid()) {
      templates_.Init(this, template_config);
    }

    // Initialize document schema.
    docnames_ = new nlp::DocumentNames(store);
  }

  ~WikipediaExtractor() { if (docnames_) docnames_->Release(); }

  // Wikipedia link resolver interface.
  Text ResolveLink(Text link) override {
    if (link.find('#') != -1) return Text();
    return wikimap_.LookupLink(lang_, link, nlp::WikipediaMap::ARTICLE);
  }

  Text ResolveTemplate(Text link) override {
    nlp::WikipediaMap::PageInfo info;
    if (!wikimap_.GetPageInfo(lang_, template_prefix_, link, &info)) {
      return Text();
    }
    if (info.type != nlp::WikipediaMap::TEMPLATE &&
        info.type != nlp::WikipediaMap::INFOBOX) {
      return Text();
    }
    return info.qid;
  }

  Text ResolveCategory(Text link) override {
    return wikimap_.LookupLink(lang_, category_prefix_, link,
                               nlp::WikipediaMap::CATEGORY);
  }

  Text ResolveMedia(Text link) override {
    return wikimap_.ResolveRedirect(lang_, image_prefix_, link);
  }

  // Tokenize document.
  void Tokenize(nlp::Document *document) const {
    tokenizer_.Tokenize(document);
  }

  nlp::DocumentNames *docnames() { return docnames_; }
  nlp::WikiTemplateRepository *templates() { return &templates_; }

 private:
  // Global store for Wikipedia extractor.
  Store *store_;

  // Wikipedia language code.
  string lang_;

  // Language settings.
  string category_prefix_;
  string template_prefix_;
  string image_prefix_;

  // Mapping from Wikipedia links to QIDs.
  nlp::WikipediaMap wikimap_;

  // Template definitions.
  nlp::WikiTemplateRepository templates_;

  // Document schema.
  nlp::DocumentNames *docnames_ = nullptr;

  // Document tokenizer.
  nlp::DocumentTokenizer tokenizer_;
};

class WikipediaPage {
 public:
  WikipediaPage(const char *wikitext)
      : wikitext_(wikitext), ast_(wikitext_.c_str()) {
    ast_.Parse();
  }

  nlp::WikiParser &ast() { return ast_; }

  // Extract tables from page.
  void ExtractTables(Store *store,
                     WikipediaExtractor *wikiex,
                     Handles *tables) {
    // Get symbols.
    Handle n_title = store->Lookup("title");
    Handle n_header = store->Lookup("header");
    Handle n_row = store->Lookup("row");

    // Run through all top-level nodes.
    nlp::WikiExtractor extractor(ast_);
    int n = ast_.node(0).first_child;
    string heading;
    while (n != -1) {
      auto &node = ast_.node(n);
      if (node.type == nlp::WikiParser::HEADING) {
        nlp::WikiPlainTextSink sink;
        extractor.Enter(&sink);
        extractor.ExtractNode(node);
        extractor.Leave(&sink);
        heading = sink.text();
      } else if (node.type == nlp::WikiParser::TABLE) {
        Builder table(store);
        string title = heading;
        Handles prevrow(store);
        std::vector<int> repeats;
        int r = node.first_child;
        while (r != -1) {
          auto &row = ast_.node(r);
          if (row.type == nlp::WikiParser::ROW) {
            // Skip empty rows.
            bool empty = true;
            for (int c = row.first_child; c != -1;) {
              auto &cell = ast_.node(c);
              if (cell.type == nlp::WikiParser::CELL ||
                  cell.type == nlp::WikiParser::HEADER) {
                empty = false;
                break;
              }
              c = cell.next_sibling;
            }

            if (!empty) {
              bool has_headers = false;
              Handles cells(store);
              int c = row.first_child;
              int colno = 0;
              while (c != -1) {
                auto &cell = ast_.node(c);
                if (cell.type == nlp::WikiParser::CELL) {
                  // Fill in repeated cells.
                  while (colno < repeats.size() && repeats[colno] > 0) {
                    if (colno < prevrow.size()) {
                      cells.push_back(prevrow[colno]);
                    } else {
                      cells.push_back(Handle::nil());
                    }
                    repeats[colno++]--;
                  }
                  int rowspan = extractor.GetIntAttr(cell, "rowspan", 1);
                  if (rowspan > 1) {
                    if (repeats.size() <= colno) repeats.resize(colno + 1);
                    repeats[colno] = rowspan - 1;
                  }

                  // Extract and annotate table cell.
                  nlp::WikiAnnotator annotator(store, wikiex);
                  annotator.set_templates(wikiex->templates());
                  extractor.Enter(&annotator);
                  extractor.ExtractNode(cell);
                  extractor.Leave(&annotator);

                  // Convert cell to document.
                  nlp::Document document(store, wikiex->docnames());
                  document.SetText(annotator.text());
                  wikiex->Tokenize(&document);
                  annotator.AddToDocument(&document);
                  document.Update();

                  // Add cell to row.
                  cells.push_back(document.top().handle());
                  int colspan = extractor.GetIntAttr(cell, "colspan", 1);
                  colno += colspan;
                  while (--colspan > 0) cells.push_back(Handle::nil());
                } else if (cell.type == nlp::WikiParser::HEADER) {
                  // Extract header cell as plain text.
                  nlp::WikiPlainTextSink sink;
                  extractor.Enter(&sink);
                  extractor.ExtractNode(cell);
                  extractor.Leave(&sink);
                  cells.push_back(store->AllocateString(sink.text()));
                  has_headers = true;
                }

                c = cell.next_sibling;
              }

              // Fill in repeated cells at end of row.
              while (colno < repeats.size() && repeats[colno] > 0) {
                if (colno < prevrow.size()) {
                  cells.push_back(prevrow[colno]);
                } else {
                  cells.push_back(Handle::nil());
                }
                repeats[colno++]--;
              }

              // Add row.
              if (!cells.empty()) {
                table.Add(has_headers ? n_header : n_row, cells);
                prevrow.swap(cells);
              }
            }
          } else if (row.type == nlp::WikiParser::CAPTION) {
            // Extract caption as plain text.
            nlp::WikiPlainTextSink sink;
            extractor.Enter(&sink);
            extractor.ExtractNode(row);
            extractor.Leave(&sink);
            title = sink.text();
          }

          r = row.next_sibling;
        }
        table.Add(n_title, title);
        tables->push_back(table.Create().handle());
      }
      n = node.next_sibling;
    }
  }

 private:
  // Wikipedia markup for Wikipedia page.
  string wikitext_;

  // Parsed Wikipedia page with AST.
  nlp::WikiParser ast_;
};

void PyWikipedia::Define(PyObject *module) {
  InitType(&type, "sling.Wikipedia", sizeof(PyWikipedia), true);
  type.tp_init = method_cast<initproc>(&PyWikipedia::Init);
  type.tp_dealloc = method_cast<destructor>(&PyWikipedia::Dealloc);

  methods.AddO("lookup", &PyWikipedia::Lookup);
  methods.AddO("parse", &PyWikipedia::Parse);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "Wikipedia");
}

int PyWikipedia::Init(PyObject *args, PyObject *kwds) {
  // Get store argument.
  pystore = nullptr;
  wikiex = nullptr;
  const char *language = "en";
  if (!PyArg_ParseTuple(args, "O|s", &pystore, &language)) return -1;
  if (!PyStore::TypeCheck(pystore)) return -1;

  // Initialize converter.
  Py_INCREF(pystore);
  wikiex = new WikipediaExtractor(pystore->store, language);

  return 0;
}

void PyWikipedia::Dealloc() {
  delete wikiex;
  if (pystore) Py_DECREF(pystore);
  Free();
}

PyObject *PyWikipedia::Lookup(PyObject *title) {
  Text link = GetText(title);
  if (link.data() == nullptr) return nullptr;
  return AllocateString(wikiex->ResolveLink(link));
}

PyObject *PyWikipedia::Parse(PyObject *wikitext) {
  // Get wikitext and make a copy for the Wikipedia parser.
  Text text = GetText(wikitext);
  if (text.data() == nullptr) return nullptr;

  // Parse wikitext.
  WikipediaPage *page = new WikipediaPage(text.data());

  // Return wikipedia page object.
  PyWikipediaPage *wp = PyObject_New(PyWikipediaPage, &PyWikipediaPage::type);
  wp->Init(this, page);
  return wp->AsObject();
}

void PyWikipediaPage::Define(PyObject *module) {
  InitType(&type, "sling.WikipediaPage", sizeof(PyWikipediaPage), false);
  type.tp_dealloc = method_cast<destructor>(&PyWikipediaPage::Dealloc);

  methods.Add("annotate", &PyWikipediaPage::Annotate);
  methods.Add("tables", &PyWikipediaPage::Tables);
  methods.Add("ast", &PyWikipediaPage::AST);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "WikipediaPage");
}

void PyWikipediaPage::Init(PyWikipedia *wikipedia, WikipediaPage *wikipage) {
  // Keep reference to wikipedia wrapper to keep extractor alive.
  Py_INCREF(wikipedia);
  pywikipedia = wikipedia;
  page = wikipage;
}

void PyWikipediaPage::Dealloc() {
  delete page;
  if (pywikipedia) Py_DECREF(pywikipedia);
  Free();
}

PyObject *PyWikipediaPage::Annotate(PyObject *args, PyObject *kwds) {
  // Get store for document.
  PyStore *pystore = nullptr;
  bool skip_tables = false;
  static const char *kwlist[] = {"store", "skip_tables", nullptr};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|b",
      const_cast<char **>(kwlist),
      &pystore, &skip_tables)) return nullptr;
  if (!PyStore::TypeCheck(pystore)) return nullptr;
  if (!pystore->Writable()) return nullptr;
  Store *store = pystore->store;

  // Extract annotations.
  nlp::WikiExtractor extractor(page->ast());
  nlp::WikiAnnotator annotator(store, pywikipedia->wikiex);
  annotator.set_templates(pywikipedia->wikiex->templates());
  extractor.set_skip_tables(skip_tables);
  extractor.Extract(&annotator);

  // Add annotations to document.
  nlp::Document document(store);
  document.SetText(annotator.text());
  pywikipedia->wikiex->Tokenize(&document);
  annotator.AddToDocument(&document);
  document.Update();

  return pystore->PyValue(document.top().handle());
}

PyObject *PyWikipediaPage::AST() {
  std::stringstream ss;
  page->ast().PrintAST(ss, 0, 0);
  return AllocateString(ss.str());
}

PyObject *PyWikipediaPage::Tables(PyObject *args) {
  PyStore *pystore = nullptr;
  if (!PyArg_ParseTuple(args, "O", &pystore)) return nullptr;
  if (!PyStore::TypeCheck(pystore)) return nullptr;
  Store *store = pystore->store;
  Handles tables(store);
  page->ExtractTables(store, pywikipedia->wikiex, &tables);

  PyArray *array = PyObject_New(PyArray, &PyArray::type);
  array->Init(pystore, tables);
  return array->AsObject();
}

}  // namespace sling

