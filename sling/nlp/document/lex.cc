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

#include "sling/nlp/document/lex.h"

#include <string>
#include <vector>

#include "sling/base/types.h"
#include "sling/frame/serialization.h"
#include "sling/frame/object.h"
#include "sling/nlp/document/document.h"
#include "sling/string/ctype.h"

namespace sling {
namespace nlp {

bool DocumentLexer::Lex(Document *document, Text lex) const {
  // Extract the plain text and frame source from the LEX-encoded text and keep
  // track of mention boundaries.
  string text;
  string source;
  std::vector<Markable> markables;
  std::vector<int> stack;
  std::vector<int> themes;
  int current_object = -1;
  int frame_level = 0;
  bool in_annotation = false;
  for (char c : lex) {
    if (frame_level > 0) {
      // Inside frame. Parse until outer '}' found.
      source.push_back(c);
      if (c == '{') {
        frame_level++;
      } else if (c == '}') {
        if (--frame_level == 0) {
          if (!in_annotation) {
            // Add frame as theme.
            themes.push_back(current_object);
          }
        }
      }
    } else {
      switch (c) {
        case '[':
          // Start new span.
          stack.push_back(markables.size());
          markables.emplace_back(text.size());
          break;
        case '|':
          // Start span annotation.
          if (in_annotation) return false;
          if (!stack.empty()) {
            // Enclose evoked frames in a list.
            in_annotation = true;
            current_object++;
            source.push_back('[');
            markables[stack.back()].object = current_object;
          } else {
            text.push_back(c);
          }
          break;
        case ']':
          // End current span.
          if (stack.empty()) return false;
          if (in_annotation) {
            source.push_back(']');
            in_annotation = false;
          }
          markables[stack.back()].end = text.size();
          stack.pop_back();
          break;
        case '{':
          if (stack.empty()) {
            // Start new thematic frame.
            current_object++;
          }
          source.push_back(c);
          frame_level++;
          break;
        default:
          if (in_annotation) {
            // Add character to frame source.
            source.push_back(c);
          } else {
            // Add character to plain text.
            text.push_back(c);
          }
      }
    }
  }

  if (!stack.empty()) return false;

  // Trim whitespace.
  int begin = 0;
  while (begin < text.size() && ascii_isspace(text[begin])) begin++;
  int end = text.size();
  while (end > begin + 1 && ascii_isspace(text[end - 1])) end--;
  if (begin > 0 || end < text.size()) {
    text = text.substr(begin, end - begin);
  }
  if (begin > 0) {
    for (Markable &m : markables) {
      m.begin -= begin;
      m.end -= begin;
    }
    end -= begin;
  }

  // Tokenize plain text and add tokens to document.
  tokenizer_->Tokenize(document, text);

  // Parse frames.
  Store *store = document->store();
  Handles objects(store);
  StringReader input(store, source);
  Reader *reader = input.reader();
  while (!reader->done()) {
    objects.push_back(reader->ReadObject());
    if (reader->error()) return false;
  }
  if (objects.size() != current_object + 1) return false;

  // Add mentions to document.
  HandleSet added;
  for (auto &m : markables) {
    int begin = document->Locate(m.begin);
    int end = document->Locate(m.end);
    Span *span = document->AddSpan(begin, end);
    if (m.object != -1) {
      Array evoked(store, objects[m.object]);
      for (int i = 0; i < evoked.length(); ++i) {
        Handle frame = evoked.get(i);
        span->Evoke(frame);
        added.add(frame);
      }
    }
  }

  // Add thematic frames. Do not add frames that are evoked by spans.
  for (int theme : themes) {
    Handle frame = objects[theme];
    if (added.has(frame)) {
      document->AddTheme(frame);
    }
  }

  // Update underlying document frame.
  document->Update();

  return true;
}

static void OutputStyle(int style, Output *output) {
  if (style & BEGIN_STYLE) {
    if (style & TITLE_BEGIN) output->Write("<h1>");
    if (style & HEADING_BEGIN) output->Write("<h2>");
    if (style & BREAKOUT_BEGIN) output->Write("<aside>");
    if (style & QUOTE_BEGIN) output->Write("<blockquote>");
    if (style & ITEMIZE_BEGIN) output->Write("<ul>\n");
    if (style & LISTITEM_BEGIN) output->Write("<li>");
    if (style & BOLD_BEGIN) output->Write("<b>");
    if (style & ITALIC_BEGIN) output->Write("<em>");
  }
  if (style & END_STYLE) {
    if (style & ITALIC_END) output->Write("</em>");
    if (style & BOLD_END) output->Write("</b>");
    if (style & LISTITEM_END) output->Write("</li>");
    if (style & ITEMIZE_END) output->Write("\n</ul>");
    if (style & QUOTE_END) output->Write("</blockquote>");
    if (style & BREAKOUT_END) output->Write("</aside>");
    if (style & HEADING_END) output->Write("</h2>");
    if (style & TITLE_END) output->Write("</h1>");
  }
}

static void OutputToken(const string &word, Output *output) {
  if (word == "``") {
    output->Write("“");
  } else if (word == "''") {
    output->Write("”");
  } else if (word == "--") {
    output->Write("–");
  } else if (word == "...") {
    output->Write("…");
  } else {
    for (char c : word) {
      switch (c) {
        case '&':  output->Write("&#38;", 5); break;
        case '<':  output->Write("&#60;", 5); break;
        case '>':  output->Write("&#62;", 5); break;
        case '{':  output->Write("&#123;", 6); break;
        case '|':  output->Write("&#124;", 6); break;
        case '}':  output->Write("&#125;", 6); break;
        case '[':  output->Write("&#91;", 5); break;
        case ']':  output->Write("&#93;", 5); break;
        case '"':  output->Write("&#34;", 5); break;
        default: output->WriteChar(c);
      }
    }
  }
}

string ToLex(const Document &document) {
  // Set up frame printer for output.
  string lex;
  StringOutputStream stream(&lex);
  Output output(&stream);
  Printer printer(document.store(), &output);

  // Output all tokens with mentions and evoked frames.
  Handles evoked(document.store());
  int styles = 0;
  for (const Token &token : document.tokens()) {
    // Add style end.
    int style = token.style();
    if (style != 0) {
      int end_style = style & END_STYLE;
      OutputStyle(end_style, &output);
      styles &= ~end_style;
    }

    // Add token break.
    if (token.index() > 0) {
      switch (token.brk()) {
        case NO_BREAK: break;
        case SPACE_BREAK: output.WriteChar(' '); break;
        case LINE_BREAK: output.Write("<br/>\n", 6); break;
        case SENTENCE_BREAK: output.Write("  ", 2); break;
        case PARAGRAPH_BREAK:
          output.Write("\n\n", 2);
          if (!(style & HEADING_BEGIN)) output.Write("<p>", 3);
          break;
        case SECTION_BREAK: output.Write("\n<section>\n"); break;
        case CHAPTER_BREAK: output.Write("\n<chapter>\n"); break;
      }
    }

    // Add style begin.
    if (style != 0) {
      int begin_style = style & BEGIN_STYLE;
      OutputStyle(begin_style, &output);
      styles |= begin_style << 1;
    }

    // Add span open brackets.
    Span *span = document.GetSpanAt(token.index());
    for (Span *s = span; s != nullptr; s = s->parent()) {
      if (s->begin() == token.index()) output.WriteChar('[');
    }

    // Add token word. Escape reserved characters.
    OutputToken(token.word(), &output);

    // Add span close brackets.
    for (Span *s = span; s != nullptr; s = s->parent()) {
      if (s->end() == token.index() + 1) {
        bool first = true;
        s->AllEvoked(&evoked);
        for (Handle frame : evoked) {
          output.WriteChar(first ? '|' : ' ');
          first = false;
          printer.PrintReference(frame);
        }
        output.WriteChar(']');
      }
    }
  }

  // Terminate remaining styles.
  if (styles != 0) {
    OutputStyle(styles, &output);
  }

  // Output themes.
  for (Handle frame : document.themes()) {
    printer.PrintReference(frame);
  }

  output.Flush();
  return lex;
}

string ToHTML(const Document &document) {
  // Set up HTML output.
  string html;
  StringOutputStream stream(&html);
  Output output(&stream);

  // Convert document to simplified HTML.
  Store *store = document.store();
  int styles = 0;
  for (const Token &token : document.tokens()) {
    // Add style end.
    int style = token.style();
    if (style != 0) {
      int end_style = style & END_STYLE;
      OutputStyle(end_style, &output);
      styles &= ~end_style;
    }

    // Add token break.
    if (token.index() > 0) {
      switch (token.brk()) {
        case NO_BREAK: break;
        case SPACE_BREAK: output.WriteChar(' '); break;
        case LINE_BREAK: output.Write("<br/>", 5); break;
        case SENTENCE_BREAK: output.Write("  ", 2); break;
        case PARAGRAPH_BREAK: output.Write("<p>", 3); break;
        case SECTION_BREAK: output.Write("<section>"); break;
        case CHAPTER_BREAK: output.Write("<chapter>"); break;
      }
    }

    // Add style begin.
    if (style != 0) {
      int begin_style = style & BEGIN_STYLE;
      OutputStyle(begin_style, &output);
      styles |= begin_style << 1;
    }

    // Add span start.
    Span *span = document.GetSpanAt(token.index());
    for (Span *s = span; s != nullptr; s = s->parent()) {
      if (s->begin() == token.index()) {
        output.Write("<a", 2);
        Handle evoked = store->Resolve(span->evoked());
        if (store->IsPublic(evoked)) {
          output.Write(" ref=", 5);
          output.WriteChar('"');
          output.Write(store->FrameId(evoked));
          output.WriteChar('"');
        }
        output.WriteChar('>');
      }
    }

    // Add token word.
    OutputToken(token.word(), &output);

    // Add span end.
    for (Span *s = span; s != nullptr; s = s->parent()) {
      if (s->end() == token.index() + 1) {
        output.Write("</a>", 4);
      }
    }
  }

  // Terminate remaining styles.
  if (styles != 0) {
    OutputStyle(styles, &output);
  }

  output.Flush();
  return html;
}

void Lexify(Document *document) {
  string lex = ToLex(*document);
  Builder b(document->top());
  b.Delete(document->names()->n_tokens);
  b.Delete(document->names()->n_mention);
  b.Delete(document->names()->n_theme);
  b.Delete(document->names()->n_text);
  b.Set(document->names()->n_lex, lex);
  b.Update();
}

}  // namespace nlp
}  // namespace sling

