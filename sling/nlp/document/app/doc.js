import {Component, h, render} from "/common/external/preact.js";
import {Layout, TextField, Grid, Card} from "/common/lib/mdl.js";

class Document {
  constructor(data) {
    this.data = data;
    this.text = data.text
    this.tokens = data.tokens
    this.frames = data.frames
    this.mentions = data.mentions
    this.themes = data.themes
  }
}

class App extends Component {
  constructor(props) {
    super(props);
    this.state = { document: null };
  }

  search(e) {
    var docid = e.target.value
    if (docid) {
      var url = "/fetch?docid=" + docid + "&fmt=cjson";
      var self = this;
      fetch(url)
        .then(response => {
          if (response.ok) {
            return response.json();
          } else {
            console.log("fetch error", response.status, response.message);
            return null;
          }
        })
        .then(response => {
          self.setState({document: new Document(response)});
        });
    }
  }

  render(props, state) {
    return (
      h("div", {id: "app"},
        h(Layout, null,
          h(Layout.Header, null,
            h(Layout.HeaderRow, null,
              h(Layout.Title, null, "Corpus browser"),
              h(Layout.Spacer),
              h(TextField, {
                id: "docid",
                placeholder: "Document ID",
                type: "search",
                onsearch: e => this.search(e),
                style: "background-color:#FFF; color:#000; padding:10px;"}),
            ),
          ),
          h(Layout.Drawer, null, h(Layout.Title, null, "Menu")),
          h(Layout.DrawerButton),
          h(Layout.Content, {id: "main"},
            h(Main, {document: state.document})
          )
        )
      )
    );
  }
}

class Main extends Component {
  render(props) {
    return (
      h(Grid, null,
        h(Card, {"class": "card"},
          h(Card.Title, null,
            h(Card.TitleText, null, "Analysis")
          ),
          h(Card.Text, null,
            h("pre", null, "DOC:", JSON.stringify(props.document, null, "  ")),
          ),
        )
      )
    );
  }
}

render(h(App), document.body);

