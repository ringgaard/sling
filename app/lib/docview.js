import {Component} from "/common/lib/component.js";

const kb_url = '/kb/';

const type_color = {
  '/s': '#FF8000',
  '/saft': '#38761D',
  '/pb': '#990000',
  '/vn': '#8B4513',
  '/w': '#0B5394',
};

const begin_styles = [
  {mask: (1 << 5),  tag: "<h2>"},
  {mask: (1 << 11), tag: "<blockquote>"},
  {mask: (1 << 7),  tag: "<ul>"},
  {mask: (1 << 9),  tag: "<li>"},
  {mask: (1 << 1),  tag: "<b>"},
  {mask: (1 << 3),  tag: "<em>"},
];

const end_styles = [
  {mask: (1 << 4),  tag: "</em>",         para: false},
  {mask: (1 << 2),  tag: "</b>",          para: false},
  {mask: (1 << 10), tag: "</li>",         para: true},
  {mask: (1 << 8),  tag: "</ul>",         para: true},
  {mask: (1 << 12), tag: "</blockquote>", para: true},
  {mask: (1 << 6),  tag: "</h2>",         para: true},
];

const notchgif = 'data:image/gif;base64,R0lGODlhDAAWAJEAAP/68NK8jv///' +
                 'wAAACH5BAUUAAIALAAAAAAMABYAAAIrlI8SmQF83INyNoBtzPhy' +
                 'XXHb1ylkZp5dSBqs6KrIq6Xw/FG3V+M9DpkVAAA7';

let next_panel = 1;
let next_docno = 1;
let text_encoder = new TextEncoder("utf-8");
let text_decoder = new TextDecoder("utf-8");

// Token break types.
const NO_BREAK = 0;
const SPACE_BREAK = 1;
const LINE_BREAK = 2;
const SENTENCE_BREAK = 3;
const PARAGRAPH_BREAK = 4;
const SECTION_BREAK = 5;
const CHAPTER_BREAK = 6;

// Document representation from JSON response.
export class Document {
  constructor(data) {
    this.data = data;
    this.docno = next_docno++;
    this.title = data.title;
    this.url = data.url;
    this.key = data.key;
    this.text = text_encoder.encode(data.text);
    this.tokens = data.tokens || [];
    this.frames = data.frames
    this.spans = data.spans
    this.themes = data.themes
    this.SortSpans();
  }

  SortSpans() {
    // Sort spans in nesting order.
    this.spans.sort((a, b) => {
      if (a.begin != b.begin) {
        return a.begin - b.begin;
      } else {
        return b.end - a.end;
      }
    });
  }

  Word(index) {
    let token = this.tokens[index];
    if (token.word) {
      return token.word;
    } else {
      let start = token.start;
      if (start === undefined) start = 0;
      let size = token.size;
      if (size === undefined) size = 1;
      return text_decoder.decode(this.text.slice(start, start + size));
    }
  }

  Phrase(begin, end) {
    if (end == begin + 1) return this.Word(begin);
    let words = [];
    for (let i = begin; i < end; ++i) {
      words.push(this.Word(i));
    }
    return words.join(" ");
  }
}

// Document viewer for displaying spans and frames.
export class DocumentViewer extends Component {
  constructor() {
    super();
    this.document = null;
    this.active_callout = null;
    this.highlighted = null;
    this.labeled = null;
    this.panels = {};
  }

  docno() {
    return this.document.docno;
  }

  visible() {
    let doc = this.state;
    return doc;
  }

