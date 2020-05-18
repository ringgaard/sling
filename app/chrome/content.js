chrome.runtime.onMessage.addListener(function (msg, sender, sendResponse) {
  if (msg.text === 'analyze_page') {
    let content = document.all[0].outerHTML;
    let page = {
      url: document.URL,
      headers: {
        'Content-Type': document.contentType,
        'Content-Length': content.length,
        'Last-Modified': new Date(document.lastModified).toUTCString()
      },
      body: content
    };
    console.log("page:", page);
    sendResponse(page);
  }
});

