// Copyright 2013 Google Inc. All Rights Reserved.
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

// This file contains functions for splitting strings.

#ifndef SLING_STRING_SPLIT_H_
#define SLING_STRING_SPLIT_H_

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/string/charset.h"
#include "sling/string/strip.h"
#include "sling/string/text.h"

namespace sling {

// This string splitting API consists of a Split() function and a handful of
// delimiter objects (more on delimiter objects below). The Split() function
// always takes two arguments: the text to be split and the delimiter on which
// to split the text. An optional third argument may also be given, which is a
// Predicate functor that will be used to filter the results, e.g., to skip
// empty strings (more on predicates below). The Split() function adapts the
// returned collection to the type specified by the caller.
//
// Example 1:
//   // Splits the given string on commas. Returns the results in a
//   // vector of strings.
//   std::vector<string> v = Split("a,b,c", ",");
//   assert(v.size() == 3);
//
// Example 2:
//   // By default, empty strings are *included* in the output. See the
//   // SkipEmpty predicate below to omit them.
//   std::vector<string> v = Split("a,b,,c", ",");
//   assert(v.size() == 4);  // "a", "b", "", "c"
//   v = Split("", ",");
//   assert(v.size() == 1);  // v contains a single ""
//
// Example 3:
//   // Splits the string as in the previous example, except that the results
//   // are returned as Text objects. Note that because we are storing
//   // the results within Text objects, we have to ensure that the input
//   // string outlives any results.
//   std::vector<Text> v = Split("a,b,c", ",");
//   assert(v.size() == 3);
//
// Example 4:
//   // Stores results in a set<string>.
//   std::set<string> a = Split("a,b,c,a,b,c", ",");
//   assert(a.size() == 3);
//
// Example 5:
//   // Stores results in a map. The map implementation assumes that the input
//   // is provided as a series of key/value pairs. For example, the 0th element
//   // resulting from the split will be stored as a key to the 1st element. If
//   // an odd number of elements are resolved, the last element is paired with
//   // a default-constructed value (e.g., empty string).
//   map<string, string> m = Split("a,b,c", ",");
//   assert(m.size() == 2);
//   assert(m["a"] == "b");
//   assert(m["c"] == "");  // last component value equals ""
//
// Example 6:
//   // Splits on the empty string, which results in each character of the input
//   // string becoming one element in the output collection.
//   std::vector<string> v = Split("abc", "");
//   assert(v.size() == 3);
//
// Example 7:
//   // Stores first two split strings as the members in an std::pair.
//   std::pair<string, string> p = Split("a,b,c", ",");
//   EXPECT_EQ("a", p.first);
//   EXPECT_EQ("b", p.second);
//   // "c" is omitted because std::pair can hold only two elements.
//
// As illustrated above, the Split() function adapts the returned collection to
// the type specified by the caller. The returned collections may contain
// string, Text, Cord, or any object that has a constructor (explicit or
// not) that takes a single Text argument. This pattern works for all
// standard STL containers including vector, list, deque, set, multiset, map,
// and multimap, non-standard containers including hash_set and hash_map, and
// even std::pair which is not actually a container.
//
// Splitting to std::pair is an interesting case because it can hold only two
// elements and is not a collection type. When splitting to an std::pair the
// first two split strings become the std::pair's .first and .second members
// respectively. The remaining split substrings are discarded. If there are less
// than two split substrings, the empty string is used for the corresponding
// std::pair member.
//
// The Split() function can be used multiple times to perform more
// complicated splitting logic, such as intelligently parsing key-value pairs.
// For example
//
//   // The input string "a=b=c,d=e,f=,g" becomes
//   // { "a" => "b=c", "d" => "e", "f" => "", "g" => "" }
//   std::map<string, string> m;
//   for (Text t : Split("a=b=c,d=e,f=,g", ",")) {
//     m.insertSplit(t, Limit("=", 1)));
//   }
//   EXPECT_EQ("b=c", m.find("a")->second);
//   EXPECT_EQ("e", m.find("d")->second);
//   EXPECT_EQ("", m.find("f")->second);
//   EXPECT_EQ("", m.find("g")->second);
//
// The above example stores the results in an std::map. But depending on your
// data requirements, you can just as easily store the results in an
// std::multimap or even a std::vector<std::pair<>>.
//
//
//                                  Delimiters
//
// The Split() function also takes a second argument that is a delimiter. This
// delimiter is actually an object that defines the boundaries between elements
// in the provided input. If a string (const char*, string, or Text) is
// passed in place of an explicit Delimiter object, the argument is implicitly
// converted to a Literal.
//
// With this split API comes the formal concept of a Delimiter (big D). A
// Delimiter is an object with a Find() function that knows how find the first
// occurrence of itself in a given Text. Models of the Delimiter concept
// represent specific kinds of delimiters, such as single characters,
// substrings, or even regular expressions.
//
// The following Delimiter objects are provided as part of the Split() API:
//
//   - Literal (default)
//   - AnyOf
//   - Limit
//   - FixedLength
//
// The following are examples of using some provided Delimiter objects:
//
// Example 1:
//   // Because a string literal is converted to a Literal,
//   // the following two splits are equivalent.
//   std::vector<string> v1 = Split("a,b,c", ",");                    // (1)
//   std::vector<string> v2 = Split("a,b,c", Literal(","));           // (2)
//
// Example 2:
//   // Splits on any of the characters specified in the delimiter string.
//   std::vector<string> v = Split("a,b;c-d", AnyOf(",;-"));
//   assert(v.size() == 4);
//
// Example 3:
//   // Uses the Limit meta-delimiter to limit the number of matches a delimiter
//   // can have. In this case, the delimiter of a Literal comma is limited to
//   // to matching at most one time. The last element in the returned
//   // collection will contain all unsplit pieces, which may contain instances
//   // of the delimiter.
//   std::vector<string> v = Split("a,b,c", Limit(",", 1));
//   assert(v.size() == 2);  // Limited to 1 delimiter; so two elements found
//   assert(v[0] == "a");
//   assert(v[1] == "b,c");
//
// Example 4:
//   // Splits into equal-length substrings.
//   std::vector<string> v = Split("12345", FixedLength(2));
//   assert(v.size() == 3);
//   assert(v[0] == "12");
//   assert(v[1] == "34");
//   assert(v[2] == "5");
//
//                                  Predicates
//
// Predicates can filter the results of a Split() operation by determining
// whether or not a resultant element is included in the result set. A predicate
// may be passed as an *optional* third argument to the Split() function.
//
// Predicates are unary functions (or functors) that take a single Text
// argument and return bool indicating whether the argument should be included
// (true) or excluded (false).
//
// One example where this is useful is when filtering out empty substrings. By
// default, empty substrings may be returned by Split(), which is similar to the
// way split functions work in other programming languages. For example:
//
//   // Empty strings *are* included in the returned collection.
//   std::vector<string> v = Split(",a,,b,", ",");
//   assert(v.size() ==  5);  // v[0] == "", v[1] == "a", v[2] == "", ...
//
// These empty strings can be filtered out of the results by simply passing the
// provided SkipEmpty predicate as the third argument to the Split() function.
// SkipEmpty does not consider a string containing all whitespace to be empty.
// For that behavior use the SkipWhitespace predicate. For example:
//
// Example 1:
//   // Uses SkipEmpty to omit empty strings. Strings containing whitespace are
//   // not empty and are therefore not skipped.
//   std::vector<string> v = Split(",a, ,b,", ",", SkipEmpty());
//   assert(v.size() == 3);
//   assert(v[0] == "a");
//   assert(v[1] == " ");  // <-- The whitespace makes the string not empty.
//   assert(v[2] == "b");
//
// Example 2:
//   // Uses SkipWhitespace to skip all strings that are either empty or contain
//   // only whitespace.
//   std::vector<string> v = Split(",a, ,b,", ",",  SkipWhitespace());
//   assert(v.size() == 2);
//   assert(v[0] == "a");
//   assert(v[1] == "b");
//

// A Delimiter object tells the splitter where a string should be broken. Some
// examples are breaking a string wherever a given character or substring is
// found, wherever a regular expression matches, or simply after a fixed length.
// All Delimiter objects must have the following member:
//
//   Text Find(Text text);
//
// This Find() member function should return a Text referring to the next
// occurrence of the represented delimiter, which is the location where the
// input string should be broken. The returned Text may be zero-length if
// the Delimiter does not represent a part of the string (e.g., a fixed-length
// delimiter). If no delimiter is found in the given text, a zero-length
// Text referring to text.end() should be returned (e.g.,
// Text(text.end(), 0)). It is important that the returned Text
// always be within the bounds of the Text given as an argument--it must
// not refer to a string that is physically located outside of the given string.
// The following example is a simple Delimiter object that is created with a
// single char and will look for that char in the text given to the Find()
// function:
//
//   struct SimpleDelimiter {
//     const char c_;
//     explicit SimpleDelimiter(char c) : c_(c) {}
//     Text Find(Text text) {
//       int pos = text.find(c_);
//       if (pos == Text::npos) return Text(text.end(), 0);
//       return Text(text, pos, 1);
//     }
//   };

// Represents a literal string delimiter. Examples:
//
//   vector<string> v = Split("a=>b=>c", Literal("=>"));
//   assert(v.size() == 3);
//   assert(v[0] == "a");
//   assert(v[1] == "b");
//   assert(v[2] == "c");
//
// The next example uses the empty string as a delimiter.
//
//   std::vector<string> v = Split("abc", Literal(""));
//   assert(v.size() == 3);
//   assert(v[0] == "a");
//   assert(v[1] == "b");
//   assert(v[2] == "c");

/// ======= Split() internals begin =======

// The default Predicate object, which doesn't filter out anything.
struct NoFilter {
  bool operator()(Text) {
    return true;
  }
};

// This class splits a string using the given delimiter, returning the split
// substrings via an iterator interface. An optional Predicate functor may be
// supplied, which will be used to filter the split strings: strings for which
// the predicate returns false will be skipped. A Predicate object is any
// functor that takes a Text and returns bool. By default, the NoFilter
// Predicate is used, which does not filter out anything.
//
// This class is NOT part of the public splitting API.
//
// Usage:
//
//   Literal d(",");
//   for (SplitIterator<Literal> it("a,b,c", d), end(d); it != end; ++it) {
//     Text substring = *it;
//     DoWork(substring);
//   }
//
// The explicit single-argument constructor is used to create an "end" iterator.
// The two-argument constructor is used to split the given text using the given
// delimiter.
template <typename Delimiter, typename Predicate = NoFilter>
class SplitIterator : public std::iterator<std::input_iterator_tag, Text> {
 public:
  // Two constructors for "end" iterators.
  explicit SplitIterator(Delimiter d)
      : delimiter_(d), predicate_(), is_end_(true) {}
  SplitIterator(Delimiter d, Predicate p)
      : delimiter_(d), predicate_(p), is_end_(true) {}

