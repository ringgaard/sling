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

#include <algorithm>
#include <iostream>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/file/recordio.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/document/document.h"

DEFINE_string(commons, "", "Commons store");
DEFINE_string(key, "", "Document key");
DEFINE_string(html, "/tmp/test.html", "HTML output");

using namespace sling;
using namespace sling::nlp;

// Stylesheet for frame renderer.
const char *kStyleSheet = R"(
.panel {
  margin: 0px 0px 10px 10px;
  padding: 5px;
  box-shadow: 1px 1px 1px 0px #808080;
  background: white;
}

.panel-titlebar {
  text-align: left;
  font:  bold 11pt arial;
  padding: 3px;
  cursor: move;
}

.panel-title {
  width: 100%;
  text-align: left;
  padding: 3px;
}

.panel-icon {
  text-align: center;
  float: right;
  cursor: default;
}

.panel-content {
  padding: 10px;
}

.chip {
  text-align: center;
  padding: 5px;
  margin: 4px;
  border-radius: 10px;
  background-color: #E0E0E0;
  box-shadow: 1px 1px 1px 0px #808080;
  white-space: nowrap;
  cursor: pointer;
}

.type-label {
  font: bold 8pt arial;
  margin: 4px;
  padding: 4px;
  border-radius: 4px;
  background-color: #909090;
  color: white;
  vertical-align: baseline;
  white-space: nowrap;
}

.boxed {
  border: 1px solid black;
  text-align: center;
  font-size: 7pt;
  padding: 1px 1px 1px 1px;
  margin-right: 5px;
  cursor: pointer;
}

.tfs {
  position: relative;
  margin-right: 3px;
}

.tfs:before {
  content: "";
  position: absolute;
  left: -5px;
  top: 0;
  border: 1px solid black;
  border-right: 0px;
  width: 5px;
  height: 100%;
}

.tfs:after {
  content: "";
  position: absolute;
  right: -5px;
  top: 0;
  border: 1px solid black;
  border-left: 0px;
  width: 5px;
  height: 100%;
}

.tfs td {
  font-family: arial;
  font-size: 10pt;
  text-align: left;
  white-space:nowrap;
}

.tfs td:last-child {
  font-family: arial;
  font-size: 10pt;
  text-align: left;
  width:100%;
}

.tfs th {
  font-family: arial;
  font-size: 10pt;
  font-weight: bold;
  text-align: left;
  padding-bottom: 5px;
  line-height: 18pt;
}

.tfs-collapsed {
  font-weight:bold;
  font-style:italic;
  color: #909090;
  width:100%;
}

.tfs-collapsed:after {
  content: " ...";
  color: #909090;
  font-weight:bold;
  font-style:italic;
}

.label {
  color: white;
  font-size: 8pt;
  line-height: 90%;
  white-space:nowrap;
  overflow: hidden;
  background:rgba(120,120,120,0.75);
  position: absolute;
  top: -10px;
  left: -1px;
  padding: 1px;
  border: 1px solid #808080;
  border-radius: 3px;
  z-index: 10;
}

.b1 {
  background-color: #F8F8F8;
  border: 1px solid #D0D0D0;
  border-radius: 4px;
  margin: 1px;
  display: inline-block;
  position: relative;
  padding: 0px 2px 0px 2px;
  cursor: pointer;
}

.b2 {
  background-color: #F0F0F0;
  border: 1px solid #D0D0D0;
  border-radius: 4px;
  margin: 1px;
  display: inline-block;
  position: relative;
  padding: 0px 2px 0px 2px;
  cursor: pointer;
}

.b3 {
  background-color: #E8E8E8;
  border: 1px solid #D0D0D0;
  border-radius: 4px;
  margin: 1px;
  display: inline-block;
  position: relative;
  padding: 0px 2px 0px 2px;
  cursor: pointer;
}

