// Copyright 2023 Ringgaard Research ApS
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Archive scanner for Epson DS-780N.

#include <time.h>
#include <string>

#include "sling/base/types.h"
#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/net/http-server.h"
#include "sling/net/static-content.h"
#include "sling/net/web-sockets.h"
#include "sling/string/printf.h"
#include "sling/string/text.h"
#include "sling/util/json.h"
#include "sling/util/threadpool.h"

#include "epsonds.h"

DEFINE_string(host, "", "HTTP server host address");
DEFINE_int32(port, 8080, "HTTP server port");
DEFINE_string(output_dir, ".", "Output directory for scanned documents");
DEFINE_string(output_url, "file:", "URL for accessing scanned documents");

using namespace sling;

// Output files
static const char *page_file = "/tmp/scan/page-%d-%03d.jpg";
static const char *archive_command =
  "./archive-scan.sh %s %s '%s/%s' /tmp/scan/page-%d-*.jpg";

static const char *app_page =
R"""(<!DOCTYPE html>
<head>
  <meta charset="utf-8">
  <meta name=viewport content="width=device-width, initial-scale=1">
  <title>Archive scanner</title>
  <link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
  <script type="module" src="scanner.js"></script>
</head>
<body style="display: none">
  <scanner-app id="app">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div id="title">Epson DS-780N archive scanner</div>
      <md-spacer></md-spacer>
      <md-icon-button id="scan" icon="play_circle"></md-icon-button>
    </md-toolbar>

    <md-content>
      <md-row-layout>
        <md-card id="docs">
          <md-card-toolbar>
            <div>Documents</div>
          </md-card-toolbar>
          <document-list id="doclist"></document-list>
        </md-card>

        <md-card id="settings">
          <md-card-toolbar>
            <div>Settings</div>
          </md-card-toolbar>

          <div class="group">Density:</div>
          <md-radio-button
            value="150DPI"
            name="density"
            label="150 dpi">
          </md-radio-button>
          <md-radio-button
            value="300DPI"
            name="density"
            label="300 dpi"
            selected=1>
          </md-radio-button>
          <md-radio-button
            value="600DPI"
            name="density"
            label="600 dpi">
          </md-radio-button>


          <div class="group">Depth:</div>
          <md-radio-button
            value="MONO"
            name="depth"
            label="1 bit monochrome"
            selected=1>
          </md-radio-button>
          <md-radio-button
            value="GRAY"
            name="depth"
            label="8-bit grayscale">
          </md-radio-button>
          <md-radio-button
            value="RGB"
            name="depth"
            label="24-bit RGB color">
          </md-radio-button>

          <div class="group">Paper size:</div>
          <md-radio-button
            value="A4"
            name="size"
            label="A4"
            selected=1>
          </md-radio-button>
          <md-radio-button
            value="A5"
            name="size"
            label="A5">
          </md-radio-button>

          <div class="group">Duplex:</div>
          <md-radio-button
            value="SINGLE"
            name="duplex"
            label="single-sided"
            selected=1>
          </md-radio-button>
          <md-radio-button
            value="DOUBLE"
            name="duplex"
            label="two-sided">
          </md-radio-button>

          <div class="group">Orientation:</div>
          <md-radio-button
            value="PORTRAIT"
            name="orientation"
            label="Portrait"
            selected=1>
          </md-radio-button>
          <md-radio-button
            value="LANDSCAPE"
            name="orientation"
            label="Landscape">
          </md-radio-button>

        </md-card>

      </md-row-layout>
    </md-content>
  </scanner-app-app>
</body>
</html>
)""";

static const char *jsapp =
R"""(
import {Component} from "/common/lib/component.js";
import {MdApp} from "/common/lib/material.js";

class ScannerApp extends MdApp {
  onconnected() {
    this.docs = new Array();
    this.attach(this.onchange, "change", "#settings");
    this.attach(this.onscan, "click", "#scan");
    this.connect();
  }

  connect() {
    this.socket = new WebSocket("ws://" + location.host + "/connect");
    this.socket.addEventListener("message", e => this.onrecv(e));
    this.socket.addEventListener("error", e => this.onerror(e));
    this.socket.addEventListener("close", e => this.onclose(e));
    return new Promise((resolve, reject) => {
      this.socket.addEventListener("open", e => {
        resolve(this);
      });
      this.socket.addEventListener("error", e => {
        reject("Error connecting to server " + url);
      });
    });
  }

  disconnect() {
    this.socket = null;
  }

  onchange(e) {
    let options = new Array();
    for (let r of this.querySelectorAll("input")) {
      if (r.checked) options.push(r.value);
    }
    this.socket.send("CONFIG " + options.join(" "));
  }

  async onscan(e) {
    if (!this.socket) await this.connect();
    this.socket.send("SCAN");
  }

  async onrecv(e) {
    console.log("onrecv", e);
    let msg = JSON.parse(await e.data.text());
    console.log("notify", msg);
    this.docs.push(msg);
    this.find("#doclist").update(this.docs);
  }

  onerror(e) {
    console.log("onerror", e);
  }

  onclose(e) {
    this.sock
    console.log("onclose", e);
  }

  static stylesheet() {
    return `
      $ #docs {
        width: 100%;
      }
      $ div.group {
        font-weight: bold;
        padding: 8px 0px 4px 0px;
      }
      $ label {
        white-space: nowrap;
    `;
  }
}

Component.register(ScannerApp);

class DocumentList extends Component {
  visible() { return this.state; }

  render() {
    let h = "";
    for (let doc of this.state) {
      h += "<div>"
      h += `<a href="${doc.url}">${doc.document}</a>`;
      if (doc.pages > 1) h += ` (${doc.pages} pages)`;
      h += "</div>";
    }
    return h;
  }
}

Component.register(DocumentList);

document.body.style = null;

)""";

