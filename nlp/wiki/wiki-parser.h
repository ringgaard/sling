#ifndef NLP_WIKI_WIKI_PARSER_H_
#define NLP_WIKI_WIKI_PARSER_H_

#include <string>
#include <vector>

#include "base/types.h"
#include "string/ctype.h"
#include "string/text.h"

namespace sling {
namespace nlp {

// Parse wiki text and convert to abstract syntax tree (AST). The plain text as
// well as structured information can then be extracted from the AST.
class WikiParser {
 public:
  // Wiki AST node type.
  enum Type {
    DOCUMENT,    // top-level node
    ARG,         // argument for template, link, etc.
    ATTR,        // attribute

    // Inline elements.
    TEXT,        // plain text
    FONT,        // ''italics'', '''bold''', and '''''both'''''
    TEMPLATE,    // {{name | args... }}
    LINK,        // [[link | text]]
    IMAGE,       // [[File:link | text]]
    CATEGORY,    // [[Category:...]]
    URL,         // [url text]
    COMMENT,     // <!-- comment -->
    TAG,         // <tag/>
    BTAG,         // <tag attr=''>
    ETAG,         // </tag>
    MATH,         // <math>...</math>

    // Elements that must be at the start of a line.
    HEADING,     // =h1= ==h2== ===h3===
    INDENT,      // : :: :::
    UL,          // * ** *** ****
    OL,          // # ## ### ###
    HR,          // ----
    TERM,        // ; term : definition
    SWITCH,      // __SWITCH__

    // Tables.
    TABLE,       // {| |}
    CAPTION,     // |+
    ROW,         // |-
    HEADER,      // ! !!
    CELL,        // | ||
    BREAK,       // |- (outside table)
  };

  // Wiki AST node.
  struct Node {
    Node(Type t, int p) : type(t), param(p) {}

    Text text() const { return Text(begin, end - begin); }
    Text name() const { return Text(name_begin, name_end - name_begin); }
    bool anchored() const { return text_begin != text_end; }

    Type type;
    int param;

    int first_child = -1;
    int last_child = -1;
    int prev_sibling = -1;
    int next_sibling = -1;

    const char *begin = nullptr;
    const char *end = nullptr;
    const char *name_begin = nullptr;
    const char *name_end = nullptr;

    int text_begin = -1;
    int text_end = -1;
  };

  // Initialize parser with wiki text.
  WikiParser(const char *wikitext);

  // Parse wiki text.
  void Parse();

  // Extract plain text and information from AST.
  void Extract() { Extract(0); }

  // Print AST node and its children.
  void PrintAST(int index, int indent);

  // Return extracted text.
  const string text() const { return text_; }

  // Return the number of AST nodes.
  int num_ast_nodes() const { return nodes_.size(); }

  // Return nodes.
  const std::vector<Node> &nodes() const { return nodes_; }

  // Return node.
  const Node &node(int index) const { return nodes_[index]; }

 private:
  // Parse input until stop mark is found.
  void ParseUntil(char stop);

  // Parse newline.
  void ParseNewLine();

  // Parse font change.
  void ParseFont();

  // Parse template start.
  void ParseTemplateBegin();

  // Parse template end.
  void ParseTemplateEnd();

  // Parse argument separator.
  void ParseArgument();

  // Parse link start.
  void ParseLinkBegin();

  // Parse link end.
  void ParseLinkEnd();

  // Parse url.
  void ParseUrl();

  // Parse tag (<...>) or comment (<!-- ... -->).
  void ParseTag();

  // Parse heading start.
  void ParseHeadingBegin();

  // Parse heading end.
  void ParseHeadingEnd();

  // Parse indent or term definition (:).
  void ParseIndent();

  // Parse list item (* or =).
  void ParseListItem();

  // Parse term (;).
  void ParseTerm();

  // Parse horizontal rule (----).
  void ParseHorizontalRule();

  // Parse behavior switch (__SWITCH__).
  void ParseSwitch();

  // Parse table start ({|).
  void ParseTableBegin();

  // Parse table caption (|+).
  void ParseTableCaption();

  // Parse table row (|-).
  void ParseTableRow();

  // Parse table header cell (! or !!).
  void ParseHeaderCell(bool first);

  // Parse table cell (| or ||).
  void ParseTableCell(bool first);

  // Parse table end (|}).
  void ParseTableEnd();

  // Parse break (|- outside table).
  void ParseBreak();

  // Parse HTML/XML attribute list. Return true if any attributes found.
  bool ParseAttributes(const char *delimiters);

  // Extract text from AST node.
  void Extract(int index);

  // Extract link.
  void ExtractLink(int index);

  // Extract URL.
  void ExtractUrl(int index);

  // Extract heading.
  void ExtractHeading(int index);

  // Extract font.
  void ExtractFont(int index);

  // Extract list item.
  void ExtractListItem(int index);

  // Extract table.
  void ExtractTable(int index);

  // Extract table  row.
  void ExtractTableRow(int index);

  // Extract text from AST node children.
  void ExtractChildren(int index);

  // Add child node to current AST node.
  int Add(Type type, int param = 0);