.callout {
  display: inline;
  z-index: 20;
  padding: 10px 10px;

  position: fixed;
  border:1px solid #dca;
  background: #fffAF0;
  border-radius: 4px;
  box-shadow: 5px 5px 8px #ccc;
}

.notch {
  width: 12px;
  height: 22px;
  position: absolute;
  top: 20px;
  left: -12px;
}

)";

// Javascript functions for frame renderer.
const char *kFunctions = R"(

var profile_url = 'http://go/plato-browser';

var active_callout = null;
var highlighted = null;
var labeled = null;
var next_panel = 1;

var type_color = {
  '/s': '#0B5394',
  '/saft': '#38761D',
  '/pb': '#990000',
  '/vn': '#8B4513',
  '/f': '#FF8000',
  '/cxn': '#630084',
}

function TypeColor(type) {
  if (type == null) return null;
  var slash = type.indexOf('/', 1);
  if (slash == -1) return null;
  return type_color[type.substring(0, slash)];
}

function HoverText(frame) {
  var text = '';
  if (frame.id) {
    text += "id: " + frame.id + '\n';
  }
  if (frame.description) {
    text += "description: " + frame.description + '\n';
  }
  return text;
}

function FrameName(f)  {
  var name;
  if (typeof f == "number") {
    var frame = frames[f];
    var name = frame.name;
    if (!name) name = frame.id;
    if (!name) name = '#' + f;
  } else {
    name = f;
  }
  return name;
}

function BuildBox(index, collapsed) {
  var box = document.createElement("div");
  box.className = "boxed";
  box.innerHTML = index;
  box.setAttribute("frame", index);
  box.setAttribute("collapsed", collapsed);
  box.addEventListener('click', ClickBox, false);
  box.addEventListener('mouseenter', EnterBox, false);
  box.addEventListener('mouseleave', LeaveBox, false);
  return box;
}

function BuildAVM(fidx, rendered) {
  var frame = frames[fidx]
  rendered[fidx] = true;

  var tbl = document.createElement("table");
  tbl.className = "tfs";
  tbl.setAttribute("frame", fidx);

  if (frame.name || frame.types.length > 0) {
    var hdr = document.createElement("tr");
    tbl.appendChild(hdr);

    var title = document.createElement("th");
    title.colSpan = 3;
    hdr.appendChild(title);

    if (frame.name) {
      var name = document.createTextNode(frame.name);
      if (frame.id) {
        if (frame.id.startsWith('/m/') || frame.id.startsWith('/g/')) {
          var a = document.createElement("a");
          a.href = profile_url + "?mid=" + frame.id;
          a.appendChild(name);
          name = a
        } else {
          var s = document.createElement("span");
          s.appendChild(name);
          name = s;
        }
        name.setAttribute("title", frame.id);
      }
      title.appendChild(name);
    }

    for (var t = 0; t < frame.types.length; ++t) {
      var type = frame.types[t];
      var label = document.createElement("span");
      label.className = "type-label";

      var color = null;
      var typename = null;
      if (typeof type == "number") {
        schema = frames[type];
        typename = schema.name;
        if (typename) {
          var hover = HoverText(schema);
          if (hover.length > 0) {
            label.setAttribute("title", hover);
          }
        } else {
          typename = schema.id;
        }
        color = TypeColor(schema.id);
        if (!typename) typename = '(' + t + ')';
      } else {
        typename = type;
        color = TypeColor(type);
      }

      if (color) label.style.backgroundColor = color;
      label.appendChild(document.createTextNode(typename));
      title.appendChild(document.createTextNode(" "));
      title.appendChild(label);
    }
  }

  var slots = frame.slots;
  if (slots) {
    for (var i = 0; i < slots.length; i += 2) {
      var n = slots[i];
      var v = slots[i + 1];

      var row = document.createElement("tr");

      var label = document.createElement("td");
      var link = false;
      if (typeof n == "number") {
        var f = frames[n];
        var role = f.name;
        if (role) {
          var hover = HoverText(f);
          if (hover.length > 0) {
            label.setAttribute("title", hover);
          }
        } else {
          role = frames[n].id;
        }
        if (!role) role = '(' + n + ')';
        if (role == 'MID' || role == '/s/profile/mid') link = true;
        label.appendChild(document.createTextNode(role + ':'));
      } else {
        label.appendChild(document.createTextNode(n + ':'));
      }

      var box = document.createElement("td");
      var val = document.createElement("td");
      if (typeof v == "number") {
        var simple = frames[v].simple == 1;
        box.appendChild(BuildBox(v, simple));
        if (rendered[v]) {
          val = null;
        } else {
          if (simple) {
            val.appendChild(BuildCollapsedAVM(v));
          } else {
            val.appendChild(BuildAVM(v, rendered));
          }
        }
      } else {
        if (link || v.startsWith('/m/') || v.startsWith('/g/')) {
          var a = document.createElement("a");
          a.href = profile_url + "?mid=" + v;
          a.appendChild(document.createTextNode(v));
          val.appendChild(a);
        } else {
          val.appendChild(document.createTextNode(v));
        }
      }

      row.appendChild(label);
      row.appendChild(box);
      if (val) row.appendChild(val);
      tbl.appendChild(row);
    }
  }

  return tbl;
}