  onupdated() {
    // Bail out if there is no document.
    this.document = this.state;
    if (!this.document) return;
    this.panels = {};

    let docno = this.document.docno;
    for (let i = 0; i < this.document.spans.length; ++i) {
      let fidx = this.document.spans[i].frame;

      // Bind event handlers for spans.
      let span = this.find("#s" + docno + "-" + fidx);
      if (span) {
        span.addEventListener('click', this.OpenPanel.bind(this), false);
        span.addEventListener('mouseenter', this.EnterSpan.bind(this), false);
        span.addEventListener('mouseleave', this.LeaveSpan.bind(this), false);
      }

      // Update frame mentions.
      let frame = this.document.frames[fidx];
      for (let s = 0; s < frame.slots.length; s += 2) {
        let value = frame.slots[s + 1];
        if (typeof value == "number") {
          let evoked = this.document.frames[value];
          if (evoked.mentions == null) evoked.mentions = [];
          evoked.mentions.push(fidx);
        }
      }
    }

    // Add chips for themes.
    for (let i = 0; i < this.document.themes.length; ++i) {
      this.AddChip(this.document.themes[i]);
    }
  }

  render() {
    // Render document viewer with text to the left, panels to the right.
    let h = [];
    h.push('<div class="doctext">');

    // Render document.
    let doc = this.state;
    let docid = doc.key;
    let docno = doc.docno.toString();
    let spans = doc.spans;
    let nesting = [];
    let next = 0;

    // Render document title.
    if (doc.title) {
      let headline = Component.escape(doc.title);
      h.push('<h1 class="title">');
      if (doc.url) {
        h.push('<a class="titlelink" target="_blank" rel="noreferer" href="');
        h.push(doc.url);
        h.push('">');
        h.push(headline);
        h.push('</a>');
      } else {
        p.push(headline);
      }
      if (docid) {
        let url = kb_url + docid;
        h.push('<span class="docref">(');
        h.push('<a class="topiclink" target="_blank" href="');
        h.push(url);
        h.push('">');
        h.push(docid);
        h.push('</a>)</span>');
      }
      h.push('</h1>');
    } else if (doc.url) {
      h.push('<div class="title">');
      h.push('url: <a target="_blank" rel="noreferer" href="');
      h.push(doc.url);
      h.push('">');
      h.push(Component.escape(doc.url));
      h.push('</a><br></div>');
    }

    // Render document text.
    let stylebits = 0;
    for (let index = 0; index < doc.tokens.length; ++index) {
      let token = doc.tokens[index];
      let brk = token.break;

      // Render style end.
      if (token.style) {
        for (let ts of end_styles) {
          if (ts.mask & token.style) {
            h.push(ts.tag);
            stylebits &= ~ts.mask;
            if (ts.para && brk == PARAGRAPH_BREAK) brk = undefined;
          }
        }
      }

      // Render token break.
      if (index > 0) {
        if (brk === undefined) {
          h.push(" ");
        } else if (brk >= CHAPTER_BREAK) {
          h.push("<hr>");
        } else if (brk >= SECTION_BREAK) {
          h.push("<center>***</center>");
        } else if (brk >= PARAGRAPH_BREAK) {
          h.push("<p>");
        } else if (brk >= SENTENCE_BREAK) {
          h.push(" 	");
        } else if (brk >= SPACE_BREAK) {
          h.push(" ");
        }
      }

      // Render style start.
      if (token.style) {
        for (let ts of begin_styles) {
          if (ts.mask & token.style) {
            h.push(ts.tag);
            stylebits |= ts.mask << 1;
          }
        }
      }

      // Start spans that begin on this token.
      while (next < spans.length && spans[next].begin == index) {
        let span = spans[next];
        nesting.push(span);
        next++;

        let depth = nesting.length;
        let fidx = span.frame.toString();
        let phrase = doc.Phrase(span.begin, span.end);
        if (depth > 3) depth = 3;
        h.push('<span id="s');
        h.push(docno);
        h.push('-');
        h.push(fidx);
        h.push('" frame="');
        h.push(fidx);
        h.push('" class="b');
        h.push(depth);
        h.push('" phrase="');
        h.push(phrase);
        h.push('">');
      }

      // Render token word.
      let word = doc.Word(index);
      if (word == "``") {
        word = "“";
      } else if (word == "''") {
        word = "”";
      } else if (word == "--") {
        word = "–";
      } else if (word == "...") {
        word = "…";
      }
      h.push(Component.escape(word));

      // End spans that end on this token.
      while (nesting.length > 0 &&
             nesting[nesting.length - 1].end == index + 1) {
        nesting.pop();
        h.push('</span>');
      }
    }

    // Terminate remaining styles.
    for (let ts of end_styles) {
      if (ts.mask & stylebits) {
        h.push(ts.tag);
      }
    }

    // Add container for theme chips.
    h.push('<div id="themes');
    h.push(docno);
    h.push('" class="themes"></div>');
    h.push('</div>');

    // Add panel container.
    h.push('<div class="docspacer"></div>');
    h.push('<div id="panels');
    h.push(docno);
    h.push('" class="docpanels"></div>');

    return h.join("");
  }