  // Two constructors taking the text to iterator.
  SplitIterator(Text text, Delimiter d)
      : text_(text), delimiter_(d), predicate_(), is_end_(false) {
    ++(*this);
  }
  SplitIterator(Text text, Delimiter d, Predicate p)
      : text_(text), delimiter_(d), predicate_(p), is_end_(false) {
    ++(*this);
  }

  Text operator*() { return curr_piece_; }
  Text *operator->() { return &curr_piece_; }

  SplitIterator &operator++() {
    do {
      if (text_.end() == curr_piece_.end()) {
        // Already consumed all of text_, so we're done.
        is_end_ = true;
        return *this;
      }
      Text found_delimiter = delimiter_.Find(text_);
      DCHECK(found_delimiter.data() != nullptr);
      DCHECK(text_.begin() <= found_delimiter.begin());
      DCHECK(found_delimiter.end() <= text_.end());
      // found_delimiter is allowed to be empty.
      // Sets curr_piece_ to all text up to but excluding the delimiter itself.
      // Sets text_ to remaining data after the delimiter.
      curr_piece_.set(text_.begin(), found_delimiter.begin() - text_.begin());
      text_.remove_prefix(found_delimiter.end() - text_.begin());
    } while (!predicate_(curr_piece_));
    return *this;
  }