function BuildCollapsedAVM(fidx) {
  var frame = frames[fidx];
  var collapsed = document.createElement("span");
  collapsed.className = "tfs-collapsed";
  collapsed.setAttribute("frame", fidx);
  collapsed.appendChild(document.createTextNode(FrameName(fidx)));
  return collapsed;
}

function BuildPanel(phrase, fidx) {
  var panel = document.createElement("div");
  panel.className = "panel";
  panel.id = "p" + next_panel++;

  var titlebar = document.createElement("div");
  titlebar.className = "panel-titlebar";
  panel.appendChild(titlebar);

  var title = document.createElement("span");
  title.className = "panel-title";
  title.appendChild(document.createTextNode(phrase));
  titlebar.appendChild(title);

  var icon = document.createElement("span");
  icon.className = "panel-icon";
  icon.innerHTML = "&times;";
  icon.setAttribute("panel", panel.id);
  icon.addEventListener('click', ClosePanel, false);
  titlebar.appendChild(icon);

  var contents = document.createElement("div");
  contents.className = "panel-content"

  var avm = BuildAVM(fidx, {});
  contents.appendChild(avm);
  panel.appendChild(contents);

  return panel;
}

function AddPanel(phrase, fidx) {
  var panel = BuildPanel(phrase, fidx);
  document.getElementById("panels").appendChild(panel);
}

function OpenPanel(e) {
  e.stopPropagation();
  var span = e.currentTarget;
  var phrase = span.getAttribute("phrase");
  var fidx = parseInt(span.getAttribute("frame"));
  AddPanel('"' + phrase + '"', fidx);
}

function ClosePanel(e) {
  var pid = e.currentTarget.getAttribute("panel");
  var panel =  document.getElementById(pid);
  document.getElementById("panels").removeChild(panel);
}

function BuildChip(fidx) {
  var name = FrameName(fidx);
  var chip = document.createElement("span");
  chip.className = "chip";
  chip.id = "t" + fidx;
  chip.setAttribute("frame", fidx);
  chip.setAttribute("phrase", name);
  chip.appendChild(document.createTextNode(name));

  return chip;
}

function AddChip(fidx) {
  var chip = BuildChip(fidx);
  document.getElementById("themes").appendChild(chip);
  chip.addEventListener('click', OpenPanel, false);
  chip.addEventListener('mouseenter', EnterChip, false);
  chip.addEventListener('mouseleave', LeaveChip, false);
}

var notchgif = 'data:image/gif;base64,R0lGODlhDAAWAJEAAP/68NK8jv///' +
               'wAAACH5BAUUAAIALAAAAAAMABYAAAIrlI8SmQF83INyNoBtzPhy' +
               'XXHb1ylkZp5dSBqs6KrIq6Xw/FG3V+M9DpkVAAA7';

