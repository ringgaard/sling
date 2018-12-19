// Copyright 2018 Google Inc.
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

#include "sling/nlp/wiki/wiki-annotator.h"

#include "sling/nlp/document/fingerprinter.h"
#include "sling/nlp/kb/calendar.h"
#include "sling/string/numbers.h"

namespace sling {
namespace nlp {

// Template macro that expands to a fixed text.
class TextTemplate : public WikiMacro {
 public:
  void Init(const Frame &config) override {
    text_ = config.GetString("text");
  }

  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    annotator->Content(text_);
  }

 private:
  string text_;
};

REGISTER_WIKI_MACRO("text", TextTemplate);

// Template macro for expanding fractions.
class FracTemplate : public WikiMacro {
 public:
  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    switch (templ.NumArgs()) {
      case 0:
        annotator->Content("/");
        break;
      case 1:
        annotator->Content("1/");
        annotator->Content(templ.GetValue(1));
        break;
      case 2:
        annotator->Content(templ.GetValue(1));
        annotator->Content("/");
        annotator->Content(templ.GetValue(2));
        break;
      case 3:
        annotator->Content(templ.GetValue(1));
        annotator->Content("&nbsp;");
        annotator->Content(templ.GetValue(2));
        annotator->Content("/");
        annotator->Content(templ.GetValue(3));
        break;
    }
  }
};

REGISTER_WIKI_MACRO("frac", FracTemplate);

// Template macro for expanding arguments with open, close, and delimiter
// text.
class TagTemplate : public WikiMacro {
 public:
  void Init(const Frame &config) override {
    arg_ = config.GetString("arg");
    argnum_ = config.GetInt("argnum", 0);
    argname_ = config.GetString("argname");
    open_ = config.GetString("open");
    close_ = config.GetString("close");
    delimiter_ = config.GetString("delimiter");
  }

  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    annotator->Content(open_);
    if (argnum_ == -1 && argname_.empty()) {
      for (int i = 1; i < templ.NumArgs() + 1; ++i) {
        if (i != 1) annotator->Content(delimiter_);
        const Node *arg = templ.GetArgument(i);
        if (arg != nullptr) {
          templ.extractor()->ExtractNode(*arg);
        }
      }
    } else {
      const Node *content = templ.GetArgument(argname_, argnum_);
      if (content != nullptr) {
        templ.extractor()->ExtractNode(*content);
      }
    }
    annotator->Content(close_);
  }

 private:
  string arg_;
  int argnum_;
  string argname_;
  string open_;
  string close_;
  string delimiter_;
};

REGISTER_WIKI_MACRO("tag", TagTemplate);

// Template macro for defintions and abbreviations.
class DefineTemplate : public WikiMacro {
 public:
  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    const Node *abbreviation = templ.GetArgument(1);
    if (abbreviation != nullptr) {
      templ.extractor()->ExtractNode(*abbreviation);
    }
    const Node *meaning = templ.GetArgument(2);
    if (meaning != nullptr) {
      annotator->Content(" (");
      templ.extractor()->ExtractNode(*meaning);
      annotator->Content(")");
    }
  }
};

REGISTER_WIKI_MACRO("define", DefineTemplate);

// Template macro for expanding dates with annotations.
class DateTemplate : public WikiMacro {
 public:
  void Init(const Frame &config) override {
    // Get the date format from the configuration.
    Store *store = config.store();
    Frame format = config.GetFrame("format");
    if (format.valid()) {
      // Get month names.
      int prefix = format.GetInt("/w/month_abbrev");
      Array months = format.Get("/w/month_names").AsArray();
      if (months.valid()) {
        for (int i = 0; i < months.length(); ++i) {
          String month(store, months.get(i));
          CHECK(month.valid());
          string name = month.value();
          month_names_.push_back(name);

          month_dictionary_[Fingerprint(name)] = i + 1;
          if (prefix > 0) {
            string abbrev = name.substr(0, prefix);
            month_dictionary_[Fingerprint(abbrev)] = i + 1;
          }
        }
      }

      // Get numeric and textual input date formats.
      Handle n_numeric = store->Lookup("/w/numeric_date_format");
      Handle n_textual = store->Lookup("/w/text_date_format");
      for (const Slot &s : format) {
        if (s.name == n_numeric) {
          String fmt(store, s.value);
          CHECK(fmt.valid());
          numeric_formats_.push_back(fmt.value());
        } else if (s.name == n_textual) {
          String fmt(store, s.value);
          CHECK(fmt.valid());
          text_formats_.push_back(fmt.value());
        }
      }

      // Get output format for dates.
      day_format_ = format.GetString("/w/day_output_format");
      month_format_ = format.GetString("/w/month_output_format");
      year_format_ = format.GetString("/w/year_output_format");
    }

    // Get date argument configuration.
    date_argnum_ = config.GetInt("full", -1);
    year_argnum_ = config.GetInt("year", -1);
    month_argnum_ = config.GetInt("month", -1);
    day_argnum_ = config.GetInt("day", -1);
    qualification_argnum_ = config.GetInt("qual", -1);
    reverse_args_ = config.GetBool("reverse");

    date_argname_ = config.GetString("fulln");
    year_argname_ = config.GetString("yearn");
    month_argname_ = config.GetString("monthn");
    day_argname_ = config.GetString("dayn");

    // Get prefix and postfix.
    prefix_ = config.GetString("pre");
    postfix_ = config.GetString("post");
  }

  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    // Parse input date.
    Date date;
    if (!GetDate(templ, &date)) {
      templ.Extract(1);
      return;
    }

