import {Component, h, render} from "/common/external/preact.js";
import {Layout, Button, Icon, Card} from "/common/lib/mdl.js";
import {Document, DocumentViewer} from "/common/lib/docview.js";
import {stylesheet} from "/common/lib/util.js";

stylesheet("/doc/lex.css");

class DocumentEditor extends Component {
  constructor(props) {
    super(props);
  }

  render(props, state) {
    return (
      h(Card, {class: "doceditor", shadow: 2},
        h("textarea", {class: "editor", oninput: props.oninput}, props.text)
      )
    );
  }
}

class App extends Component {
  constructor(props) {
    super(props);
    this.state = {document: null, editmode: true};
    this.text = "";
  }

  view(e) {
    console.log("view", e);
  }

  edit(e) {
    console.log("edit", e);
  }

  oninput(e) {
    this.text = e.target.value;
  }

  render(props, state) {
    var action, content;
    if (state.editmode) {
      let icon = h(Icon, {icon: "send"});
      action = h(Button, {icon: true, onclick: e => this.view(e)}, icon);
      content = h(DocumentEditor,
                  {text: this.text, oninput: e => this.oninput(e)});
    } else {
      let icon = h(Icon, {icon: "edit"});
      action = h(Button, {icon: true, onclick: e => this.edit(e)}, icon);
      content = h(DocumentViewer, {document: state.document});
    }

    return (
      h("div", {id: "app"},
        h(Layout, null,
          h(Layout.Header, null,
            h(Layout.HeaderRow, null,
              h(Layout.Title, null, "LEX document viewer"),
              h(Layout.Spacer),
              action
            ),
          ),
          h(Layout.Drawer, null, h(Layout.Title, null, "Menu")),
          h(Layout.DrawerButton),

          h(Layout.Content, {id: "main"}, content)
        )
      )
    );
  }
}

render(h(App), document.body);