function AddCallout(span) {
  var callout = document.createElement("span");
  callout.className = "callout";

  var notch = document.createElement("img");
  notch.className = "notch";
  notch.setAttribute("src", notchgif);
  callout.appendChild(notch);

  var bbox = span.getBoundingClientRect();
  callout.style.left = (bbox.right + 15) + "px";
  callout.style.top = ((bbox.top + bbox.bottom) / 2 - 30)  + "px";

  var fidx = parseInt(span.getAttribute("frame"))
  var avm = BuildAVM(fidx, {});
  callout.appendChild(avm)

  span.appendChild(callout);
  return span;
}

function RemoveCallout(span) {
  for (var i = 0; i < span.childNodes.length; ++i) {
    var child = span.childNodes[i];
    if (child.className == "callout") {
      span.removeChild(child);
      break;
    }
  }
}

function GetAVMs(fidx) {
  var matches = null;
  var elements = document.getElementsByClassName("tfs");
  for (var i = 0; i < elements.length; ++i) {
    var e = elements[i];
    var frame = e.getAttribute("frame");
    if (frame == fidx) {
      if (matches == null) matches = [];
      matches.push(e);
    }
  }
  return matches;
}

function GetBoxes(fidx) {
  var matches = null;
  var elements = document.getElementsByClassName("boxed");
  for (var i = 0; i < elements.length; ++i) {
    var e = elements[i];
    var frame = e.getAttribute("frame");
    if (frame == fidx) {
      if (matches == null) matches = [];
      matches.push(e);
    }
  }
  return matches;
}

function EvokedFrames(midx) {
  var mention = frames[midx];
  var evoked = new Set();
  for (var s = 0; s < mention.slots.length; s += 2) {
    var value = mention.slots[s + 1];
    if (typeof value == "number") evoked.add(value);
  }
  return evoked;
}

function Mentions(evoked) {
  var mentions = new Set();
  for (var fidx of evoked) {
    var frame = frames[fidx];
    if (frame.mentions) {
      for (var m = 0; m < frame.mentions.length; ++m) {
        mentions.add(frame.mentions[m]);
      }
    }
  }
  return mentions;
}

function HighlightMentions(mentions) {
  for (var idx of mentions) {
    var span = document.getElementById('s' + idx);
    span.style.backgroundColor = '#FFFFFF';
    span.style.borderColor = '#FFFFFF';
    span.style.boxShadow = '2px 2px 9px 1px rgba(0,0,0,0.5)';
    highlighted.push(span);
  }
}

function HighlightFrames(evoked) {
  for (var fidx of evoked) {
    avms = GetAVMs(fidx);
    if (avms) {
      for (var i = 0; i < avms.length; ++i) {
        var avm = avms[i];
        avm.style.backgroundColor = '#D0D0D0';
        highlighted.push(avm);
      }
    }

    boxes = GetBoxes(fidx);
    if (boxes) {
      for (var i = 0; i < boxes.length; ++i) {
        var box = boxes[i];
        box.style.backgroundColor = '#D0D0D0';
        highlighted.push(box);
      }
    }
  }
}

function LabelMentionedRoles(fidx) {
  var frame = frames[fidx];
  for (var i = 0; i < frame.slots.length; i += 2) {
    var n = frame.slots[i];
    var v = frame.slots[i + 1];
    if (typeof v == "number") {
      var role = FrameName(n);
      var mentions = Mentions(new Set([v]));
      for (var idx of mentions) {
        var span = document.getElementById('s' + idx);
        var label = document.createElement("span");
        label.className = "label";
        label.appendChild(document.createTextNode(role + ':'));
        span.insertBefore(label, span.firstElementChild);
        labeled.push(span);
      }
    }
  }
}