  // Set node name. This trims whitespace from the name.
  void SetName(int index, const char *begin, const char *end);

  // End current text block.
  void EndText();

  // Push new node top stack.
  int Push(Type type, int param = 0);

  // Pop top node from stack.
  int Pop();

  // Unwind stack.
  int UnwindUntil(int type);

  // Check if inside one element rather than another.
  bool Inside(Type type, Type another = DOCUMENT);
  bool Inside(Type type, Type another1, Type another2);

  // Check if current input matches string.
  bool Matches(const char *prefix);

  // Skip whitespace.
  void SkipWhitespace();

  // Append text to text buffer.
  void Append(const char *begin, const char *end);
  void Append(const char *str) { Append(str, str + strlen(str)); }
  void Append(Text str) { Append(str.data(), str.data() +str.size()); }
  void Append(const Node &node) { Append(node.begin, node.end); }

  // Check if a character is an XML name character.
  static bool IsNameChar(int c) {
    return ascii_isalnum(c) || c == ':' || c == '-' || c == '_' || c == '.';
  }

  // Current position in text.
  const char *ptr_ = nullptr;

  // Start of current text node.
  const char *txt_ = nullptr;

  // List of AST nodes on page.
  std::vector<Node> nodes_;

  // Current nesting of AST nodes. The stack contains indices into the AST
  // node array.
  std::vector<int> stack_;

  // Extracted text.
  string text_;

  // Number of pending line breaks.
  int line_breaks_ = 0;

  // Current font.
  int font_ = 0;

 public:
  // Special template types.
  enum Special {
    TMPL_NORMAL,

    TMPL_DEFAULTSORT,
    TMPL_DISPLAYTITLE,
    TMPL_PAGENAME,
    TMPL_PAGENAMEE,
    TMPL_BASEPAGENAME,
    TMPL_BASEPAGENAMEE,
    TMPL_SUBPAGENAME,
    TMPL_SUBPAGENAMEE,
    TMPL_NAMESPACE,
    TMPL_NAMESPACEE,
    TMPL_FULLPAGENAME,
    TMPL_FULLPAGENAMEE,
    TMPL_TALKSPACE,
    TMPL_TALKSPACEE,
    TMPL_SUBJECTSPACE,
    TMPL_SUBJECTSPACEE,
    TMPL_ARTICLESPACE,
    TMPL_ARTICLESPACEE,
    TMPL_TALKPAGENAME,
    TMPL_TALKPAGENAMEE,
    TMPL_SUBJECTPAGENAME,
    TMPL_SUBJECTPAGENAMEE,
    TMPL_ARTICLEPAGENAME,
    TMPL_ARTICLEPAGENAMEE,
    TMPL_REVISIONID,
    TMPL_REVISIONDAY,
    TMPL_REVISIONDAY2,
    TMPL_REVISIONMONTH,
    TMPL_REVISIONYEAR,
    TMPL_REVISIONTIMESTAMP,
    TMPL_SITENAME,
    TMPL_SERVER,
    TMPL_SCRIPTPATH,
    TMPL_SERVERNAME,

    TMPL_CONTENTLANGUAGE,
    TMPL_DIRECTIONMARK,
    TMPL_CURRENTYEAR,

    TMPL_CURRENTMONTH,
    TMPL_CURRENTMONTH1,
    TMPL_CURRENTMONTHNAME,
    TMPL_CURRENTMONTHABBREV,
    TMPL_CURRENTDAY,
    TMPL_CURRENTDAY2,
    TMPL_CURRENTDOW,
    TMPL_CURRENTDAYNAME,
    TMPL_CURRENTTIME,
    TMPL_CURRENTHOUR,
    TMPL_CURRENTWEEK,
    TMPL_CURRENTTIMESTAMP,
    TMPL_CURRENTMONTHNAMEGEN,
    TMPL_LOCALYEAR,
    TMPL_LOCALMONTH,
    TMPL_LOCALMONTH1,
    TMPL_LOCALMONTHNAME,
    TMPL_LOCALMONTHNAMEGEN,
    TMPL_LOCALMONTHABBREV,
    TMPL_LOCALDAY,
    TMPL_LOCALDAY2,
    TMPL_LOCALDOW,
    TMPL_LOCALDAYNAME,
    TMPL_LOCALTIME,
    TMPL_LOCALHOUR,
    TMPL_LOCALWEEK,
    TMPL_LOCALTIMESTAMP,

    TMPL_FORMATNUM,
    TMPL_GRAMMAR,
    TMPL_PLURAL,

    TMPL_INT,
    TMPL_MSG,
    TMPL_MSGNW,
    TMPL_RAW,
    TMPL_SUBST,

    TMPL_EXPR,
    TMPL_IFEXPR,
    TMPL_IFEQ,
    TMPL_TAG,
    TMPL_RELATED,
    TMPL_TIME,
    TMPL_INVOKE,
    TMPL_SECTION,
    TMPL_PROPERTY,
  };
};

}  // namespace nlp
}  // namespace sling

#endif  // NLP_WIKI_WIKI_PARSER_H_