  SplitIterator operator++(int) {
    SplitIterator old(*this);
    ++(*this);
    return old;
  }

  bool operator==(const SplitIterator &other) const {
    // Two "end" iterators are always equal. If the two iterators being compared
    // aren't both end iterators, then we fallback to comparing their fields.
    // Importantly, the text being split must be equal and the current piece
    // within the text being split must also be equal. The delimiter_ and
    // predicate_ fields need not be checked here because they're template
    // parameters that are already part of the SplitIterator's type.
    return (is_end_ && other.is_end_) ||
           (is_end_ == other.is_end_ &&
            text_ == other.text_ &&
            text_.data() == other.text_.data() &&
            curr_piece_ == other.curr_piece_ &&
            curr_piece_.data() == other.curr_piece_.data());
  }

  bool operator!=(const SplitIterator &other) const {
    return !(*this == other);
  }

 private:
  // The text being split. Modified as delimited pieces are consumed.
  Text text_;
  Delimiter delimiter_;
  Predicate predicate_;
  bool is_end_;
  // Holds the currently split piece of text. Will always refer to string data
  // within text_. This value is returned when the iterator is dereferenced.
  Text curr_piece_;
};

// Declares a functor that can convert a Text to another type. This works
// for any type that has a constructor (explicit or not) taking a single
// Text argument. A specialization exists for converting to string
// because the underlying data needs to be copied. In theory, these
// specializations could be extended to work with other types (e.g., int32), but
// then a solution for error reporting would need to be devised.
template <typename To>
struct TextTo {
  To operator()(Text from) const {
    return To(from);
  }
};

// Specialization for converting to string.
template <>
struct TextTo<string> {
  string operator()(Text from) const {
    return from.str();
  }
};

// Specialization for converting to *const* string.
template <>
struct TextTo<const string> {
  string operator()(Text from) const {
    return from.str();
  }
};

// IsNotInitializerList<T>::type exists iff T is not an initializer_list. More
// details below in Splitter<> where this is used.
template <typename T>
struct IsNotInitializerList {
  typedef void type;
};
template <typename T>
struct IsNotInitializerList<std::initializer_list<T> > {};

// This class implements the behavior of the split API by giving callers access
// to the underlying split substrings in various convenient ways, such as
// through iterators or implicit conversion functions. Do not construct this
// class directly, rather use the Split() function instead.
//
// Output containers can be collections of either Text or string objects.
// Text is more efficient because the underlying data will not need to be
// copied; the returned Texts will all refer to the data within the
// original input string. If a collection of string objects is used, then each
// substring will be copied.
//
// An optional Predicate functor may be supplied. This predicate will be used to
// filter the split strings: only strings for which the predicate returns true
// will be kept. A Predicate object is any unary functor that takes a
// Text and returns bool. By default, the NoFilter predicate is used,
// which does not filter out anything.
template <typename Delimiter, typename Predicate = NoFilter>
class Splitter {
 public:
  typedef SplitIterator<Delimiter, Predicate> Iterator;

