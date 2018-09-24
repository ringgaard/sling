import { Component, h, render } from '/common/external/preact.js';
import '/common/external/material.js';
import { Button } from '/common/external/preact-mdl.js';

class App extends Component {
	componentDidMount() {
		this.setState({ message:'Hello!' });
	}
	render(props, state) {
		return (
			h('div', {id:'app'},
				h(Header, { message: state.message }),
				h(Main),
				h(MyButton)
			)
		);
	}
}

const Header = (props) => {
	return h('header', null,
		h('h1', null, 'App'),
		props.message && h('h2', null, props.message)
	);
};

class Main extends Component {
	render() {
		const items = [1,2,3,4,5].map( (item) => (
			h('li', {id:item}, 'Item '+item)
		));
		return (
			h('main', null,
				h('ul', null, items)
			)
		);
	}
}

class MyButton extends Component {
  render() {
    return(
      h("div", {}, h(Button, {}, "I am button!"))
    )
  }
}

render(h(App), document.body);
