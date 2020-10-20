// Material Design web components.

import {Component, stylesheet} from "/common/lib/component.js";

stylesheet(`
@import url(https://fonts.googleapis.com/css?family=Roboto:400,400italic,500,500italic,700,700italic,900,900italic,300italic,300,100italic,100);

@font-face {
  font-family: 'Material Icons';
  font-style: normal;
  font-weight: 400;
  src: url(https://fonts.gstatic.com/s/materialicons/v55/flUhRq6tzZclQEJ-Vdg-IuiaDsNc.woff2) format('woff2');
}

html {
  width: 100%;
  height: 100%;
  min-height: 100%;
  position:relative;
}

body {
  font-family: Roboto,Helvetica,sans-serif;
  font-size: 14px;
  font-weight: 400;
  line-height: 20px;
  padding: 0;
  margin: 0;
  box-sizing: border-box;

  width: 100%;
  height: 100%;
  min-height: 100%;
  position:relative;
}
`);

export class MdColumnLayout extends Component {
  static stylesheet() {
    return `
      md-column-layout {
        display: flex;
        flex-direction: column;
        margin: 0;
        height: 100%;
        min-height: 100%;
      }
    `
  }
}

Component.register(MdColumnLayout);

export class MdRowLayout extends Component {
  static stylesheet() {
    return `
      md-row-layout {
        display: flex;
        flex-direction: row;
        margin: 0;
        width: 100%;
        min-width: 100%;
      }
    `
  }
}

Component.register(MdRowLayout);

export class MdToolbar extends Component {
  static stylesheet() {
    return `
      md-toolbar {
        display: flex;
        flex-direction: row;
        align-items: center;
        background-color: #00A0D6;
        color: rgb(255,255,255);
        height: 56px;
        max-height: 56px;
        font-size: 20px;
        padding: 0px 16px;
        margin: 0;
        box-shadow: 0 1px 8px 0 rgba(0,0,0,.2),
                    0 3px 4px 0 rgba(0,0,0,.14),
                    0 3px 3px -2px rgba(0,0,0,.12);
        z-index: 2;
      }
    `
  }
}

Component.register(MdToolbar);


export class MdContent extends Component {
  static stylesheet() {
    return `
      md-content {
        flex: 1;
        padding: 8px;
        display: block;
        overflow: auto;
        color: rgb(0,0,0);
        background-color: rgb(250,250,250);

        position:relative;

        flex-basis: 0%;
        flex-grow: 1;
        flex-shrink: 1;
      }
    `
  }
}

Component.register(MdContent);

export class MdIconButton extends Component {
  render() {
    return this.html`
      <button ${this.props.disabled ? "disabled" : ""}>
        <i>${this.props.icon}</i>
      </button>`;
  }

  disable() {
    if (!this.props.disabled) {
      this.props.disabled = true;
      this.update();
    }
  }

  enable() {
    if (this.props.disabled) {
      this.props.disabled = false;
      this.update();
    }
  }

  static stylesheet() {
    return `
      md-icon-button button {
        border-radius: 50%;
        border: 0;
        height: 40px;
        width: 40px;
        margin: 0 6px;
        padding: 8px;
        background: transparent;
        user-select: none;
        cursor: pointer;
      }

      md-icon-button button:hover:enabled {
        background-color: rgba(0,0,0,0.07);
      }

      md-icon-button button:disabled {
        color: rgba(0,0,0,0.38);
        cursor: default;
      }

      md-toolbar md-icon-button button {
        color: rgb(255,255,255);
      }

      md-toolbar md-icon-button button:first-child {
        margin-left: -8px;
      }

      md-icon-button button:focus {
        outline: none;
      }

      md-icon-button button i {
        font-family: 'Material Icons';
        font-weight: normal;
        font-style: normal;
        font-size: 24px;
        line-height: 1;
        letter-spacing: normal;
        text-transform: none;
        display: inline-block;
        white-space: nowrap;
        word-wrap: normal;
        direction: ltr;
      }
    `
  }
}

