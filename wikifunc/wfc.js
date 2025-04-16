// WikiFunctions compiler front-end.

import * as http from 'node:http';
import * as wikifunc from './wikifunc.js';

// Get command-line argumnents.
let port = 8080;
let annotate = false;
for (let i = 1; i < process.argv.length - 1; ++i) {
  if (process.argv[i] == "--port") {
    port = JSON.parse(process.argv[++i]);
  } else if (process.argv[i] == "--annotate") {
    annotate = JSON.parse(process.argv[++i]);
  }
}

const main_page = `
<html>
<head>
<title>WikiFunctions compiler</title>
</head>
<body>

<script>
async function oncompile() {
  try {
    let zid = document.getElementById("zid").value;
    let r = await fetch("/wfc/compile/" + zid);
    let code = await r.json();
    let info = [];
    if (code.name) info.push("name: " + code.name);
    if (code.label) info.push("label: " + code.label);
    if (code.description) info.push("description: " + code.description);
    document.getElementById("info").innerHTML = info.join("\\n");
    let source = 'function "' + code.name + '"(';
    source += code.args.join(",") + ') {\\n\\n';
    source += code.body;
    source += '\\n}\\n';
    document.getElementById("code").innerHTML = source;
  } catch (e) {
    document.getElementById("info").innerHTML = e.toString();
    document.getElementById("code").innerHTML = "";
  }
}
</script>

<h1>WikiFunctions compiler</h1>
<label for="zid">ZID:</label>
<input id="zid" value="Z13728">
<button id="compile" onclick="oncompile()">Compile</button>
<br>
<pre id="info">
</pre>
<pre id="code" style="background: #f8f8f8;">
</pre>
</body>
</html>
`;

class Frontend {
  constructor() {
    this.wiki = new wikifunc.WikiFunctions({annotate});
  }

  async handle(req, res) {
    try {
      if (req.url.startsWith("/wfc/compile/")) {
        await this.compile(req, res);
      } else if (req.url.startsWith("/wfc/item/")) {
        await this.item(req, res);
      } else if (req.headers["content-type"] == "application/json") {
        await this.execute(req, res);
      } else if (req.url.startsWith("/wfc")) {
        res.writeHead(200, {'Content-Type': 'text/html'});
        res.end(main_page);
      } else if (req.url == "/favicon.ico") {
        res.writeHead(402);
      } else {
        console.log("unknown", req.url, req.headers);
        res.writeHead(400);
      }
    } catch (error) {
      console.log("error", req.url, error);
      res.writeHead(500).end();
    }
  }

  async compile(req, res) {
    let zid = req.url.slice(13);
    let code = await this.wiki.compile(zid);
    res.writeHead(200, {'Content-Type': 'application/json'});
    res.end(JSON.stringify(code));
  }

  async item(req, res) {
    let zid = req.url.slice(10);
    let item = await this.wiki.item(zid);
    res.writeHead(200, {'Content-Type': 'application/json'});
    res.end(JSON.stringify(item.json));
  }

  async execute(req, res) {
    let chunks = [];
    req.on('data', chunk => chunks.push(chunk));
    req.on('end', async () => {
      try {
        let request = JSON.parse(Buffer.concat(chunks).toString());

        let compile_start =  performance.now();
        let func = await this.wiki.func(request.func);
        let compile_end =  performance.now();

        let exec_start =  performance.now();
        let retval = func(...request.args);
        let exec_end =  performance.now();

        let response = {
          result: retval,
          compile: compile_end - compile_start,
          exec: exec_end - exec_start,
        };

        res.writeHead(200, {'Content-Type': 'application/json'});
        res.end(JSON.stringify(response));
      } catch (error) {
        console.log("error", req.url, error);
        res.writeHead(500).end();
      }
    });
  }
}

let frontend = new Frontend();

let server = http.createServer(frontend.handle.bind(frontend));
server.listen(port, '');
console.log('WikiFunctions server running on port', port);