function ClearHighlight() {
  if (highlighted) {
    for (var i = 0; i < highlighted.length; ++i) {
      highlighted[i].removeAttribute("style");
    }
    highlighted = null;
  }
  if (labeled) {
    for (var i = 0; i < labeled.length; ++i) {
      var span = labeled[i];
      for (var j = 0; j < span.childNodes.length; ++j) {
        var child = span.childNodes[j];
        if (child.className == "label") span.removeChild(child);
      }
    }
    labeled = null;
  }
}

function EnterSpan(e) {
  if (e.shiftKey) {
    if (active_callout) RemoveCallout(active_callout);
    active_callout = AddCallout(e.currentTarget);
  } else {
    ClearHighlight();
    var span = e.currentTarget;
    var midx = parseInt(span.getAttribute("frame"));

    highlighted = [];
    labeled = [];
    var evoked = EvokedFrames(midx);
    HighlightFrames(evoked);
    var corefs = Mentions(evoked);
    HighlightMentions(corefs);
    for (var fidx of evoked) {
      LabelMentionedRoles(fidx);
    }
  }
}

function LeaveSpan(e) {
  RemoveCallout(e.currentTarget);
  active_callout = null;
  ClearHighlight();
}

function EnterChip(e) {
  ClearHighlight();
  var chip = e.currentTarget;
  var fidx = parseInt(chip.getAttribute("frame"));

  highlighted = [];
  labeled = [];
  HighlightFrames([fidx]);
  LabelMentionedRoles(fidx);
}

function LeaveChip(e) {
  ClearHighlight();
}

function EnterBox(e) {
  if (e.shiftKey) return;
  ClearHighlight();
  var box = e.currentTarget;
  var fidx = parseInt(box.getAttribute("frame"));

  highlighted = [];
  labeled = [];
  var evoked = new Set([fidx]);
  HighlightFrames(evoked);
  var corefs = Mentions(evoked);
  HighlightMentions(corefs);
  LabelMentionedRoles(fidx);
}

function LeaveBox(e) {
  if (e.shiftKey) return;
  ClearHighlight();
}

function ClickBox(e) {
  var box = e.currentTarget;
  var collapsed = box.getAttribute("collapsed") == 1;
  var fidx = parseInt(box.getAttribute("frame"));
  var parent = box.parentElement
  var avm = parent.nextSibling
  if (!avm) return;

  ClearHighlight();
  if (collapsed) {
    avm.parentNode.replaceChild(BuildAVM(fidx, {}), avm);
    box.setAttribute("collapsed", 0);
  } else {
    avm.parentNode.replaceChild(BuildCollapsedAVM(fidx), avm);
    box.setAttribute("collapsed", 1);
  }
}

)";

// Initialization script for frame renderer.
const char *kScript = R"(

for (var i = 0; i < mentions.length; ++i) {
  var fidx = mentions[i];
  var span = document.getElementById('s' + fidx);

  span.addEventListener('click', OpenPanel, false);
  span.addEventListener('mouseenter', EnterSpan, false);
  span.addEventListener('mouseleave', LeaveSpan, false);

  var frame = frames[fidx];
  for (var s = 0; s < frame.slots.length; s += 2) {
    var value = frame.slots[s + 1];
    if (typeof value == "number") {
      var evoked = frames[value];
      if (evoked.mentions == null) evoked.mentions = [];
      evoked.mentions.push(fidx);
    }
  }
}

for (var i = 0; i < themes.length; ++i) {
  AddChip(themes[i]);
}

)";

class DocumentRenderer {
 public:
  // Maximum depth of the evoking spans. Spans can be deeper nested, but the
  // style sheet only has different rendering of the first levels.
  static const int kMaxSpanDepth = 3;

  DocumentRenderer(const Document &document)
      : document_(document),
        frames_(document.store()),
        mentions_(document.store()),
        themes_(document.store()) {
    n_name_ = document.store()->Lookup("name");
  }

