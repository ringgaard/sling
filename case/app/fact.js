// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";

class FactEditor extends Component {
  visible() {
    return this.state;
  }

  render() {
    return "Fact editor";
  }
}

Component.register(FactEditor);

class FactStatement extends Component {
}

Component.register(FactStatement);

class FactProperty extends Component {
}

Component.register(FactProperty);

class FactValue extends Component {
}

Component.register(FactValue);