  Splitter(Text text, Delimiter d)
      : begin_(text, d), end_(d) {}

  Splitter(Text text, Delimiter d, Predicate p)
      : begin_(text, d, p), end_(d, p) {}

  // Range functions that iterate the split substrings as Text objects.
  // These methods enable a Splitter to be used in a range-based for loop in
  // C++11, for example:
  //
  //   for (Text t : my_splitter) {
  //     DoWork(t);
  //   }
  const Iterator &begin() const { return begin_; }
  const Iterator &end() const { return end_; }

// All compiler flags are first saved with a diagnostic push and restored with a
// diagnostic pop below.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wc++98-compat"

  // Uses SFINAE to restrict conversion to container-like types (by testing for
  // the presence of a const_iterator member type) and also to disable
  // conversion to an initializer_list (which also has a const_iterator).
  // Otherwise, code compiled in C++11 will get an error due to ambiguous
  // conversion paths (in C++11 vector<T>::operator= is overloaded to take
  // either a vector<T> or an initializer_list<T>).
  template <typename Container,
            typename IsNotInitializerListChecker =
                typename IsNotInitializerList<Container>::type,
            typename ContainerChecker =
                typename Container::const_iterator>
  operator Container() {
    return SelectContainer<Container, is_map<Container>::value>()(this);
  }

// Restores diagnostic settings, i.e., removes the "ignore" on -Wpragmas and
// -Wc++98-compat.
#pragma GCC diagnostic pop