  TypeColor(type) {
    if (type == null) return null;
    let slash = type.indexOf('/', 1);
    if (slash == -1) return null;
    return type_color[type.substring(0, slash)];
  }

  HoverText(frame) {
    let text = '';
    if (frame.id) {
      text += "id: " + frame.id + '\n';
    }
    if (frame.description) {
      text += frame.description + '\n';
    }
    return text;
  }

  FrameName(f)  {
    let name;
    if (typeof f == "number") {
      let frame = this.document.frames[f];
      name = frame.name;
      if (!name) name = frame.id;
      if (!name) name = '#' + f;
    } else {
      name = f;
    }
    return name;
  }

  IsExternal(f) {
    if (typeof f == "number") f = this.document.frames[f];
    if (typeof f == "object") {
      for (let t = 0; t < f.types.length; ++t) {
        let type = f.types[t];
        if (typeof type == "number") {
          let schema = this.document.frames[type];
          if (schema.id == "/w/item") return true;
        }
      }
    }
    return false;
  }

  BuildBox(index, collapsed) {
    let box = document.createElement("div");
    box.className = "boxed";
    box.innerHTML = index;
    box.setAttribute("frame", index);
    box.setAttribute("collapsed", collapsed);
    box.addEventListener('click', this.ClickBox.bind(this), false);
    box.addEventListener('mouseenter', this.EnterBox.bind(this), false);
    box.addEventListener('mouseleave', this.LeaveBox.bind(this), false);
    return box;
  }

  AddTypes(elem, types) {
    if (!types) return;
    for (let t = 0; t < types.length; ++t) {
      let type = types[t];
      let label = document.createElement("span");
      label.className = "type-label";

      let color = null;
      let typename = null;
      if (typeof type == "number") {
        let schema = this.document.frames[type];
        typename = schema.name;
        if (typename) {
          let hover = this.HoverText(schema);
          if (hover.length > 0) {
            label.setAttribute("tooltip", hover);
          }
        } else {
          typename = schema.id;
        }
        color = this.TypeColor(schema.id);
        if (!typename) typename = '(' + t + ')';
      } else {
        typename = type;
        color = this.TypeColor(type);
      }

      if (color) label.style.backgroundColor = color;
      label.appendChild(document.createTextNode(typename));
      elem.appendChild(document.createTextNode(" "));
      elem.appendChild(label);
    }
  }

