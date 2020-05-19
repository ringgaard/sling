function analyze(page) {
  console = chrome.extension.getBackgroundPage().console;

  console.log("page:", page);

  console.log("url:", page.url);
  console.log("headers:", page.headers);
  //for (var hdr of page.headers) {
  //   console.log(hdr[0]+ ': '+ hdr[1]);
  //}
  console.log("body:", page.body);

  //console.log("Content-Length:", page.body.length);
  //console.log("Last-Modified:", new Date(page.lastModified).toUTCString());
  //console.log("body:", page.body);
};

chrome.contextMenus.onClicked.addListener(function(info, tab) {
  if (info.menuItemId == "analyze") {
    console = chrome.extension.getBackgroundPage().console;

    // Try to fetch page directly.
    fetch(tab.url).then((response) => {
      if (response.ok) {
        analyze({
          url: tab.url,
          headers: response.headers,
          body: response.text()
        });
      } else {
        chrome.tabs.sendMessage(tab.id, {text: 'analyze_page'}, analyze);
      }
    })
    .catch((error) => {
      // Get the page content from the document DOM.
      console.error('Error fetching url:', error);
      chrome.tabs.sendMessage(tab.id, {text: 'analyze_page'}, analyze);
    });
  }
});

chrome.contextMenus.create({
  id: "analyze",
  title: "Analyze with SLING",
  contexts:["all"]
});