  void Render() {
    // Render header.
    h("<!doctype html>\n");
    h("<html>\n");
    h("<head>\n");
    h("<meta charset=\"utf-8\">\n");
    h("<title>Document</title>\n");
    h("<style>\n");
    h(kStyleSheet);
    h("</style>\n");
    h("<script>\n");
    h(kFunctions);
    h("</script>\n");
    h("</head>\n");
    h("<body>\n");

    // Create layout with document text to the left and frames to the right.
    h("<table cellspacing=15px style=\"margin: 10 10px 10px 10px; background: #eeeeee;\">\n");
    h("<tr id=themes colspan=2>\n");
    h("</tr>\n");
    h("<tr>\n");

    // Build list of spans in nesting order.
    std::vector<Span *> spans;
    for (int i = 0; i < document_.num_spans(); ++i) {
      Span *span = document_.span(i);
      if (!span->deleted()) spans.push_back(span);
    }
    std::sort(spans.begin(), spans.end(),
              [](const Span *a, const Span *b) -> bool {
                if (a->begin() != b->begin()) {
                  return a->begin() < b->begin();
                } else {
                  return a->end() > b->end();
                }
              });


    // Build frame list.
    BuildFrameList();

    // Render document text.
    h("<td id=text valign=top width=500 style=\"background: white; border: 2px solid #cccccc; font: 13pt lora, georgia, serif; padding: 10px;\">\n");
    std::vector<Span *> nesting;
    int next = 0;
    for (int index = 0; index < document_.num_tokens(); ++index) {
      // Output break.
      if (index > 0) OutputBreak(document_.token(index));

      // Stack spans that begins on this token.
      while (next < spans.size() && spans[next]->begin() == index) {
        // Output span start.
        Span *span = spans[next];
        int fidx = Lookup(span->mention().handle());
        string text = span->GetText();
        int depth = nesting.size() + 1;
        if (depth > kMaxSpanDepth) depth = kMaxSpanDepth;
        h("<span id='s");
        h(fidx);
        h("' frame=");
        h(fidx);
        h(" class='b");
        h(depth);
        h("' phrase='");
        escape(text);
        h("'>");

        // Push span onto stack.
        nesting.push_back(span);
        next++;
      }

      // Output token.
      OutputToken(document_.token(index));

      // Pop spans that end on this token.
      while (!nesting.empty() && nesting.back()->end() == index + 1) {
        h("</span>");
        nesting.pop_back();
      }
    }
    h("</td>\n");

    h("<td id=panels valign=top>\n");
    h("</td>\n");
    h("</table>\n");

    // Output frame list.
    h("<script>\n");
    RenderFrameList();
    h(kScript);
    h("</script>\n");

    // Render footer.
    h("</body>\n");
    h("</html>\n");
  }

  void OutputBreak(const Token &token) {
    BreakType brk = token.brk();
    if (brk >= CHAPTER_BREAK) {
      h("\n<hr>\n");
    } else if (brk >= SECTION_BREAK) {
      h("\n<center>***</center>\n");
    } else if (brk >= PARAGRAPH_BREAK) {
      h("\n<p>");
    } else if (brk >= SENTENCE_BREAK) {
      h("&ensp;");
    } else if (brk >= SPACE_BREAK) {
      h(" ");
    }
  }

  void OutputToken(const Token &token) {
    // Convert special punctuation tokens.
    const string &word = token.word();
    if (word == "``") {
      h("“");
    } else if (word == "''") {
      h("”");
    } else if (word == "--") {
      h("—");
    } else if (word == "...") {
      h("…");
    } else {
      escape(word);
    }
  }