  template <typename First, typename Second>
  operator std::pair<First, Second>() {
    return ToPair<First, Second>();
  }

 private:
  // is_map<T>::value is true iff there exists a type T::mapped_type. This is
  // used to dispatch to one of the SelectContainer<> functors (below) from the
  // implicit conversion operator (above).
  template <typename T>
  struct is_map {
    typedef char small;
    typedef short big;

    template <typename U> static big test(typename U::mapped_type *);
    template <typename> static small test(...);
    static const bool value = (sizeof(test<T>(0)) == sizeof(big));
  };

  // Base template handles splitting to non-map containers
  template <typename Container, bool>
  struct SelectContainer {
    Container operator()(Splitter *splitter) const {
      return splitter->template ToContainer<Container>();
    }
  };

  // Partial template specialization for splitting to map-like containers.
  template <typename Container>
  struct SelectContainer<Container, true> {
    Container operator()(Splitter *splitter) const {
      return splitter->template ToMap<Container>();
    }
  };

  // Inserts split results into the container. To do this the results are first
  // stored in a vector<Text>. This is where the input text is actually
  // "parsed". The elements in this vector are then converted to the requested
  // type and inserted into the requested container. This is handled by the
  // ConvertContainer() function.
  //
  // The reason to use an intermediate vector of Text is so we can learn
  // the needed capacity of the output container. This is needed when the output
  // container is a vector<string> in which case resizes can be expensive due to
  // copying of the ::string objects.
  //
  // At some point in the future we might add a C++11 move constructor to
  // ::string, in which case the vector resizes are much less expensive and the
  // use of this intermediate vector "v" can be removed.
  template <typename Container>
  Container ToContainer() {
    std::vector<Text> v;
    for (Iterator it = begin(); it != end_; ++it) {
      v.push_back(*it);
    }
    Container c;
    ConvertContainer(v, &c);
    return c;
  }

  // The algorithm is to insert a new pair into the map for each even-numbered
  // item, with the even-numbered item as the key with a default-constructed
  // value. Each odd-numbered item will then be assigned to the last pair's
  // value.
  template <typename Map>
  Map ToMap() {
    typedef typename Map::key_type Key;
    typedef typename Map::mapped_type Data;
    Map m;
    TextTo<Key> key_converter;
    TextTo<Data> val_converter;
    typename Map::iterator curr_pair;
    bool is_even = true;
    for (Iterator it = begin(); it != end_; ++it) {
      if (is_even) {
        curr_pair = InsertInMap(std::make_pair(key_converter(*it), Data()), &m);
      } else {
        curr_pair->second = val_converter(*it);
      }
      is_even = !is_even;
    }
    return m;
  }

  // Returns a pair with its .first and .second members set to the first two
  // strings returned by the begin() iterator. Either/both of .first and .second
  // will be empty strings if the iterator doesn't have a corresponding value.
  template <typename First, typename Second>
  std::pair<First, Second> ToPair() {
    TextTo<First> first_converter;
    TextTo<Second> second_converter;
    Text first, second;
    Iterator it = begin();
    if (it != end()) {
      first = *it;
      if (++it != end()) {
        second = *it;
      }
    }
    return std::make_pair(first_converter(first), second_converter(second));
  }

  // Overloaded InsertInMap() function. The first overload is the commonly used
  // one for most map-like objects. The second overload is a special case for
  // multimap, because multimap's insert() member function directly returns an
  // iterator, rather than a pair<iterator, bool> like map's.
  template <typename Map>
  typename Map::iterator InsertInMap(
      const typename Map::value_type &value, Map *map) {
    return map->insert(value).first;
  }