class ScannerService {
 public:
  class Client : public WebSocket {
   public:
    Client(ScannerService *service, SocketConnection *conn)
      : WebSocket(conn), service_(service) {}

    ~Client() {
      service_->Disconnect();
    }

    void Receive(const uint8 *data, uint64 size, bool binary) override {
      service_->Receive(data, size);
    }

   private:
    ScannerService *service_;
  };

  ScannerService() {
    workerpool_.StartWorkers();
    serial_ = time(0);
  }

  // Register scanner service.
  void Register(HTTPServer *http) {
    http->Register("/", this, &ScannerService::HandleHome);
    http->Register("/scanner.js", this, &ScannerService::HandleScript);
    http->Register("/connect", this, &ScannerService::HandleConnect);
    common_.Register(http);
  }

  void HandleHome(HTTPRequest *request, HTTPResponse *response) {
    response->set_content_type("text/html");
    response->Append(app_page);
  }

  void HandleScript(HTTPRequest *request, HTTPResponse *response) {
    response->set_content_type("text/javascript");
    response->Append(jsapp);
  }

  void HandleConnect(HTTPRequest *request, HTTPResponse *response) {
    if (client_) delete client_;
    client_ = new Client(this, request->conn());
    if (!WebSocket::Upgrade(client_, request, response)) {
      delete client_;
      client_ = nullptr;
      response->SendError(404);
      return;
    }
    LOG(INFO) << "websock connected";
  }

  void Receive(const uint8 *data, uint64 size) {
    Text cmd(reinterpret_cast<const char *>(data), size);
    LOG(INFO) << "Sock recv: " << cmd;
    auto args = cmd.split(' ');
    if (args.size() == 0) return;
    if (args[0] == "SCAN") {
      Scan();
    } else if (args[0] == "CONFIG") {
      for (int i = 1; i < args.size(); ++i) {
        Text param = args[i];
        if (param == "150DPI") {
          scan_dpi = 150;
        } else if (param == "300DPI") {
          scan_dpi = 300;
        } else if (param == "600DPI") {
          scan_dpi = 600;
        } else if (param == "MONO") {
          scan_color = COLOR_MONO;
        } else if (param == "GRAY") {
          scan_color = COLOR_GRAY;
        } else if (param == "RGB") {
          scan_color = COLOR_RGB;
        } else if (param == "A4") {
          paper_size = A4;
        } else if (param == "A5") {
          paper_size = A5;
        } else if (param == "SINGLE") {
          duplex = 0;
        } else if (param == "DOUBLE") {
          duplex = 1;
        } else if (param == "PORTRAIT") {
          orientation = PORTRAIT;
        } else if (param == "LANDSCAPE") {
          orientation = LANDSCAPE;
        } else {
          LOG(ERROR) << "Unknown config param: " << param;
        }
      }
    }
  }

  void Disconnect() {
    LOG(INFO) << "websock disconnect";
    client_ = nullptr;
  }

  void Notify(const JSON::Object &message) {
    if (client_) {
      IOBuffer buffer;
      message.Write(&buffer);
      client_->Send(buffer.data());
    }
  }

  void Scan() {
    // Scan document.
    scanner_connect();
    scanner_handshake();
    scanner_lock();
    scanner_para();
    int serial = serial_++;
    int pages = scan_document(page_file, serial);
    scanner_unlock();
    scanner_disconnect();

    // Convert JPGs to PDF in the background.
    if (pages > 0) {
      workerpool_.Schedule([this, serial, pages]() { Convert(serial, pages); });
    }
  }

  void Convert(int serial, int pages) {
    // Generate PDF file name.
    time_t now = time(0);
    struct tm tm;
    localtime_r(&now, &tm);
    string pdffn = StringPrintf("%04d-%02d-%02d Scanning %02d%02d%02d.pdf",
      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
      tm.tm_hour, tm.tm_min, tm.tm_sec);
    LOG(INFO) << "PDF file: " << pdffn;

    // Run archive script.
    const char *color = "?";
    switch (scan_color) {
      case COLOR_MONO: color = "M"; break;
      case COLOR_GRAY: color = "G"; break;
      case COLOR_RGB: color = "C"; break;
    }
    const char *orient = "?";
    switch (orientation) {
      case PORTRAIT: orient = "P"; break;
      case LANDSCAPE: orient = "L"; break;
    }

    string cmd = StringPrintf(archive_command,
                              color, orient,
                              FLAGS_output_dir.c_str(), pdffn.c_str(),
                              serial);
    if (system(cmd.c_str()) < 0) perror("archive");

    JSON::Object msg;
    msg.Add("document", pdffn);
    msg.Add("pages", pages);
    msg.Add("url", FLAGS_output_url + "/" + pdffn);
    Notify(msg);
  }

 private:
  // Common JS libraries.
  StaticContent common_{"/common", "app"};

  // Only a single client is supported.
  Client *client_ = nullptr;

  // Next serial number for scan files.
  int serial_ = 0;

  // Worker pool for converting scanned documents.
  ThreadPool workerpool_{5, 100};
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  SocketServerOptions options;
  HTTPServer http(options, FLAGS_host.c_str(), FLAGS_port);
  CHECK(http.Start(false));

  // Initialize scanner service.
  ScannerService service;
  service.Register(&http);

  CHECK(http.Start(true));
  LOG(INFO) << "HTTP server running";
  http.Wait();
  LOG(INFO) << "HTTP server done";

  return 0;
}