  void BuildFrameList() {
    // Builds client-side frame list.
    Store *store = document_.store();
    Handle n_evokes = store->Lookup("evokes");

    // Add standard values.
    Add(Handle::isa());
    Add(Handle::is());
    Add(n_name_);

    // Add all evoked frames.
    Handles queue(store);
    for (int i = 0; i < document_.num_spans(); ++i) {
      Span *span = document_.span(i);
      if (span->deleted()) continue;
      const Frame &mention = span->mention();

      // Add the mention frame.
      if (Add(mention.handle())) {
        queue.push_back(mention.handle());
        mentions_.push_back(mention.handle());
      }

      // Add all evoked frames.
      for (const Slot &slot : mention) {
        if (slot.name != n_evokes) continue;

        // Queue all evoked frames.
        Handle h = slot.value;
        if (!store->IsFrame(h)) continue;
        if (Add(h)) {
          queue.push_back(h);
        }
      }
    }

    // Add thematic frames.
    for (Handle h : document_.themes()) {
      if (!store->IsFrame(h)) continue;
      if (Add(h)) {
        queue.push_back(h);
      }
      themes_.push_back(h);
    }

    // Process queue.
    int current = 0;
    while (current < queue.size()) {
      // Process all slot names and values for next frame in queue.
      Frame frame(store, queue[current++]);
      for (const Slot &slot : frame) {
        if (store->IsFrame(slot.name)) {
          if (Add(slot.name)) {
            // Only add local frames to queue.
            if (slot.name.IsLocalRef()) queue.push_back(slot.name);
          }
        }
        if (store->IsFrame(slot.value)) {
          if (Add(slot.value)) {
            // Only add local frames to queue.
            if (slot.value.IsLocalRef()) queue.push_back(slot.value);
          }
        }
      }
    }
  }

  // Renders the frame list.
  void RenderFrameList() {
    // Get role names.
    Store *store = document_.store();
    Handle n_description = store->Lookup("description");
    Handle n_simple = store->Lookup("simple");

    // Output frame table as JSON.
    h("var frames = [\n");
    string id;
    string name;
    string description;
    std::vector<int> types;
    std::vector<string> external_types;
    std::vector<std::pair<string, string>> roles;
    int index = 0;
    for (Handle handle : frames_) {
      // Collect id, name, description, types, and other roles for frame.
      bool simple = false;
      id.clear();
      name.clear();
      description.clear();
      types.clear();
      external_types.clear();
      roles.clear();
      if (store->IsFrame(handle)) {
        Frame frame(store, handle);
        for (const Slot &slot : frame) {
          if (slot.name == Handle::id() && store->IsSymbol(slot.value)) {
            if (id.empty()) id = Symbol(store, slot.value).name().str();
          } else if (slot.name == n_name_ && store->IsString(slot.value)) {
            if (name.empty()) name = String(store, slot.value).value();
          } else if (slot.name == n_description &&
                     store->IsString(slot.value)) {
            if (description.empty()) {
              description = String(store, slot.value).value();
            }
          } else if (slot.name.IsIsA()) {
            int index = Lookup(slot.value);
            if (index != -1) {
              types.push_back(index);
            } else {
              Frame type(store, slot.value);
              if (type.valid()) {
                string type_id = type.Id().str();
                if (!type_id.empty()) external_types.push_back(type_id);
                if (type.GetBool(n_simple)) simple = true;
              }
            }
          } else {
            string name = ConvertToJS(store, slot.name);
            string value = ConvertToJS(store, slot.value);
            roles.emplace_back(name, value);
          }
        }
      } else if (store->IsSymbol(handle)) {
        Symbol symbol(store, handle);
        id = symbol.name().str();
      }

      // Output JSON object for frame.
      h("  {id: ");
      h(ConvertToJSString(id));
      h(", name: ");
      h(ConvertToJSString(name));
      h(", description: ");
      h(ConvertToJSString(description));
      if (simple) h(", simple: 1");
      h(", types: [");
      bool first = true;
      for (int t : types) {
        if (!first) h(", ");
        h(t);
        first = false;
      }
      for (const string &e : external_types) {
        if (!first) h(", ");
        h(ConvertToJSString(e));
        first = false;
      }
      h("], slots: [");
      first = true;
      for (auto &r : roles) {
        if (!first) h(", ");
        h(r.first);
        h(",");
        h(r.second);
        first = false;
      }
      h("], mentions: null}, // ");
      h(index);
      h("\n");
      index++;
    }
    h("];\n");

    // Output frame ids for mention spans.
    h("var mentions = [");
    bool first = true;
    for (Handle mention : mentions_) {
      int index = Lookup(mention);
      CHECK_NE(index, -1);
      if (!first) h(", ");
      h(index);
      first = false;
    }
    h("];\n");

    // Output frame ids for thematic frames.
    h("var themes = [");
    first = true;
    for (Handle theme : themes_) {
      int index = Lookup(theme);
      CHECK_NE(index, -1);
      if (!first) h(", ");
      h(index);
      first = false;
    }
    h("];\n");
  }