    // Output date.
    annotator->Content(prefix_);
    if (date.precision != Date::NONE) {
      // Output formatted date.
      int begin = annotator->position();
      annotator->Content(DateAsText(date));
      int end = annotator->position();

      // Create date annotation.
      Builder b(annotator->store());
      b.AddIsA("/w/time");
      b.AddIs(date.AsHandle(annotator->store()));
      annotator->AddMention(begin, end, b.Create().handle());
    } else {
      // Unable to parse date, output verbatim.
      templ.Extract(1);
    }
    if (qualification_argnum_ != -1) {
      templ.Extract(qualification_argnum_);
    }
    annotator->Content(postfix_);
  }

 protected:
  // Get date component.
  const Node *DateComponent(const WikiTemplate &templ, Text name, int index) {
    if (reverse_args_) {
      // In reverse argument mode you have {{date|y}}, {{date|m|y}}, or
      // {{date|d|m|y}}, compared to normal mode, where the order is
      // {{date|y}}, {{date|y|m}}, or {{date|y|m|d}}.
      int numargs = templ.NumArgs();
      if (numargs > 3) numargs = 3;
      index = numargs - index + 1;
    }
    return templ.GetArgument(name, index);
  }

  // Get date from template argument(s).
  bool GetDate(const WikiTemplate &templ, Date *date) {
    // Parse full date argument.
    const Node *full_arg = templ.GetArgument(date_argname_, date_argnum_);
    if (full_arg != nullptr) {
      string fulldate = templ.GetValue(full_arg);
      if (!ParseDate(fulldate, date)) return false;
    }

    // Parse year argument.
    const Node *year_arg = DateComponent(templ, year_argname_, year_argnum_);
    if (year_arg != nullptr) {
      int y = templ.GetNumber(year_arg);
      if (y != -1) {
        date->year = y;
      } else {
        return false;
      }
    }

    // Parse month argument.
    const Node *month_arg = DateComponent(templ, month_argname_, month_argnum_);
    if (month_arg != nullptr) {
      int m = templ.GetNumber(month_arg);
      if (m != -1) {
        date->month = m;
      } else {
        uint64 fp = Fingerprint(templ.GetValue(month_arg));
        auto f = month_dictionary_.find(fp);
        if (f != month_dictionary_.end()) {
          date->month = f->second;
        } else {
          return false;
        }
      }
    }

    // Parse day argument.
    const Node *day_arg = DateComponent(templ, day_argname_, day_argnum_);
    if (day_arg != nullptr) {
      int d = templ.GetNumber(day_arg);
      if (d != -1) {
        date->day = d;
      } else {
        return false;
      }
    }

    // Check BCE.
    if (templ.GetArgument("BCE") || templ.GetArgument("BC")) {
      date->year = -date->year;
    }

    // Determine precision.
    if (date->year != 0) {
      date->precision = Date::YEAR;
      if (date->month != 0) {
        date->precision = Date::MONTH;
        if (date->day != 0) {
          date->precision = Date::DAY;
        }
      }
    }
    return true;
  }

  // Try to parse input date.
  bool ParseDate(Text str, Date *date) {
    // Determine if date is numeric or text.
    bool numeric = true;
    for (char c : str) {
      if (!IsDigit(c) && !IsDelimiter(c)) {
        numeric = false;
        break;
      }
    }

    // Parse date into year, month, and date component.
    bool valid = false;
    int len = str.size();
    int y, m, d, ys;
    if (numeric) {
      // Try to parse numeric date using each of the numeric date formats.
      for (const string &fmt : numeric_formats_) {
        if (len != fmt.size()) continue;
        y = m = d = ys = 0;
        valid = true;
        for (int i = 0; i < fmt.size(); ++i) {
          char c = str[i];
          char f = fmt[i];
          if (IsDigit(c)) {
            switch (f) {
              case 'Y': y = y * 10 + Digit(c); ys++; break;
              case 'M': m = m * 10 + Digit(c); break;
              case 'D': d = d * 10 + Digit(c); break;
              default: valid = false;
            }
          } else if (c != f) {
            valid = false;
          }
          if (!valid) break;
        }
        if (valid) break;
      }
    } else {
      // Try to parse text date format.
      for (const string &fmt : text_formats_) {
        y = m = d = ys = 0;
        valid = true;
        int j = 0;
        for (int i = 0; i < fmt.size(); ++i) {
          if (j >= len) valid = false;
          if (!valid) break;

          switch (fmt[i]) {
            case 'Y':
              // Parse sequence of digits as year.
              if (IsDigit(str[j])) {
                while (j < len && IsDigit(str[j])) {
                  y = y * 10 + Digit(str[j++]);
                  ys++;
                }
              } else {
                valid = false;
              }
              break;

            case 'M': {
              // Parse next token as month name.
              int k = j;
              while (k < len && !IsMonthBreak(str[k])) k++;
              auto f = month_dictionary_.find(Fingerprint(Text(str, j, k - j)));
              if (f != month_dictionary_.end()) {
                m = f->second;
                j = k;
              } else {
                valid = false;
              }
              break;
            }

            case 'D':
              // Parse sequence of digits as day.
              if (IsDigit(str[j])) {
                while (j < len && IsDigit(str[j])) {
                  d = d * 10 + Digit(str[j++]);
                }
              } else {
                valid = false;
              }
              break;

            case ' ':
              // Skip sequence of white space.
              if (str[j] != ' ') {
                valid = false;
              } else {
                while (j < len && str[j] == ' ') j++;
              }
              break;

            default:
              // Literal match.
              valid = (fmt[i] == str[j++]);
          }
        }
        if (valid) break;
      }
    }

    // Return parsed date if it is valid.
    if (valid) {
      // Dates with two-digit years will have the years from 1970 to 2069.
      if (ys == 2) {
        if (y < 70) {
          y += 2000;
        } else {
          y += 1900;
        }
      }
      date->year = y;
      date->month = m;
      date->day = d;
    }
    return valid;
  }

  // Convert date to text.
  string DateAsText(const Date &date) {
    Text format;
    switch (date.precision) {
      case Date::YEAR: format = year_format_; break;
      case Date::MONTH: format = month_format_; break;
      case Date::DAY: format = day_format_; break;
      default: return date.AsString();
    }
    string str;
    char buf[8];
    for (char c : format) {
      switch (c) {
        case 'Y':
          if (date.year != 0) {
            sprintf(buf, "%04d", date.year);
            str.append(buf);
          }
          break;

        case 'M':
          if (date.month != 0) {
            if (date.month > 0 && date.month <= month_names_.size()) {
              str.append(month_names_[date.month - 1]);
            } else {
              sprintf(buf, "?%02d?", date.month);
              str.append(buf);
            }
          }
          break;

        case 'D':
          if (date.day != 0) {
            sprintf(buf, "%d", date.day);
            str.append(buf);
          }
          break;

        default:
          str.push_back(c);
      }
    }
    return str;
  }

  // Return fingerprint for token.
  static uint64 Fingerprint(Text token) {
    return Fingerprinter::Fingerprint(token, NORMALIZE_CASE);
  }

  // Check if character is a digit.
  static bool IsDigit(char c) {
    return c >= '0' && c <= '9';
  }

  // Check if character is a date delimiter.
  static bool IsDelimiter(char c) {
    return c == '-' || c == '/' || c == '.';
  }

  // Check if character is a month name break.
  static bool IsMonthBreak(char c) {
    return c == ' ' || IsDigit(c) || IsDelimiter(c);
  }

  // Return digit value.
  static int Digit(char c) {
    DCHECK(IsDigit(c));
    return c - '0';
  }

 private:
  // Month names for generating dates.
  std::vector<string> month_names_;

  // Month name (and abbreviation) fingerprints for parsing.
  std::unordered_map<uint64, int> month_dictionary_;

  // Numeric date input formats, e.g. 'YYYY-MM-DD'.
  std::vector<string> numeric_formats_;

  // Text date input formats, e.g. 'M D, Y'.
  std::vector<string> text_formats_;

  // Output formats for dates.
  string day_format_;
  string month_format_;
  string year_format_;

  // Date input argument.
  int date_argnum_ = -1;
  int year_argnum_ = -1;
  int month_argnum_ = -1;
  int day_argnum_ = -1;
  int qualification_argnum_ = -1;
  bool reverse_args_ = false;

  string date_argname_;
  string year_argname_;
  string month_argname_;
  string day_argname_;

  // Prefix and postfix text.
  string prefix_;
  string postfix_;
};