  // InsertInMap overload for multimap.
  template <typename K, typename T, typename C, typename A>
  typename std::multimap<K, T, C, A>::iterator InsertInMap(
      const typename std::multimap<K, T, C, A>::value_type &value,
      typename std::multimap<K, T, C, A> *map) {
    return map->insert(value);
  }

  // Converts the container and elements to the specified types. This is the
  // generic case. There is an overload of this function to optimize for the
  // common case of a vector<string>.
  template <typename Container>
  void ConvertContainer(const std::vector<Text> &vin, Container *c) {
    typedef typename Container::value_type ToType;
    TextTo<ToType> converter;
    std::insert_iterator<Container> inserter(*c, c->begin());
    for (size_t i = 0; i < vin.size(); ++i) {
      *inserter++ = converter(vin[i]);
    }
  }

  // Overload of ConvertContainer() that is optimized for the common case of a
  // vector<string>. In this case, vector space is reserved, and a temp string
  // is lifted outside the loop and reused inside the loop to minimize
  // constructor calls and allocations.
  template <typename A>
  void ConvertContainer(const std::vector<Text> &vin,
                        std::vector<string, A> *vout) {
    vout->reserve(vin.size());
    string tmp;  // reused inside the loop
    for (size_t i = 0; i < vin.size(); ++i) {
      vin[i].CopyToString(&tmp);
      vout->push_back(tmp);
    }
  }

  const Iterator begin_;
  const Iterator end_;
};

/// ======= Split() internals end =======

// This is the *default* delimiter used if a literal string or string-like
// object is used where a Delimiter object is expected. For example, the
// following calls are equivalent.
//
//   std::vector<string> v = Split("a,b", ",");
//   std::vector<string> v = Split("a,b", Literal(","));
//
class Literal {
 public:
  explicit Literal(Text t);
  Text Find(Text text) const;

 private:
  const string delimiter_;
};

// A traits-like meta function for selecting the default Delimiter object type
// for a particular Delimiter type. The base case simply exposes type Delimiter
// itself as the delimiter's Type. However, there are specializations for
// string-like objects that map them to the Literal delimiter object. This
// allows functions like Split() and Limit() to accept string-like objects
// (e.g., ",") as delimiter arguments but they will be treated as if a Literal
// delimiter was given.
template <typename Delimiter>
struct SelectDelimiter {
  typedef Delimiter Type;
};
template <> struct SelectDelimiter<const char *> { typedef Literal Type; };
template <> struct SelectDelimiter<Text> { typedef Literal Type; };
template <> struct SelectDelimiter<string> { typedef Literal Type; };

// Represents a delimiter that will match any of the given byte-sized
// characters. AnyOf is similar to Literal, except that AnyOf uses
// Text::find_first_of() and Literal uses Text::find(). AnyOf
// examples:
//
//   std::vector<string> v = Split("a,b=c", AnyOf(",="));
//
//   assert(v.size() == 3);
//   assert(v[0] == "a");
//   assert(v[1] == "b");
//   assert(v[2] == "c");
//
// If AnyOf is given the empty string, it behaves exactly like Literal and
// matches each individual character in the input string.
//
// Note: The string passed to AnyOf is assumed to be a string of single-byte
// ASCII characters. AnyOf does not work with multi-byte characters.
class AnyOf {
 public:
  explicit AnyOf(Text t);
  Text Find(Text text) const;

 private:
  const string delimiters_;
};

// A delimiter for splitting into equal-length strings. The length argument to
// the constructor must be greater than 0. This delimiter works with ascii
// string data, but does not work with variable-width encodings, such as UTF-8.
// Examples:
//
//   std::vector<string> v = Split("123456789", FixedLength(3));
//   assert(v.size() == 3);
//   assert(v[0] == "123");
//   assert(v[1] == "456");
//   assert(v[2] == "789");
//
// Note that the string does not have to be a multiple of the fixed split
// length. In such a case, the last substring will be shorter.
//
//   std::vector<string> v = Split("12345", FixedLength(2));
//   assert(v.size() == 3);
//   assert(v[0] == "12");
//   assert(v[1] == "34");
//   assert(v[2] == "5");
//
class FixedLength {
 public:
  explicit FixedLength(int length);
  Text Find(Text text) const;