  BuildAVM(fidx, rendered) {
    let frame = this.document.frames[fidx];
    if (frame == undefined) return document.createTextNode(fidx);
    rendered[fidx] = true;

    let tbl = document.createElement("table");
    tbl.className = "tfs";
    tbl.setAttribute("frame", fidx);

    if (frame.name || frame.id || frame.types.length > 0) {
      let hdr = document.createElement("tr");
      tbl.appendChild(hdr);

      let title = document.createElement("th");
      title.colSpan = 3;
      hdr.appendChild(title);

      if (frame.name || frame.id) {
        let name = document.createTextNode(frame.name ? frame.name : frame.id);
        if (frame.id) {
          if (this.IsExternal(frame)) {
            let a = document.createElement("a");
            a.href = kb_url + frame.id;
            a.target = " _blank";
            a.appendChild(name);
            name = a
          } else {
            let s = document.createElement("span");
            s.appendChild(name);
            name = s;
          }
          name.setAttribute("tooltip", this.HoverText(frame));
        }
        title.appendChild(name);
      }

      this.AddTypes(title, frame.types);
    }

    let slots = frame.slots;
    if (slots) {
      for (let i = 0; i < slots.length; i += 2) {
        let n = slots[i];
        let v = slots[i + 1];

        let row = document.createElement("tr");
        let label = document.createElement("td");
        let box = document.createElement("td");
        let val = document.createElement("td");

        if (typeof n == "number") {
          let span = document.createElement("span");
          let f = this.document.frames[n];
          let role = f.name;
          if (role) {
            let hover = this.HoverText(f);
            if (hover.length > 0) {
              span.setAttribute("tooltip", hover);
            }
          } else {
            role = this.document.frames[n].id;
          }
          if (!role) role = '(' + n + ')';
          span.appendChild(document.createTextNode(role + ':'));
          label.appendChild(span);
        } else {
          label.appendChild(document.createTextNode(n + ':'));
        }

        if (typeof v == "number") {
          let simple = this.document.frames[v].simple == 1;
          box.appendChild(this.BuildBox(v, simple));
          if (rendered[v]) {
            val = null;
          } else {
            if (simple) {
              val.appendChild(this.BuildCollapsedAVM(v));
            } else {
              val.appendChild(this.BuildAVM(v, rendered));
            }
          }
        } else {
          if (this.IsExternal(v)) {
            let a = document.createElement("a");
            a.href = kb_url + v;
            a.target = "_blank";
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

  BuildCollapsedAVM(fidx) {
    let frame = this.document.frames[fidx];
    let collapsed = document.createElement("span");
    collapsed.className = "tfs-collapsed";
    collapsed.setAttribute("frame", fidx);
    collapsed.appendChild(document.createTextNode(this.FrameName(fidx)));
    return collapsed;
  }

  BuildPanel(phrase, fidx) {
    let frame = this.document.frames[fidx];
    let panel = document.createElement("div");
    panel.className = "panel";
    panel.id = "p" + next_panel++;
    panel.setAttribute("frame", fidx);

    let titlebar = document.createElement("div");
    titlebar.className = "panel-titlebar";
    panel.appendChild(titlebar);

    let title = document.createElement("span");
    title.className = "panel-title";
    if (phrase) {
      title.appendChild(document.createTextNode(phrase));
      titlebar.appendChild(title);
      this.AddTypes(titlebar, frame.types);
    }

    let icon = document.createElement("span");
    icon.className = "panel-icon";
    icon.innerHTML = "&times;";
    icon.setAttribute("panel", panel.id);
    icon.addEventListener('click', this.ClosePanel.bind(this), false);
    titlebar.appendChild(icon);

    let contents = document.createElement("div");
    contents.className = "panel-content"

    if (phrase) {
      let rendered = {};
      let slots = frame.slots;
      if (slots) {
        for (let i = 0; i < slots.length; i += 2) {
          let n = slots[i];
          let v = slots[i + 1];
          if (this.document.frames[n].id == "evokes" ||
              this.document.frames[n].id == "is") {
            let avm = this.BuildAVM(v, rendered);
            contents.appendChild(avm);
          }
        }
      }
    } else {
      let avm = this.BuildAVM(fidx, {});
      contents.appendChild(avm);
    }
    panel.appendChild(contents);
    return panel;
  }

  AddPanel(phrase, fidx) {
    var panel = this.panels[fidx];
    if (panel == undefined) {
      panel = this.BuildPanel(phrase, fidx);
      document.getElementById("panels" + this.docno()).appendChild(panel);
      this.panels[fidx] = panel;
    }
    panel.scrollIntoView();
  }

  OpenPanel(e) {
    e.stopPropagation();
    let span = e.currentTarget;
    let phrase = span.getAttribute("phrase");
    let fidx = parseInt(span.getAttribute("frame"));
    if (phrase) {
      this.AddPanel('"' + phrase + '"', fidx);
    } else {
      this.AddPanel(null, fidx);
    }
  }

  ClosePanel(e) {
    let pid = e.currentTarget.getAttribute("panel");
    let panel =  document.getElementById(pid);
    delete this.panels[panel.getAttribute("frame")];
    document.getElementById("panels" + this.docno()).removeChild(panel);
  }

  BuildChip(fidx) {
    let name = this.FrameName(fidx);
    let chip = document.createElement("span");
    chip.className = "chip";
    chip.id = "t" + fidx;
    chip.setAttribute("frame", fidx);
    chip.appendChild(document.createTextNode(name));

    return chip;
  }

  AddChip(fidx) {
    let chip = this.BuildChip(fidx);
    document.getElementById("themes" + this.docno()).appendChild(chip);
    chip.addEventListener('click', this.OpenPanel.bind(this), false);
    chip.addEventListener('mouseenter', this.EnterChip.bind(this), false);
    chip.addEventListener('mouseleave', this.LeaveChip.bind(this), false);
  }

  AddCallout(span) {
    let callout = document.createElement("span");
    callout.className = "callout";

    let notch = document.createElement("img");
    notch.className = "notch";
    notch.setAttribute("src", notchgif);
    callout.appendChild(notch);

    let bbox = span.getBoundingClientRect();
    callout.style.left = (bbox.right + 15) + "px";
    callout.style.top = ((bbox.top + bbox.bottom) / 2 - 30)  + "px";

    let fidx = parseInt(span.getAttribute("frame"))
    let mention = this.document.frames[fidx];
    let rendered = {};
    let slots = mention.slots;
    if (slots) {
      for (let i = 0; i < slots.length; i += 2) {
        let n = slots[i];
        let v = slots[i + 1];
        if (this.document.frames[n].id == "evokes") {
          let avm = this.BuildAVM(v, rendered);
          callout.appendChild(avm);
        }
      }
    }

    span.appendChild(callout);
    return span;
  }

  RemoveCallout(span) {
    for (let i = 0; i < span.childNodes.length; ++i) {
      let child = span.childNodes[i];
      if (child.className == "callout") {
        span.removeChild(child);
        break;
      }
    }
  }

  GetAVMs(fidx) {
    let matches = null;
    let elements = document.getElementsByClassName("tfs");
    for (let i = 0; i < elements.length; ++i) {
      let e = elements[i];
      let frame = e.getAttribute("frame");
      if (frame == fidx) {
        if (matches == null) matches = [];
        matches.push(e);
      }
    }
    return matches;
  }

  GetBoxes(fidx) {
    let matches = null;
    let elements = document.getElementsByClassName("boxed");
    for (let i = 0; i < elements.length; ++i) {
      let e = elements[i];
      let frame = e.getAttribute("frame");
      if (frame == fidx) {
        if (matches == null) matches = [];
        matches.push(e);
      }
    }
    return matches;
  }

  EvokedFrames(midx) {
    let mention = this.document.frames[midx];
    let evoked = new Set();
    for (let s = 0; s < mention.slots.length; s += 2) {
      let value = mention.slots[s + 1];
      if (typeof value == "number") evoked.add(value);
    }
    return evoked;
  }

  Mentions(evoked) {
    let mentions = new Set();
    for (let fidx of evoked) {
      let frame = this.document.frames[fidx];
      if (frame.mentions) {
        for (let m = 0; m < frame.mentions.length; ++m) {
          mentions.add(frame.mentions[m]);
        }
      }
    }
    return mentions;
  }

  HighlightMentions(mentions) {
    for (let idx of mentions) {
      let span = document.getElementById('s' + this.docno() + '-' + idx);
      span.style.backgroundColor = '#FFFFFF';
      span.style.borderColor = '#FFFFFF';
      span.style.boxShadow = '2px 2px 9px 1px rgba(0,0,0,0.5)';
      this.highlighted.push(span);
    }
  }

  HighlightFrames(evoked) {
    for (let fidx of evoked) {
      let avms = this.GetAVMs(fidx);
      if (avms) {
        for (let i = 0; i < avms.length; ++i) {
          let avm = avms[i];
          avm.style.backgroundColor = '#D0D0D0';
          this.highlighted.push(avm);
        }
      }

      let boxes = this.GetBoxes(fidx);
      if (boxes) {
        for (let i = 0; i < boxes.length; ++i) {
          let box = boxes[i];
          box.style.backgroundColor = '#D0D0D0';
          this.highlighted.push(box);
        }
      }
    }
  }

  LabelMentionedRoles(fidx) {
    let frame = this.document.frames[fidx];
    for (let i = 0; i < frame.slots.length; i += 2) {
      let n = frame.slots[i];
      let v = frame.slots[i + 1];
      if (typeof v == "number") {
        let role = this.FrameName(n);
        let mentions = this.Mentions(new Set([v]));
        for (let idx of mentions) {
          let span = document.getElementById('s' + this.docno() + '-' + idx);
          let label = document.createElement("span");
          label.className = "label";
          label.appendChild(document.createTextNode(role + ':'));
          span.insertBefore(label, span.firstElementChild);
          this.labeled.push(span);
        }
      }
    }
  }

  ClearHighlight() {
    if (this.highlighted) {
      for (let i = 0; i < this.highlighted.length; ++i) {
        this.highlighted[i].removeAttribute("style");
      }
      this.highlighted = null;
    }
    if (this.labeled) {
      for (let i = 0; i < this.labeled.length; ++i) {
        let span = this.labeled[i];
        for (let j = 0; j < span.childNodes.length; ++j) {
          let child = span.childNodes[j];
          if (child.className == "label") span.removeChild(child);
        }
      }
      this.labeled = null;
    }
  }

  EnterSpan(e) {
    if (e.shiftKey) {
      if (this.active_callout) this.RemoveCallout(this.active_callout);
      this.active_callout = this.AddCallout(e.currentTarget);
    } else {
      this.ClearHighlight();
      let span = e.currentTarget;
      let midx = parseInt(span.getAttribute("frame"));

      this.highlighted = [];
      this.labeled = [];
      let evoked = this.EvokedFrames(midx);
      this.HighlightFrames(evoked);
      let corefs = this.Mentions(evoked);
      this.HighlightMentions(corefs);
      for (let fidx of evoked) {
        this.LabelMentionedRoles(fidx);
      }
    }
  }

  LeaveSpan(e) {
    this.RemoveCallout(e.currentTarget);
    this.active_callout = null;
    this.ClearHighlight();
  }

  EnterChip(e) {
    this.ClearHighlight();
    let chip = e.currentTarget;
    let fidx = parseInt(chip.getAttribute("frame"));

    this.highlighted = [];
    this.labeled = [];
    this.HighlightFrames([fidx]);
    this.LabelMentionedRoles(fidx);
  }

  LeaveChip(e) {
    this.ClearHighlight();
  }

  EnterBox(e) {
    if (e.shiftKey) return;
    this.ClearHighlight();
    let box = e.currentTarget;
    let fidx = parseInt(box.getAttribute("frame"));

    this.highlighted = [];
    this.labeled = [];
    let evoked = new Set([fidx]);
    this.HighlightFrames(evoked);
    let corefs = this.Mentions(evoked);
    this.HighlightMentions(corefs);
    this.LabelMentionedRoles(fidx);
  }

  LeaveBox(e) {
    if (e.shiftKey) return;
    this.ClearHighlight();
  }

  ClickBox(e) {
    let box = e.currentTarget;
    let collapsed = box.getAttribute("collapsed") == 1;
    let fidx = parseInt(box.getAttribute("frame"));
    let parent = box.parentElement
    let avm = parent.nextSibling
    if (!avm) return;

    this.ClearHighlight();
    if (collapsed) {
      avm.parentNode.replaceChild(this.BuildAVM(fidx, {}), avm);
      box.setAttribute("collapsed", 0);
    } else {
      avm.parentNode.replaceChild(this.BuildCollapsedAVM(fidx), avm);
      box.setAttribute("collapsed", 1);
    }
  }

  static stylesheet() {
    return `
      @import url('https://fonts.googleapis.com/css?family=Lato|Lora:400,400i,700,700i');

      $ {
        display: flex;
        flex-direction: row;
        width: 100%;
        height: 100%;
      }

      .doctext {
        flex: 1;
        height: 100%;
        overflow-y: auto;
        padding: 5px 5px 5px 10px;
        background: white;
        border: 2px solid #cccccc;
        box-sizing: border-box;
        font: 13pt lora, georgia, serif;
      }

      .docspacer {
        flex: 0 0 10px;
      }

      .docpanels {
        flex: 1;
        height: 100%;
        overflow-y: auto;
      }

      .docref {
        font-size: 14pt;
        color: #888888;
        margin-left: 10px;
      }

      .title {
        font: 24pt lato, helvetica, sans-serif;
        margin-top: 10px;
        margin-bottom: 20px;
      }

      .titlelink {
        color: black;
        text-decoration: none;
      }

      .titlelink:link {
        color: black;
        text-decoration: none;
      }

      .titlelink:visited {
        color: black;
        text-decoration: none;
      }

      .topiclink {
        color: #888888;
        text-decoration: none;
      }

      .topiclink:link {
        color: #888888;
        text-decoration: none;
      }

      .topiclink:visited {
        color: #888888;
        text-decoration: none;
      }

      $ a {
        color: black;
        font-weight: normal;
      }

      $ a:link {
        text-decoration: underline;
      }

      $ a:visited {
        text-decoration: underline;
      }

      $ h2 {
        font: 16pt lato, helvetica, sans-serif;
        margin-top: 20px;
        margin-bottom: 10px;
      }

      $ ul {
        font-size: 13pt;
      }

      $ blockquote {
        font: 12pt lora, georgia, serif;
        line-height: normal;
        letter-spacing: normal;
      }

      .themes {
        margin-top: 10px;
      }

      .panel {
        margin: 0px 5px 10px 5px;
        padding: 5px;
        box-shadow: 1px 1px 1px 0px #808080;
        background: white;
      }

      .panel-titlebar {
        text-align: left;
        font:  bold 11pt arial;
        padding: 3px;
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
        user-select: none;
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
        display: inline-block;
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
        cursor: pointer;
      }

      .boxed {
        border: 1px solid black;
        text-align: center;
        font-size: 7pt;
        line-height: 1;
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
        border: 1px solid #e0e0e0;
        border-right: 0px;
        width: 5px;
        height: 100%;
      }

      .tfs:after {
        content: "";
        position: absolute;
        right: -5px;
        top: 0;
        border: 1px solid #e0e0e0;
        border-left: 0px;
        width: 5px;
        height: 100%;
      }

      .tfs td {
        font-family: arial;
        font-size: 10pt;
        text-align: left;
        white-space: nowrap;
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
        font-weight: bold;
        font-style: italic;
        color: #909090;
      }

      .tfs-collapsed:after {
        content: " ...";
        color: #909090;
        font-weight: bold;
        font-style: italic;
      }

      .label {
        color: white;
        font-size: 8pt;
        line-height: 90%;
        white-space: nowrap;
        overflow: hidden;
        background: rgba(120,120,120,0.75);
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
        word-wrap: break-word;
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
        word-wrap: break-word;
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
        word-wrap: break-word;
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

      a[tooltip]:hover:before, span[tooltip]:hover:before {
        content: attr(tooltip);
        font:  11pt arial;
        position: absolute;
        color: #fff;
        padding: 8px;
        border-radius: 5px;
        z-index: 1;
        margin-top: 25px;
        background: #666;
        white-space: pre-wrap;
      }
    `;
  }
}

Component.register(DocumentViewer);