REGISTER_WIKI_MACRO("date", DateTemplate);

// Template macro for years.
class YearTemplate : public WikiMacro {
 public:
  void Init(const Frame &config) override {
    bce_ = config.GetBool("bce");
    range_ = config.GetBool("range");
    bcarg_ = config.GetInt("bc");
    prefix_ = config.GetString("pre");
    postfix_ = config.GetString("post");
  }

  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    GenerateYear(templ, annotator, 1);
    if (range_ && templ.GetArgument(2)) {
      annotator->Content(" – ");
      GenerateYear(templ, annotator, 2);
    }
  }

  void GenerateYear(const WikiTemplate &templ, WikiAnnotator *annotator,
                    int argnum) {
    annotator->Content(prefix_);
    int year = templ.GetNumber(argnum);
    if (year != -1) {
      Date date(bce_ ? -year : year, 0, 0, Date::YEAR);
      string bc;
      if (bcarg_ != -1) {
        bc = templ.GetValue(bcarg_);
        if (!bc.empty()) date.year = -date.year;
      }

      int begin = annotator->position();
      templ.Extract(argnum);
      annotator->Content(bc);
      annotator->Content(postfix_);
      int end = annotator->position();

      Builder b(annotator->store());
      b.AddIsA("/w/time");
      b.AddIs(date.AsHandle(annotator->store()));
      annotator->AddMention(begin, end, b.Create().handle());
    } else {
      templ.Extract(argnum);
    }
  }

 private:
  bool bce_;
  bool range_;
  string prefix_;
  string postfix_;
  int bcarg_ = -1;
};

