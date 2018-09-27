import {Component, h, render} from '/common/external/preact.js';
import {Layout, Button, Icon, Card, Menu, TextField, Grid} from '/common/external/preact-mdl.js';

class App extends Component {
  render(props, state) {
    return (
      h('div', {id: 'app'},
        h(Layout, {"fixed-header": true},
          h(Header),
          h(Sidebar),
          h(Main)
        )
      )
    );
  }
}

class Sidebar extends Component {
  render(props, state) {
    return (
      h(Layout.Drawer)
    );
  }
}

class Header extends Component {
  render(props, state) {
    return (
      h(Layout.Header, null,
        h(Layout.HeaderRow, null,
          h(Layout.Title, null, "Document analyzer"),
          h(Layout.Spacer),
          h(Button, {id: "header-menu", icon: true},
            h(Icon, {icon: "more_vert"})
          )
        ),
        h(Menu, {"for": "header-menu", "bottom-right": true},
          h(Menu.Item, null, "Long longer item 1"),
          h(Menu.Item, null, "Item 2"),
          h(Menu.Item, null, "Item 3")
        )
      )
    );
  }
}

class Main extends Component {
  render() {
    return (
      h(Layout.Content, {id: "main"},
        h(Grid, null,
          h(Card, {"class": "card"},
            h("form", null,
              h(TextField, {multiline: true}, "Enter text to analyze...")
            )
          ),
          h(Card, {"class": "card"},
            h(Card.Title, null,
              h(Card.TitleText, null, "Analysis")
            ),
            h(Card.Text, null, "Card text"),
          )
        )
      )
    );
  }
}

render(h(App), document.body);
