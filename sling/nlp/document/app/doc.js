import {Component, h, render} from '/common/external/preact.js';
import {Layout, Button, Icon, Card, Menu} from '/common/external/preact-mdl.js';

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

/*
h(Menu, {"top-right": true},
  h(Menu.Item, null, "Item 1"),
  h(Menu.Item, null, "Item 2"),
  h(Menu.Item, null, "Item 3")
)
*/

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
          h(Layout.Title, null, "Document analyzer")
        ),
      )
    );
  }
}

class Main extends Component {
  render() {
    return (
      h(Layout.Content, {id: "main"},
        h(Card, {class: "skinny", shadow: 4},
          h(Card.Title, null,
            h(Icon, {icon: "info", style: "float:left; margin-top:20px"}),
            h(Card.TitleText, null, "Documentation")
          ),
          h(Card.Text)
        )
      )
    );
  }
}

render(h(App), document.body);
