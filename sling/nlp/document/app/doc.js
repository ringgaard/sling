import {Component, h, render} from '/common/external/preact.js';
import {Layout, Button, Icon, Card, Menu, TextField, Grid} from '/common/external/preact-mdl.js';

class App extends Component {
  constructor(props) {
    super(props);
    this.state = { document: null };
  }

  search(e) {
    var docid = e.target.value
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
        self.setState({document: response});
      });
  }

  render(props, state) {
    console.log("render app", state);
    return (
      h('div', {id: 'app'},
        h(Layout, {"fixed-header": true},
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
              h(Button, {id: "header-menu", icon: true},
                h(Icon, {icon: "more_vert"})
              )
            ),
            h(Menu, {"for": "header-menu", "bottom-right": true})
          ),
          h(Layout.Drawer),
          h(Main, {document: state.document})
        )
      )
    );
  }
}

class Main extends Component {
  render(props, state) {
    var doc = props.document;
    var docstr = JSON.stringify(doc);
    console.log("main document", docstr);
    return (
      h(Layout.Content, {id: "main", doc: doc},
        h(Grid, {doc: doc},
          h(Card, {"class": "card"},
            h("form", null,
              h(TextField, {multiline: true}, "Enter text to analyze...")
            )
          ),
          h(Card, {"class": "card", doc: doc},
            h(Card.Title, null,
              h(Card.TitleText, null, "Analysis")
            ),
            h(Card.Text, {doc: doc},
              h("pre", null, "DOC:", docstr),
            ),
          )
        )
      )
    );
  }
}

render(h(App), document.body);
