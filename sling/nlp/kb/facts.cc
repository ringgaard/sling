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

#include "sling/nlp/kb/facts.h"

#include "sling/base/logging.h"

namespace sling {
namespace nlp {

void FactCatalog::Init(Store *store) {
  // Initialize names.
  store_ = store;
  CHECK(names_.Bind(store));

  // Initialize calendar.
  calendar_.Init(store);

  // Determine extraction method for each property.
  for (const Slot &s : Frame(store, store->Lookup("/w/entity"))) {
    if (s.name != p_role_) continue;
    Frame property(store, s.value);
    Handle target = property.GetHandle(p_target_);
    if (target == n_item_) {
      bool is_location = false;
      for (const Slot &s : property) {
        if (s.name == p_subproperty_of_) {
          if (store->Resolve(s.value) == p_location_) is_location = true;
        }
      }
      if (is_location) {
        SetExtractor(property, &Facts::ExtractLocation);
      } else {
        SetExtractor(property, &Facts::ExtractSimple);
      }
    } else if (target == n_time_) {
      SetExtractor(property, &Facts::ExtractDate);
    }
  }

  // Set extraction method for specific properties.
  SetExtractor(p_instance_of_, &Facts::ExtractType);
  SetExtractor(p_subclass_of_, &Facts::ExtractSuperclass);
  SetExtractor(p_educated_at_, &Facts::ExtractAlmaMater);
  SetExtractor(p_employer_, &Facts::ExtractEmployer);
  SetExtractor(p_occupation_, &Facts::ExtractOccupation);
  SetExtractor(p_position_, &Facts::ExtractPosition);
  SetExtractor(p_member_of_sports_team_, &Facts::ExtractTeam);
  SetExtractor(p_time_period_, &Facts::ExtractTimePeriod);
  SetExtractor(p_described_by_source_, &Facts::ExtractNothing);
  SetExtractor(p_different_from_, &Facts::ExtractNothing);
  SetExtractor(p_located_at_body_of_water_, &Facts::ExtractSimple);
  SetExtractor(p_located_on_street_, &Facts::ExtractSimple);
  SetExtractor(p_given_name_, &Facts::ExtractName);
  SetExtractor(p_family_name_, &Facts::ExtractName);

  // Set up items that stop closure expansion.
  static const char *baseids[] = {
    "Q5",         // human
    "Q215627",    // person
    "Q17334923",  // location
    "Q811430",    // construction
    "Q43229",     // organization
    "Q6881511",   // enterprise
    "Q783794",    // company
    "Q2385804",   // educational institution
    "Q294163",    // public institution
    "Q15401930",  // product
    "Q12737077",  // occupation
    "Q192581",    // job
    "Q4164871",   // position
    "Q216353",    // title
    nullptr,
  };
  for (const char **id = baseids; *id != nullptr; ++id) {
    base_items_.insert(store_->Lookup(*id));
  }
}

Taxonomy *FactCatalog::CreateDefaultTaxonomy() {
  static const char *default_taxonomy[] = {
    "Q215627",     // person
    "Q95074",      // fictional character
    "Q729",        // animal
    "Q4164871",    // position
    "Q12737077",   // occupation
    "Q216353",     // title
    "Q618779",     // award
    "Q27020041",   // sports season
    "Q4438121",    // sports organization
    "Q215380",     // band
    "Q2385804",    // educational institution
    "Q783794",     // company
    "Q17334923",   // location
    "Q43229",      // organization
    "Q431289",     // brand
    "Q15474042",   // MediaWiki page
    "Q18616576",   // Wikidata property
    "Q2188189",    // musical work
    "Q571",        // book
    "Q732577",     // publication
    "Q11424",      // film
    "Q15416",      // television program
    "Q12136",      // disease
    "Q16521",      // taxon
    "Q5058355",    // cellular component
    "Q7187",       // gene
    "Q11173",      // chemical compound
    "Q811430",     // construction
    "Q618123",     // geographical object
    "Q1656682",    // event
    "Q101352",     // family name
    "Q202444",     // given name
    "Q577",        // year
    "Q186081",     // time interval
    "Q11563",      // number
    "Q17376908",   // languoid
    "Q47574",      // unit of measurement
    "Q39875001",   // measure
    "Q3695082",    // sign
    "Q2996394",    // biological process
    "Q11410",      // game
    "Q7397",       // software
    "Q838948",     // work of art
    "Q47461344",   // written work
    "Q28877",      // goods
    "Q15401930",   // product
    "Q483394",     // genre
    "Q121769",     // reference
    "Q1047113",    // specialty
    "Q1190554",    // occurrence
    "Q151885",     // concept
    "Q35120",      // entity
    nullptr,
  };
  return new Taxonomy(this, default_taxonomy);
}

Taxonomy *FactCatalog::CreateEntityTaxonomy() {
  // Taxonomy used for entity types.
  static const char *entity_types[] = {
    "Q215627",      // person
    "Q2385804",     // educational institution
    "Q13226383",    // facility
    "Q17334923",    // location
    "Q43229",       // organization
    "Q12737077",    // occupation
    "Q216353",      // title
    "Q4164871",     // position
    "Q1656682",     // event
    "Q386724",      // work
    "Q1047113",     // specialty
    "Q14795564",    // relative point in time
    "/w/quantity",  // quantity
    "/w/time",      // time
    "/w/geo",       // geopoint
    nullptr,
  };
  return new Taxonomy(this, entity_types);
}

bool FactCatalog::ItemInClosure(Handle property, Handle coarse, Handle fine) {
  if (coarse == fine) return true;

  Handles closure(store_);
  closure.push_back(fine);
  int current = 0;
  while (current < closure.size()) {
    Frame f(store_, closure[current++]);
    for (const Slot &s : f) {
      if (s.name == property) {
        Handle value = store_->Resolve(s.value);
        if (value == coarse) {
          return true;
        } else if (!IsBaseItem(value)) {
          bool known = closure.contains(value);
          if (!known) closure.push_back(value);
        }
      }
    }
  }

  return false;
}

void FactCatalog::ExtractItemTypes(Handle item, std::vector<Handle> *types) {
  // Get types for item.
  item = store_->Resolve(item);
  if (!store_->IsFrame(item)) return;
  for (const Slot &s : Frame(store_, item)) {
    if (s.name == p_instance_of_) {
      Handle type = store_->Resolve(s.value);
      types->push_back(type);
    }
  }

  // Build type closure.
  int current = 0;
  while (current < types->size()) {
    Frame f(store_, (*types)[current++]);
    if (IsBaseItem(f.handle())) continue;
    for (const Slot &s : f) {
      if (s.name != p_subclass_of_) continue;

      // Add new item unless it is already known.
      Handle newitem = store_->Resolve(s.value);
      bool known = false;
      for (Handle h : *types) {
        if (newitem == h) {
          known = true;
          break;
        }
      }
      if (!known) types->push_back(newitem);
    }
  }
}

bool FactCatalog::InstanceOf(Handle item, Handle type) {
  // Check types for item.
  Handles types(store_);
  item = store_->Resolve(item);
  if (!store_->IsFrame(item)) return false;
  for (const Slot &s : Frame(store_, item)) {
    if (s.name == p_instance_of_) {
      Handle t = store_->Resolve(s.value);
      if (t == type) return true;
      types.push_back(t);
    }
  }

  // Check type closure.
  int current = 0;
  while (current < types.size()) {
    Frame f(store_, types[current++]);
    if (IsBaseItem(f.handle())) continue;
    for (const Slot &s : f) {
      if (s.name != p_subclass_of_) continue;

      // Add new item unless it is already known.
      Handle t = store_->Resolve(s.value);
      if (t == type) return true;
      bool known = types.contains(t);
      if (!known) types.push_back(t);
    }
  }

  return false;
}

void Facts::Extract(Handle item) {
  // Extract facts from the properties of the item.
  auto &extractors = catalog_->property_extractors_;
  if (!store_->IsFrame(item)) return;
  for (const Slot &s : Frame(store_, item)) {
    // Look up extractor for property.
    auto f = extractors.find(s.name);
    if (f == extractors.end()) continue;

    // Extract facts for property.
    FactCatalog::Extractor extractor = f->second;
    push(s.name);
    int start = delimiters_.size();
    (this->*extractor)(s.value);
    int end = delimiters_.size();
    if (end > start) groups_.push_back(end);
    pop();
  }
}

void Facts::Expand(Handle property, Handle value) {
  auto &extractors = catalog_->property_extractors_;
  auto f = extractors.find(property);
  if (f == extractors.end()) return;

  FactCatalog::Extractor extractor = f->second;
  push(property);
  int start = delimiters_.size();
  (this->*extractor)(value);
  int end = delimiters_.size();
  if (end > start) groups_.push_back(end);
  pop();
}

void Facts::ExtractFor(Handle item, const HandleSet &properties) {
  // Extract facts from the properties of the item.
  auto &extractors = catalog_->property_extractors_;
  if (!store_->IsFrame(item)) return;
  for (const Slot &s : Frame(store_, item)) {
    // Ignore if property is not in the property set.
    if (properties.find(s.name) == properties.end()) continue;

    // Look up extractor for property.
    auto f = extractors.find(s.name);
    if (f == extractors.end()) continue;

    // Extract facts for property.
    FactCatalog::Extractor extractor = f->second;
    push(s.name);
    int start = delimiters_.size();
    (this->*extractor)(s.value);
    int end = delimiters_.size();
    if (end > start) groups_.push_back(end);
    pop();
  }
}

void Facts::ExtractSimple(Handle value) {
  AddFact(store_->Resolve(value));
}

void Facts::ExtractSimple(const Name &property, Handle value) {
  if (value.IsNil()) return;
  push(property);
  ExtractSimple(value);
  pop();
}

void Facts::ExtractNothing(Handle value) {
}

void Facts::ExtractClosure(Handle item, Handle relation) {
  item = store_->Resolve(item);
  if (!closure_) {
    AddFact(item);
    return;
  }

  Handles closure(store_);
  closure.push_back(item);
  int current = 0;
  while (current < closure.size()) {
    Handle h = closure[current++];
    if (!store_->IsFrame(h)) continue;
    if (catalog_->IsBaseItem(h)) continue;
    AddFact(h);
    Frame f(store_, h);
    for (const Slot &s : f) {
      if (s.name != relation) continue;

      // Add new item unless it is already known.
      Handle newitem = store_->Resolve(s.value);
      bool known = closure.contains(newitem);
      if (!known) closure.push_back(newitem);
    }
  }
}

void Facts::ExtractType(Handle type) {
  ExtractClosure(type, catalog_->p_subclass_of_.handle());
}

void Facts::ExtractSuperclass(Handle item) {
  item = store_->Resolve(item);
  if (!store_->IsFrame(item)) return;
  ExtractClosure(item, catalog_->p_subclass_of_.handle());

  push(catalog_->p_subclass_of_);
  for (const Slot &s : Frame(store_, item)) {
    if (s.name == catalog_->p_subclass_of_) {
      Frame superclass(store_, s.value);
      Handle of = superclass.GetHandle(catalog_->p_of_);
      if (!of.IsNil()) {
        push(catalog_->p_of_);
        AddFact(of);
        pop();
      }
    }
  }
  pop();
}

void Facts::ExtractClass(Handle item) {
  item = store_->Resolve(item);
  if (!store_->IsFrame(item)) return;
  for (const Slot &s : Frame(store_, item)) {
    if (s.name == catalog_->p_instance_of_) {
      push(catalog_->p_instance_of_);
      ExtractType(s.value);
      pop();
    }
  }
}

void Facts::ExtractProperty(Handle item, const Name &property) {
  Frame f(store_, store_->Resolve(item));
  Handle value = f.GetHandle(property);
  ExtractSimple(property, value);
}

void Facts::ExtractQualifier(Handle item, const Name &qualifier) {
  Frame f(store_, item);
  if (!f.Has(Handle::is())) return;
  Handle value = f.GetHandle(qualifier);
  ExtractSimple(qualifier, value);
}

void Facts::ExtractDate(Handle value) {
  value = store_->Resolve(value);
  if (!closure_) {
    AddFact(value);
    return;
  }

  // Convert value to date.
  Date date(Object(store_, value));

  // Add numeric dates.
  if (numeric_dates_) {
    // Add numeric date as fact.
    int day_number = date.AsNumber();
    if (day_number != -1) AddFact(Handle::Integer(day_number));

    // Back-off to month.
    if (date.precision == Date::DAY) {
      Date month(date.year, date.month, 0, Date::MONTH);
      int month_number = month.AsNumber();
      if (month_number != -1) AddFact(Handle::Integer(month_number));
    }

    // Back-off to year.
    if (date.precision == Date::DAY || date.precision == Date::MONTH) {
      Date year(date.year, 0, 0, Date::YEAR);
      int year_number = year.AsNumber();
      if (year_number != -1) AddFact(Handle::Integer(year_number));
    }
  }

  // Add facts for year, decade, and century.
  AddFact(catalog_->calendar_.Year(date));
  AddFact(catalog_->calendar_.Decade(date));
  AddFact(catalog_->calendar_.Century(date));
}

void Facts::ExtractTimePeriod(Handle period) {
  // Add fact for period.
  ExtractSimple(period);

  // Add facts for start and end time of period.
  period = store_->Resolve(period);
  if (!store_->IsFrame(period)) return;
  Frame f(store_, period);
  Handle start = f.GetHandle(catalog_->p_start_time_);
  if (!start.IsNil()) {
    push(catalog_->p_start_time_);
    ExtractDate(start);
    pop();
  }
  Handle end = f.GetHandle(catalog_->p_end_time_);
  if (!end.IsNil()) {
    push(catalog_->p_end_time_);
    ExtractDate(end);
    pop();
  }
}

void Facts::ExtractTime(Handle event) {
  if (!store_->IsFrame(event)) return;
  Frame f(store_, event);

  Handle time = f.GetHandle(catalog_->p_point_in_time_);
  if (!time.IsNil()) {
    push(catalog_->p_point_in_time_);
    ExtractDate(time);
    pop();
  }
  Handle start = f.GetHandle(catalog_->p_start_time_);
  if (!start.IsNil()) {
    push(catalog_->p_start_time_);
    ExtractDate(start);
    pop();
  }
  Handle end = f.GetHandle(catalog_->p_end_time_);
  if (!end.IsNil()) {
    push(catalog_->p_end_time_);
    ExtractDate(end);
    pop();
  }
}

void Facts::ExtractLocation(Handle location) {
  ExtractClosure(location, catalog_->p_located_in_.handle());
}

void Facts::ExtractPlacement(Handle item) {
  Frame f(store_, store_->Resolve(item));
  Handle value = f.GetHandle(catalog_->p_located_in_);
  if (!value.IsNil()) {
    push(catalog_->p_located_in_);
    ExtractLocation(value);
    pop();
  }
  Handle country = f.GetHandle(catalog_->p_country_);
  ExtractSimple(catalog_->p_country_, country);
}

void Facts::ExtractAlmaMater(Handle institution) {
  ExtractSimple(institution);
  ExtractClass(institution);
  ExtractPlacement(institution);
  ExtractQualifier(institution, catalog_->p_academic_degree_);
  ExtractTime(institution);
}

void Facts::ExtractEmployer(Handle employer) {
  ExtractSimple(employer);
  ExtractClass(employer);
  ExtractPlacement(employer);
}

void Facts::ExtractOccupation(Handle occupation) {
  ExtractType(occupation);
}

void Facts::ExtractPosition(Handle position) {
  Frame f(store_, position);
  ExtractType(position);
  ExtractTime(position);
  ExtractQualifier(position, catalog_->p_jurisdiction_);

  Handle organization = f.GetHandle(catalog_->p_of_);
  if (!organization.IsNil()) {
    push(catalog_->p_of_);
    ExtractEmployer(organization);
    pop();
  }

  Handle role = f.GetHandle(catalog_->p_subject_role_);
  if (!role.IsNil()) {
    push(catalog_->p_subject_role_);
    ExtractOccupation(role);
    pop();
  }
}

void Facts::ExtractTeam(Handle team) {
  ExtractSimple(team);
  ExtractTime(team);
  ExtractProperty(team, catalog_->p_league_);
}

void Facts::ExtractName(Handle name) {
  ExtractSimple(name);
  ExtractProperty(name, catalog_->p_instance_of_);
  ExtractProperty(name, catalog_->p_language_);
}

void Facts::AddFact(Handle value) {
  if (value.IsNil()) return;
  for (Handle p : path_) list_.push_back(p);
  list_.push_back(value);
  delimiters_.push_back(list_.size());
}

int Facts::FindGroup(Handle property, Handle value) const {
  for (int g = 0; g < groups_.size(); ++g) {
    int f = group_begin(g);
    if (simple(f) && first(f) == property && last(f) == value) return g;
  }
  return -1;
}

void Facts::RemoveGroup(int group) {
  DCHECK_LT(group, groups_.size());
  int gb = group_begin(group);
  int ge = group_end(group);
  int gn = ge - gb;

  // Remove fact predicates from list.
  int b = begin(gb);
  int e = end(ge - 1);
  int n = e - b;
  list_.erase(list_.begin() + b, list_.begin() + e);

  // Adjust fact delimiters.
  for (int f = ge; f < delimiters_.size(); ++f) {
    delimiters_[f - gn] = delimiters_[f] - n;
  }
  delimiters_.resize(delimiters_.size() - gn);

  // Adjust fact groups.
  for (int g = group + 1; g < groups_.size(); ++g) {
    groups_[g - 1] = groups_[g] - gn;
  }
  groups_.resize(groups_.size() - 1);
}

void Facts::CopyGroup(const Facts &facts, int group) {
  DCHECK_LT(group, facts.groups_.size());
  for (int f = facts.group_begin(group); f < facts.group_end(group); ++f) {
    for (int p = facts.begin(f); p < facts.end(f); ++p) {
      list_.push_back(facts.list_[p]);
    }
    delimiters_.push_back(list_.size());
  }
  groups_.push_back(delimiters_.size());
}

Handle Facts::AsArrays(Store *store) const {
  Array array(store, delimiters_.size());
  int pos = 0;
  for (int i = 0; i < delimiters_.size(); ++i) {
    int begin = pos;
    int end = delimiters_[i];
    array.set(i, store->AllocateArray(&list_[begin], &list_[end]));
    pos = end;
  }
  return array.handle();
}

void Facts::AsArrays(Store *store, Handles *array) const {
  array->clear();
  int pos = 0;
  for (int i = 0; i < delimiters_.size(); ++i) {
    int begin = pos;
    int end = delimiters_[i];
    array->push_back(store->AllocateArray(&list_[begin], &list_[end]));
    pos = end;
  }
}

Taxonomy::Taxonomy(const FactCatalog *catalog, const std::vector<Text> &types) {
  catalog_ = catalog;
  for (Text type : types) {
    Handle t = catalog->store_->LookupExisting(type);
    if (t.IsNil()) {
      LOG(WARNING) << "Ignoring unknown type in taxonomy: " << type;
      continue;
    }
    int rank = typemap_.size();
    typemap_[t] = rank;
  }
}

Taxonomy::Taxonomy(const FactCatalog *catalog, const char **types) {
  catalog_ = catalog;
  for (const char **type = types; *type != nullptr; ++type) {
    Handle t = catalog->store_->LookupExisting(*type);
    if (t.IsNil()) {
      LOG(WARNING) << "Ignoring unknown type in taxonomy: " << *type;
      continue;
    }
    int rank = typemap_.size();
    typemap_[t] = rank;
  }
}

Handle Taxonomy::Classify(const Frame &item) {
  // Get immediate types for item.
  Handles types(item.store());
  Store *store = item.store();
  for (const Slot &s : item) {
    if (s.name == catalog_->p_instance_of_) {
      Handle type = store->Resolve(s.value);
      if (store->IsFrame(type)) types.push_back(type);
    }
  }

  // Run over type closure to find the type with the lowest rank.
  int rank = typemap_.size();
  Handle best = Handle::nil();
  int current = 0;
  while (current < types.size()) {
    Frame type(store, types[current++]);
    auto f = typemap_.find(type.handle());
    if (f != typemap_.end()) {
      if (f->second < rank) {
        rank = f->second;
        best = type.handle();
      }
      continue;
    }

    // Recurse into the subclass-of relation.
    for (const Slot &s : type) {
      if (s.name != catalog_->p_subclass_of_) continue;

      // Add new type unless it is already known.
      Handle newtype = store->Resolve(s.value);
      bool known = types.contains(newtype);
      if (!known) types.push_back(newtype);
    }
  }

  return best;
}

}  // namespace nlp
}  // namespace sling