Component.register(MdIconButton);

export class MdIcon extends Component {
  render() {
    return `<i>${this.props.icon}</i>`;
  }

  static stylesheet() {
    return `
      md-icon i {
        font-family: 'Material Icons';
        font-weight: normal;
        font-style: normal;
        font-size: 24px;
        line-height: 1;
        letter-spacing: normal;
        text-transform: none;
        display: inline-block;
        white-space: nowrap;
        word-wrap: normal;
        direction: ltr;
      }
    `
  }
}

Component.register(MdIcon);

export class MdRadioButton extends Component {
  render() {
    return `
      <input type="radio"
             name="${this.props.name}"
             value="${this.props.value}"
             ${this.props.selected ? "checked" : ""}>`;
  }

  static stylesheet() {
    return `
      md-radio-button {
        display: flex;
        height: 30px;
        width: 30px;
        border-radius: 50%;
      }

      md-radio-button:hover {
        background-color: rgba(0,0,0,0.07);
      }

      md-radio-button input {
        height: 15px;
        width: 15px;
        margin: 8px;
        background: transparent;
        user-select: none;
        cursor: pointer;
      }
    `
  }
}

Component.register(MdRadioButton);

export class MdSpacer extends Component {
  static stylesheet() {
    return `
      md-spacer {
        display: block;
        flex: 1;
      }
    `
  }
}

Component.register(MdSpacer);

export class MdDataField extends Component {}

Component.register(MdDataField);

export class MdDataTable extends Component {
  constructor() {
    super();
    this.fields = [];
    for (const e of this.elements) {
      if (e instanceof MdDataField) {
        this.fields.push({
          name: e.props.field,
          header: e.innerHTML,
          style: e.style ? e.style.cssText : null,
          escape: !e.props.html,
        });
      }
    }
  }

  render() {
    let out = [];
    out.push("<table><thead><tr>");
    for (const fld of this.fields) {
      if (fld.style) {
        out.push(`<th style="${fld.style}">`);
      } else {
        out.push("<th>");
      }
      out.push(fld.header);
      out.push("</th>");
    }
    out.push("</tr></thead><tbody>");

    if (this.state) {
      for (const row of this.state) {
        out.push("<tr>");
        for (const fld of this.fields) {
          if (fld.style) {
            out.push(`<td style="${fld.style}">`);
          } else {
            out.push("<td>");
          }

          let value = row[fld.name];
          if (value == undefined) value = "";
          value = value.toString();

          if (fld.escape) value = Component.escape(value);
          out.push(value);
          out.push("</td>");
        }
        out.push("</tr>");
      }
    }

    out.push("</tbody></table>");

    return out.join("");
  }

  static stylesheet() {
    return `
      md-data-table {
        border: 0;
        border-collapse: collapse;
        white-space: nowrap;
        font-size: 14px;
        text-align: left;
      }

      md-data-table thead {
        padding-bottom: 3px;
      }

      md-data-table th {
        vertical-align: bottom;
        padding: 8px 12px;
        box-sizing: border-box;
        border-bottom: 1px solid rgba(0,0,0,.12);
        text-overflow: ellipsis;
        color: rgba(0,0,0,.54);
      }

      md-data-table td {
        vertical-align: middle;
        border-bottom: 1px solid rgba(0,0,0,.12);
        padding: 8px 12px;
        box-sizing: border-box;
        text-overflow: ellipsis;
      }

      md-data-table td:first-of-type, md-data-table th:first-of-type {
        padding-left: 24px;
      }
    `
  }
}

Component.register(MdDataTable);

export class MdCard extends Component {
  static stylesheet() {
    return `
      md-card {
        display: block;
        background-color: rgb(255, 255, 255);
        box-shadow: rgba(0, 0, 0, 0.16) 0px 2px 4px 0px,
                    rgba(0, 0, 0, 0.23) 0px 2px 4px 0px;
        margin: 10px 5px;
        padding: 10px;
    `
  }
}

Component.register(MdCard);