 private:
  const int length_;
};

// Wraps another delimiter and sets a max number of matches for that delimiter.
// Create LimitImpls using the Limit() function. Example:
//
//   std::vector<string> v = Split("a,b,c,d", Limit(",", 2));
//
//   assert(v.size() == 3);  // Split on 2 commas, giving a vector with 3 items
//   assert(v[0] == "a");
//   assert(v[1] == "b");
//   assert(v[2] == "c,d");
//
template <typename Delimiter>
class LimitImpl {
 public:
  LimitImpl(Delimiter delimiter, int limit)
      : delimiter_(delimiter), limit_(limit), count_(0) {}
  Text Find(Text text) {
    if (count_++ == limit_) {
      return Text(text.end(), 0);  // no more matches
    }
    return delimiter_.Find(text);
  }

 private:
  Delimiter delimiter_;
  const int limit_;
  int count_;
};

// Limit() function to create LimitImpl<> objects.
template <typename Delimiter>
inline LimitImpl<typename SelectDelimiter<Delimiter>::Type>
  Limit(Delimiter delim, int limit) {
  typedef typename SelectDelimiter<Delimiter>::Type DelimiterType;
  return LimitImpl<DelimiterType>(DelimiterType(delim), limit);
}

//
// Predicates are functors that return bool indicating whether the given
// Text should be included in the split output. If the predicate returns
// false then the string will be excluded from the output from Split().
//

// Always returns true, indicating that all strings--including empty
// strings--should be included in the split output. This predicate is not
// strictly needed because this is the default behavior of the Split()
// function. But it might be useful at some call sites to make the intent
// explicit.
//
// std::vector<string> v = Split(" a , ,,b,", ",", AllowEmpty());
// EXPECT_THAT(v, ElementsAre(" a ", " ", "", "b", ""));
struct AllowEmpty {
  bool operator()(Text t) const {
    return true;
  }
};

// Returns false if the given Text is empty, indicating that the Split() API
// should omit the empty string.
//
// std::vector<string> v = Split(" a , ,,b,", ",", SkipEmpty());
// EXPECT_THAT(v, ElementsAre(" a ", " ", "b"));
struct SkipEmpty {
  bool operator()(Text t) const {
    return !t.empty();
  }
};

// Returns false if the given Text is empty or contains only whitespace,
// indicating that the Split() API should omit the string.
//
// std::vector<string> v = Split(" a , ,,b,", ",", SkipWhitespace());
// EXPECT_THAT(v, ElementsAre(" a ", "b"));
struct SkipWhitespace {
  bool operator()(Text t) const {
    StripWhiteSpace(&t);
    return !t.empty();
  }
};

// Definitions of the main Split() function. The use of SelectDelimiter<> allows
// these functions to be called with a Delimiter template parameter that is an
// actual Delimiter object (e.g., Literal or AnyOf), OR called with a
// string-like delimiter argument, (e.g., ","), in which case the delimiter used
// will default to Literal.
template <typename Delimiter>
inline Splitter<typename SelectDelimiter<Delimiter>::Type>
Split(Text text, Delimiter d) {
  typedef typename SelectDelimiter<Delimiter>::Type DelimiterType;
  return Splitter<DelimiterType>(text, DelimiterType(d));
}

template <typename Delimiter, typename Predicate>
inline Splitter<typename SelectDelimiter<Delimiter>::Type, Predicate>
Split(Text text, Delimiter d, Predicate p) {
  typedef typename SelectDelimiter<Delimiter>::Type DelimiterType;
  return Splitter<DelimiterType, Predicate>(text, DelimiterType(d), p);
}

}  // namespace sling

#endif  // SLING_STRING_SPLIT_H_

