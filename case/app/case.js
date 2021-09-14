// Case-based knowledge management app.

import {Component} from "/common/lib/component.js";
import {StdDialog} from "/common/lib/material.js";

const kbservice = "https://ringgaard.com/kb"

//-----------------------------------------------------------------------------
// App
//-----------------------------------------------------------------------------

class CaseApp extends Component {
  onconnected() {
  }
}

Component.register(CaseApp);

//-----------------------------------------------------------------------------
// Case search box
//-----------------------------------------------------------------------------

class CaseSearchBox extends Component {
  onconnected() {
    this.bind("md-search", "query", e => this.onquery(e));
    this.bind("md-search", "item", e => this.onitem(e));
  }

  onquery(e) {
    let detail = e.detail
    let target = e.target;
    let params = "fmt=cjson";
    let query = detail.trim();
    if (query.endsWith(".")) {
      params += "&fullmatch=1";
      query = query.slice(0, -1);
    }

    let app = this.match("#app");
    fetch(kbservice + "/query?" + params + "&q=" + encodeURIComponent(query))
      .then(response => response.json())
      .then((data) => {
        let items = [];
        for (let item of data.matches) {
          let elem = document.createElement("md-search-item");
          elem.setAttribute("name", item.text);
          elem.setAttribute("value", item.ref);

          let title = document.createElement("span");
          title.className = "item-title";
          title.appendChild(document.createTextNode(item.text));
          elem.appendChild(title);

          if (item.description) {
            let desciption = document.createElement("span");
            desciption.className = "item-description";
            desciption.appendChild(document.createTextNode(item.description));
            elem.appendChild(desciption);
          }

          items.push(elem);
        }
        target.populate(detail, items);
      })
      .catch(error => {
        console.log("Query error", query, error.message, error.stack);
        StdDialog.error(error.message);
        target.populate(detail, null);
      });
  }

  onitem(e) {
    let id = e.detail;
    console.log("selected", id)
  }

  query() {
    return this.find("md-search").query();
  }

  clear() {
    return this.find("md-search").clear();
  }

  render() {
    return `
      <form>
        <md-search
          placeholder="Search for case or topic..."
          min-length=2
          autofocus>
        </md-search>
      </form>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        width: 100%;
        max-width: 800px;
        padding-left: 10px;
        padding-right: 3px;
      }

      $ form {
        display: flex;
        width: 100%;
      }

      $ .item-title {
        font-weight: bold;
        display: block;
        padding: 2px 10px 2px 10px;
      }

      $ .item-description {
        display: block;
        padding: 0px 10px 0px 10px;
      }
    `;
  }
}

Component.register(CaseSearchBox);

//-----------------------------------------------------------------------------
// HTML template
//-----------------------------------------------------------------------------

document.body.innerHTML = `
<case-app id="app">
  <md-column-layout class="desktop">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div>Case Manager</div>
      <case-search-box id="search"></kb-search-box>
    </md-toolbar>

    <md-content>
    </md-content>
  </md-column-layout>
</kb-app>
`;