  // Convert string to JS string.
  string ConvertToJSString(const string &str) {
    if (str.empty()) {
      return "null";
    } else {
      string js;
      js.append("'");
      for (char c : str) {
        switch (c) {
          case '\'': js.append("\\'"); break;
          case '\n': js.append("\\n"); break;
          default: js.push_back(c);
        }
      }
      js.append("'");
      return js;
    }
  }

  // Converts SLING object handle to Javascript value. Handles to known
  // frames are output as integer indices.
  string ConvertToJS(Store *store, Handle value) {
    // Output null for nil values.
    if (value.IsNil()) return "null";

    // Output integer index for references to known frames.
    auto f = mapping_.find(value);
    if (f != mapping_.end()) return std::to_string(f->second);

    // Output name or id as string for external frames.
    string literal;
    if (store->IsFrame(value)) {
      Frame frame(store, value);
      if (frame.Has(n_name_)) {
        literal = ConvertToJSString(frame.GetString(n_name_));
      } else {
        string id = frame.Id().str();
        if (!id.empty()) literal = ConvertToJSString(id);
      }
      if (!literal.empty()) return literal;
    }

    // Output strings literally.
    if (store->IsString(value)) {
      String str(store, value);
      literal = ConvertToJSString(str.value());
      return literal;
    }

    // Otherwise output SLING text encoding.
    return ConvertToJSString(ToText(store, value));
  }

  // Adds frame to frame list.
  bool Add(Handle h) {
    // Do not add frame if it is already in the list.
    if (mapping_.find(h) != mapping_.end()) return false;

    // Add frame to list and mapping.
    mapping_[h] = frames_.size();
    frames_.push_back(h);
    return true;
  }

  // Looks up frame index for frame.
  int Lookup(Handle handle) {
    auto f = mapping_.find(handle);
    return f != mapping_.end() ? f->second : -1;
  }

  const string &html() const { return html_; }

 private:
  void h(const char *str) { html_.append(str); }
  void h(const string &str) { html_.append(str); }
  void h(int n) { html_.append(std::to_string(n)); }
  void escape(const string &str) { html_.append(str); }

  const Document &document_;
  Handle n_name_;
  string html_;
  Handles frames_;          // frames by index
  Handles mentions_;        // mentions evoking frames
  Handles themes_;          // thematic frames
  HandleMap<int> mapping_;  // mapping from frame to index
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);
  if (argc < 2) {
    std::cerr << argv[0] << "[OPTIONS] [FILE] ...\n";
    return 1;
  }

  Store commons;
  if (!FLAGS_commons.empty()) {
    LoadStore(FLAGS_commons, &commons);
  }
  commons.Freeze();

  std::vector<string> files;
  for (int i = 1; i < argc; ++i) {
    File::Match(argv[i], &files);
  }

  RecordFileOptions options;
  RecordDatabase db(files, options);
  Record record;
  CHECK(db.Lookup(FLAGS_key, &record));

  Store store(&commons);
  Frame top = Decode(&store, record.value).AsFrame();
  Document document(top);

  LOG(INFO) << ToText(document.top(), 2);

  DocumentRenderer renderer(document);
  renderer.Render();

  File::WriteContents(FLAGS_html, renderer.html());

  return 0;
}
