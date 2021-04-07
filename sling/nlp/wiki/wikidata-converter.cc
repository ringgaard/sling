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

#include "sling/nlp/wiki/wikidata-converter.h"

#include "sling/frame/serialization.h"
#include "sling/nlp/kb/calendar.h"
#include "sling/nlp/wiki/wiki.h"
#include "sling/string/strcat.h"

namespace sling {
namespace nlp {

// Conversion table for Wikidata JSON date precision.
static Date::Precision date_precision[] = {
  Date::NONE,       // 0: 1 Gigayear
  Date::NONE,       // 1: 100 Megayears
  Date::NONE,       // 2: 10 Megayears
  Date::NONE,       // 3: Megayear
  Date::NONE,       // 4: 100 Kiloyears
  Date::NONE,       // 5: 10 Kiloyears
  Date::MILLENNIUM, // 6: Kiloyear
  Date::CENTURY,    // 7: 100 years
  Date::DECADE,     // 8: 10 years
  Date::YEAR,       // 9: years
  Date::MONTH,      // 10: months
  Date::DAY,        // 11: days
  Date::NONE,       // 12: hours (unused)
  Date::NONE,       // 13: minutes (unused)
  Date::NONE,       // 14: seconds (unused)
};

WikidataConverter::WikidataConverter(Store *commons, const string &language) {
  // Initialize global symbols.
  names_.Bind(commons);

  // Build type mapping.
  auto AddTypeMapping = [this, commons](Text datatype, Text type) {
    datatypes_[datatype] = commons->Lookup(type);
  };
  AddTypeMapping(s_string_.name(), "/w/string");
  AddTypeMapping(s_time_.name(), "/w/time");
  AddTypeMapping(s_quantity_.name(), "/w/quantity");
  AddTypeMapping(s_monolingualtext_.name(), "/w/text");
  AddTypeMapping(s_wikibase_item_.name(), "/w/item");
  AddTypeMapping(s_wikibase_lexeme_.name(), "/w/lexeme");
  AddTypeMapping(s_wikibase_form_.name(), "/w/form");
  AddTypeMapping(s_wikibase_sense_.name(), "/w/sense");
  AddTypeMapping(s_commons_media_.name(), "/w/media");
  AddTypeMapping(s_external_id_.name(), "/w/xref");
  AddTypeMapping(s_wikibase_property_.name(), "/w/property");
  AddTypeMapping(s_url_.name(), "/w/url");
  AddTypeMapping(s_globe_coordinate_.name(), "/w/geo");
  AddTypeMapping(s_math_.name(), "/w/math");
  AddTypeMapping(s_tabular_data_.name(), "/w/table");
  AddTypeMapping(s_geo_shape_.name(), "/w/shape");
  AddTypeMapping(s_musical_notation_.name(), "/w/music");

  // Get primary language.
  primary_language_name_ = language;
  if (primary_language_name_.empty()) {
    primary_language_name_ = Wiki::language_priority[0];
  }
  primary_language_ = commons->Lookup(primary_language_name_);

  // Initialize per-language information.
  const char **lang = Wiki::language_priority;
  int priority = 1;
  while (*lang != nullptr) {
    LanguageInfo info;
    info.priority = priority++;
    info.language = commons->Lookup(StrCat("/lang/", *lang));
    info.wikisite = commons->Lookup(StrCat(*lang, "wiki"));
    languages_[commons->Lookup(*lang)] = info;
    language_map_[*lang] = info.language;
    lang++;
  }
  languages_[primary_language_].priority = 0;
  language_map_["mul"] = n_lang_mul_.handle();
  language_map_["zxx"] = n_lang_none_.handle();

  language_order_.resize(priority + 1);
  for (auto &it : languages_) {
    language_order_[it.second.priority] = it.second.language;
  }
}

Frame WikidataConverter::Convert(const Frame &item,
                                 uint64 *revision,
                                 string *modified) {
  // Get top-level item attributes.
  Store *store = item.store();
  string id = item.GetString(s_id_);
  string type = item.GetString(s_type_);
  Frame labels = item.GetFrame(s_labels_);
  Frame descriptions = item.GetFrame(s_descriptions_);

  // Get revision.
  if (revision != nullptr) {
    *revision = -1;
    Handle lastrevid = item.GetHandle(s_lastrevid_);
    if (!lastrevid.IsNil()) {
      if (lastrevid.IsInt()) {
        *revision = lastrevid.AsInt();
      } else if (store->IsString(lastrevid)) {
        Text str = String(store, lastrevid).text();
        safe_strtou64(str.data(), str.size(), revision);
      }
    }
  }

  // Get last modification date.
  if (modified != nullptr) {
    Handle modtime = item.GetHandle(s_modified_);
    if (!modtime.IsNil() && store->IsString(modtime)) {
      *modified = String(store, modtime).value();
    }
  }

  // Create builder for constructing the frame for the item.
  Builder builder(store);
  if (!id.empty()) builder.AddId(id);
  WikidataType kind;
  if (type == "property") {
    kind = WIKIDATA_PROPERTY;
    builder.AddIsA(n_property_);
  } else if (type == "lexeme") {
    kind = WIKIDATA_LEXEME;
    builder.AddIsA(n_lexeme_);
  } else {
    kind = WIKIDATA_ITEM;
    builder.AddIsA(n_item_);
  }

  // Get label and description in all supported languages.
  int num_languages = language_order_.size();
  Slots names(store);
  names.resize(num_languages);
  Handle other_name = Handle::nil();
  if (labels.valid()) {
    for (const Slot &l : labels) {
      // Get language.
      auto f = languages_.find(l.name);
      if (f != languages_.end()) {
        int priority = f->second.priority;
        CHECK_LT(priority, num_languages);
        names[priority].name = l.value;
      } else if (other_name.IsNil()) {
        other_name = l.value;
      }
    }
  }
  if (descriptions.valid()) {
    for (const Slot &l : descriptions) {
      // Get language.
      auto f = languages_.find(l.name);
      if (f != languages_.end()) {
        int priority = f->second.priority;
        CHECK_LT(priority, num_languages);
        names[priority].value = l.value;
      }
    }
  }

  bool name_found = false;
  for (int i = 0; i < num_languages; ++i) {
    Slot &s = names[i];
    if (!s.name.IsNil()) {
      Handle name = Frame(store, s.name).GetHandle(s_value_);
      builder.Add(n_name_, name, language_order_[i]);
      name_found = true;

      if (!s.value.IsNil()) {
        Handle description = Frame(store, s.value).GetHandle(s_value_);
        builder.Add(n_description_, description, language_order_[i]);
      }
    }

    // Skip the remaining languages if only primary language (priority 0) is
    // allowed.
    if (only_primary_language_) break;
  }
  if (!name_found && !other_name.IsNil() &&
      !(only_primary_language_ || only_known_languages_)) {
    // Add fallback name.
    string name = Frame(store, other_name).GetString(s_value_);
    builder.Add(n_name_, name);
  }

  // Add data type for property.
  if (kind == WIKIDATA_PROPERTY) {
    String datatype = item.Get(s_datatype_).AsString();
    CHECK(!datatype.IsNil());
    auto f = datatypes_.find(datatype.text());
    CHECK(f != datatypes_.end()) << datatype.text();
    builder.Add(n_source_, n_entity_);
    builder.Add(n_target_, f->second);
  }

  // Parse aliases.
  Frame aliases = item.GetFrame(s_aliases_);
  if (aliases.valid()) {
    for (auto &it : languages_) {
      // Get aliases for language.
      Handle lang = it.second.language;
      if (only_primary_language_ && it.second.priority != 0) continue;
      Array alias_list = aliases.Get(it.first).AsArray();
      if (alias_list.valid()) {
        for (int i = 0; i < alias_list.length(); ++i) {
          Handle name = Frame(store, alias_list.get(i)).GetHandle(s_value_);
          builder.Add(n_alias_, name, lang);
        }
      }
    }
  }

  // Parse claims.
  Frame claims = item.GetFrame(s_claims_);
  if (claims.valid()) {
    for (const Slot &property : claims) {
      Array statement_list(store, property.value);
      for (int i = 0; i < statement_list.length(); ++i) {
        // Parse statement.
        Frame statement(store, statement_list.get(i));
        Frame snak = statement.GetFrame(s_mainsnak_);
        CHECK(snak.valid());
        Handle property = snak.GetHandle(s_property_);
        CHECK(!property.IsNil());
        Frame datavalue = snak.GetFrame(s_datavalue_);
        if (datavalue.invalid()) continue;

        // Get statement rank, 0=deprecated, 1=normal, 2=preferred.
        int ranking = 1;
        Text rank = statement.GetText(s_rank_);
        if (!rank.empty() && rank != "normal") {
          if (rank == "preferred") ranking = 2;
          if (rank == "deprecated") ranking = 0;
        }

        Object value(store, ConvertValue(datavalue));
        if (!value.IsNil()) {
          // Add qualifiers.
          Frame qualifiers = statement.GetFrame(s_qualifiers_);
          if (qualifiers.valid()) {
            Builder qualified(store);
            qualified.AddIs(value);
            for (const Slot &qproperty : qualifiers) {
              Array qstatement_list(store, qproperty.value);
              for (int j = 0; j < qstatement_list.length(); ++j) {
                Frame qstatement(store, qstatement_list.get(j));
                Handle qproperty = qstatement.GetHandle(s_property_);
                CHECK(!qproperty.IsNil());
                Frame qdatavalue = qstatement.GetFrame(s_datavalue_);
                if (qdatavalue.invalid()) continue;
                Object qvalue(store, ConvertValue(qdatavalue));
                if (!qvalue.IsNil()) {
                  qualified.Add(Property(store, qproperty), qvalue);
                }
              }
            }
            if (ranking != 1) qualified.Add(n_rank_, ranking);
            value = qualified.Create();
          } else if (ranking != 1) {
            // Add rank to primitive value.
            Builder qualified(store);
            qualified.AddIs(value);
            qualified.Add(n_rank_, ranking);
            value = qualified.Create();
          }

          // Add property with value.
          builder.Add(Property(store, property), value);
        }
      }
    }
  }

  // Add site links.
  Frame sitelinks = item.GetFrame(s_sitelinks_);
  if (sitelinks.valid()) {
    Builder sites(store);
    for (auto &it : languages_) {
      Frame site = sitelinks.GetFrame(it.second.wikisite);
      if (site.valid()) {
        string title = site.GetString(s_title_);
        if (!title.empty()) {
          sites.Add(it.second.language, title);
        }
      }
    }
    if (!sites.empty()) {
      builder.Add(n_wikipedia_, sites.Create());
    }

    // Add Wikidata QID from sitelinks.
    Frame wikisite = sitelinks.GetFrame(s_wikidatawiki_);
    if (wikisite.valid()) {
      string qid = wikisite.GetString(s_title_);
      if (!qid.empty()) {
        builder.Add(n_qid_, qid);
      }
    }
  }

  // Return SLING frame for item.
  return builder.Create();
}

Handle WikidataConverter::ConvertNumber(Text str) {
  // Try to convert as integer.
  int integer;
  if (safe_strto32(str.data(), str.size(), &integer) &&
      integer >= Handle::kMinInt && integer <= Handle::kMaxInt) {
    return Handle::Integer(integer);
  }

  // Try to convert as floating point number.
  float number;
  if (safe_strtof(str.str(), &number)) {
    return Handle::Float(number);
  }

  return Handle::nil();
}

Handle WikidataConverter::ConvertNumber(Store *store, Handle value) {
  if (value.IsNil()) return Handle::nil();
  if (value.IsInt() || value.IsFloat()) return value;
  if (store->IsString(value)) {
    Handle converted = ConvertNumber(store->GetString(value)->str());
    if (!converted.IsNil()) return converted;
  }
  return value;
}

Handle WikidataConverter::ConvertQuantity(const Frame &value) {
  // Get quantity amount, unit, and bounds.
  Store *store = value.store();
  Handle amount = ConvertNumber(store, value.GetHandle(s_amount_));
  Handle unit = value.GetHandle(s_unit_);
  Handle lower = ConvertNumber(store, value.GetHandle(s_lowerbound_));
  Handle upper = ConvertNumber(store, value.GetHandle(s_upperbound_));
  Handle precision = Handle::nil();

  // Convert unit.
  if (store->IsString(unit)) {
    Text unitstr = store->GetString(unit)->str();
    if (unitstr == "1") {
      unit = Handle::nil();
    } else {
      int pos = unitstr.rfind('/');
      if (pos != -1) {
        unitstr.remove_prefix(pos + 1);
        unit = store->Lookup(unitstr);
      } else {
        LOG(WARNING) << "Unknown unit: " << unitstr;
      }
    }
  }

  // Discard empty bounds.
  if (lower == amount && upper == amount) {
    lower = Handle::nil();
    upper = Handle::nil();
  } else if (amount.IsInt() && lower.IsInt() && upper.IsInt()) {
    int upper_precision = upper.AsInt() - amount.AsInt();
    int lower_precision = amount.AsInt() - lower.AsInt();
    if (upper_precision == 1 && lower_precision == 1) {
      lower = Handle::nil();
      upper = Handle::nil();
    } else if (upper_precision == lower_precision) {
      precision = Handle::Integer(upper_precision);
    }
  } else if (amount.IsFloat() && lower.IsFloat() && upper.IsFloat()) {
    float upper_precision = upper.AsFloat() - amount.AsFloat();
    float lower_precision = amount.AsFloat() - lower.AsFloat();
    float ratio = upper_precision / lower_precision;
    if (ratio > 0.999 && ratio < 1.001) {
      precision = Handle::Float(upper_precision);
    }
  }

  // Create quantity frame if needed.
  if (!unit.IsNil() || !lower.IsNil() || !upper.IsNil()) {
    Builder quantity(store);
    quantity.Add(n_amount_, amount);
    if (!unit.IsNil()) quantity.Add(n_unit_, unit);
    if (!precision.IsNil()) {
      quantity.Add(n_precision_, precision);
    } else {
      if (!lower.IsNil()) quantity.Add(n_low_, lower);
      if (!upper.IsNil()) quantity.Add(n_high_, upper);
    }
    amount = quantity.Create().handle();
  }

  return amount;
}

Handle WikidataConverter::ConvertText(const Frame &value) {
  // Get text and language. Only keep values for supported langages.
  Store *store = value.store();
  Handle text = value.GetHandle(s_text_);
  string langid = value.GetString(s_language_);
  auto f = language_map_.find(langid);
  if (f == language_map_.end()) return Handle::nil();
  if (f->second == n_lang_mul_ || f->second == n_lang_none_) {
    return text;
  } else {
    // Convert text to string qualified by language.
    return store->AllocateString(text, f->second);
  }
}

Handle WikidataConverter::ConvertTime(const Frame &value) {
  // Convert ISO date string and precision to date.
  Store *store = value.store();
  Object timestamp = value.Get(s_time_);
  Date date(timestamp);
  date.precision = date_precision[value.GetInt(s_precision_, 11)];

  // Convert timestamp to simplified integer or string format.
  Handle h = date.AsHandle(store);
  if (!h.IsNil()) return h;
  return timestamp.handle();
}

Handle WikidataConverter::ConvertEntity(const Frame &value) {
  String type = value.Get(s_entity_type_).AsString();
  Handle id = value.GetHandle(s_numeric_id_);
  if (type.equals("item")) {
    return Item(value.store(), id.AsInt());
  } else if (type.equals("lexeme")) {
    return Lexeme(value.store(), id.AsInt());
  } else if (type.equals("form")) {
    return Form(value.store(), value.GetText(s_id_));
  } else if (type.equals("sense")) {
    return Sense(value.store(), value.GetText(s_id_));
  } else if (type.equals("property")) {
    return Property(value.store(), id.AsInt());
  } else {
    LOG(FATAL) << "Unknown entity type: " << ToText(value);
    return Handle::nil();
  }
}

Handle WikidataConverter::ConvertCoordinate(const Frame &value) {
  // Get fields.
  Store *store = value.store();
  Handle lat = ConvertNumber(store, value.GetHandle(s_latitude_));
  Handle lng = ConvertNumber(store, value.GetHandle(s_longitude_));
  Handle prec = ConvertNumber(store, value.GetHandle(s_precision_));
  Handle globe = value.GetHandle(s_globe_);

  // Determine globe for coordinate, default to Earth.
  if (store->IsString(globe)) {
    Text globestr = store->GetString(globe)->str();
    int pos = globestr.rfind('/');
    if (pos != -1) {
      globestr.remove_prefix(pos + 1);
    }
    if (globestr == "Q2") {
      globe = Handle::nil();
    } else {
      globe = store->Lookup(globestr);
    }
  }

  // Cap precision.
  if (prec.IsFloat() && prec.AsFloat() < 0.0001) prec = Handle::nil();

  // Create geo frame.
  Builder geo(store);
  geo.AddIsA(n_geo_);
  geo.Add(n_lat_, lat);
  geo.Add(n_lng_, lng);
  if (!prec.IsNil()) geo.Add(n_precision_, prec);
  if (!globe.IsNil()) geo.Add(n_globe_, globe);

  return geo.Create().handle();
}

Handle WikidataConverter::ConvertValue(const Frame &datavalue) {
  String type = datavalue.Get(s_type_).AsString();
  if (type.IsNil()) return Handle::nil();
  if (type.equals("string")) {
    return datavalue.GetHandle(s_value_);
  } else {
    Frame value = datavalue.GetFrame(s_value_);
    if (value.invalid()) return Handle::nil();

    if (type.equals("wikibase-entityid")) {
      return ConvertEntity(value);
    } else if (type.equals("time")) {
      return ConvertTime(value);
    } else if (type.equals("quantity")) {
      return ConvertQuantity(value);
    } else if (type.equals("monolingualtext")) {
      return ConvertText(value);
    } else if (type.equals("globecoordinate")) {
      return ConvertCoordinate(value);
    } else {
      LOG(FATAL) << "Unknown data type: " << type.text();
      return Handle::nil();
    }
  }
}

}  // namespace nlp
}  // namespace sling