REGISTER_WIKI_MACRO("year", YearTemplate);

// Template macro for measures.
class MeasureTemplate : public WikiMacro {
 public:
  void Init(const Frame &config) override {
    Store *store = config.store();
    Frame units = config.GetFrame("units");
    if (units.valid()) {
      for (const Slot &s : units) {
        String name(store, s.name);
        Frame info(store, s.value);
        if (!name.valid() || !info.valid()) break;
        Unit &unit = units_[name.value()];
        unit.item = info.GetHandle("/w/unit");
        unit.factor = info.GetFloat("/w/amount");
      }
    }
    value_argnum_ = config.GetInt("value", 1);
    unit_argnum_ = config.GetInt("unit", 2);
  }

  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    // Get value and unit.
    string value_text = templ.GetValue(value_argnum_);
    string unit_text = templ.GetValue(unit_argnum_);

    // Output measure.
    int begin = annotator->position();
    annotator->Content(value_text);
    annotator->Content(" ");
    annotator->Content(unit_text);
    int end = annotator->position();

    // Parse value.
    float amount;
    if (!safe_strtof(value_text, &amount)) return;

    // Look up unit.
    auto f = units_.find(unit_text);
    if (f == units_.end()) return;

    // Scale value.
    const Unit &unit = f->second;
    if (unit.factor != 0) amount *= unit.factor;

    // Add measure annotation.
    Builder b(annotator->store());
    b.Add("/w/amount", amount);
    b.Add("/w/unit", unit.item);
    annotator->AddMention(begin, end, b.Create().handle());
  }

 private:
  struct Unit {
    Handle item;
    float factor;
  };

  // Units with scaling factors.
  std::unordered_map<string, Unit> units_;

  // Value and unit arguments.
  int value_argnum_;
  int unit_argnum_;
};

REGISTER_WIKI_MACRO("measure", MeasureTemplate);

}  // namespace nlp
}  // namespace sling

