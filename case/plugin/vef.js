// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for VEF postings.

import {store, frame} from "/common/lib/global.js";

const n_is = store.is;
const n_name = frame("name");
const n_description = frame("description");
const n_media = frame("media");
const n_instance_of = frame("P31");
const n_human = frame("Q5");
const n_gender = frame("P21");
const n_female = frame("Q6581072");
const n_date_of_birth = frame("P569");
const n_country_of_citizenship = frame("P27");
const n_denmark = frame("Q35");
const n_featured_in = frame("PFEIN");
const n_page9 = frame("t/1215/18");
const n_point_in_time = frame("P585");
const n_described_at_url = frame("P973");

const postpat = /http:\/\/vintage-erotica-forum\.com\/showpost.php\?p=(\d+)/
const p9 = true;

export default class VEFPlugin {
  async process(action, query, context) {
    let url = new URL(query);
    console.log(url.search);
    let m = url.search.match(/^\?p=(\d+)/);
    if (!m) return;
    let postingid = m[1];
    if (!postingid) return;

    return {
      ref: postingid,
      name: postingid,
      description: "VEF posting",
      url: query,
      context: context,
      onitem: item => this.extract(item),
    };
  }

  async extract(item) {
    // Retrieve VEF posting.
    let context = item.context;
    let r = await context.fetch(item.url);
    let html = await r.text();

    // Parse HTML.
    let doc = new DOMParser().parseFromString(html, "text/html");
    let container = doc.querySelector("td.alt1");
    let parts = container.querySelectorAll("div");
    let title = parts.item(0).innerText.trim();
    let body = parts.item(1);

    // Get month from title.
    let date = new Date(title);
    let year = date.getFullYear();
    let month = date.getMonth() + 1;

    // Extract parts from posts.
    let day = 0;
    let text = "";
    let brk = false;
    let name = null;
    let age = null;
    let source = null;
    let topic = null;
    for (let e of body.childNodes) {
      if (e.nodeType == Node.TEXT_NODE) {
        if (brk) text = text.trim() + "\n";
        brk = false;
        text += e.textContent;
        name = null;
        age = null;
        topic = null;
      } else if (e.nodeType == Node.ELEMENT_NODE) {
        if (e.tagName == "BR") {
          brk = true;
        } else if (e.tagName == "B") {
          if (brk) text = text.trim() + "\n";
          brk = false;
          text += e.innerText;
        } else if (e.tagName == "A") {
          if (e.href.startsWith("https://www.imagevenue.com/")) {
            // Continuation.
            let m = text.match(/^,?\s*and\s+/);
            if (m) text = text.slice(m[0].length);

            // Track date.
            for (;;) {
              let m = text.match(/^(\d+)(st|nd|rd|th)\s*,?/)
              if (!m) break;
              day = parseInt(m[1]);
              text = text.slice(m[0].length).trim()
            }
            text = text.trim();
            let date = year * 10000 + month * 100 + day;

            // Parse name and age.
            m = text.match(/^([A-Za-z\- ]+)\s*(\(\d+\))?/)
            if (m) {
              name = m[1];
              age = m[2] && parseInt(m[2].slice(1, m[2].length - 1));
              text = text.slice(m[0].length).trim();
            }
            if (text.startsWith("(IM)")) text = text.slice(4).trim();
            if (text.startsWith("(M)")) text = text.slice(3).trim();

            // Create new topic.
            if (!topic) {
              topic = await context.new_topic();
              if (name) topic.put(n_name, name);
              if (p9) topic.put(n_description, "Side 9 pige");
              if (text) topic.put(n_description, text);
              topic.put(n_instance_of, n_human);
              topic.put(n_gender, n_female);
              if (age) topic.put(n_date_of_birth, year - age);
              if (p9) topic.put(n_country_of_citizenship, n_denmark);
              if (p9) {
                let f = store.frame();
                f.add(n_is, n_page9);
                f.add(n_point_in_time, date);
                topic.put(n_featured_in, f);
              }
              if (source) topic.put(n_described_at_url, source);
              context.updated(topic);
            }

            // Add photo.
            let img = e.querySelector("img");
            if (img) {
              let thumb = img.src;
              let base = thumb.slice(thumb.lastIndexOf("/") + 1);
              let hires = base.replace("_t", "_o");
              let d = MD5(hires);
              let path = `${d[0]}${d[1]}/${d[2]}${d[3]}/${d[4]}${d[5]}`
              let photo = "https://cdn-images.imagevenue.com/" +
                          path + "/" + hires;
              console.log("link", date, name, age, text, photo);
              topic.add(n_media, "!" + photo);
            }

            text = "";
            brk = false;
            source = null;
          } else {
            if (e.href.match(postpat)) source = e.href;
            text += e.innerText;
          }
        } else {
          console.log("element", e);
        }
      }
    }
  }
}

function MD5(inputString) {
  const hc = '0123456789abcdef';
  const rh = n => {let j,s='';for(j=0;j<=3;j++) s+=hc.charAt((n>>(j*8+4))&0x0F)+hc.charAt((n>>(j*8))&0x0F);return s;}
  const ad = (x,y) => {let l=(x&0xFFFF)+(y&0xFFFF);let m=(x>>16)+(y>>16)+(l>>16);return (m<<16)|(l&0xFFFF);}
  const rl = (n,c) => (n<<c)|(n>>>(32-c));
  const cm = (q,a,b,x,s,t) => ad(rl(ad(ad(a,q),ad(x,t)),s),b);
  const ff = (a,b,c,d,x,s,t) => cm((b&c)|((~b)&d),a,b,x,s,t);
  const gg = (a,b,c,d,x,s,t) => cm((b&d)|(c&(~d)),a,b,x,s,t);
  const hh = (a,b,c,d,x,s,t) => cm(b^c^d,a,b,x,s,t);
  const ii = (a,b,c,d,x,s,t) => cm(c^(b|(~d)),a,b,x,s,t);
  const sb = x => {
    let i;const nblk=((x.length+8)>>6)+1;const blks=[];for(i=0;i<nblk*16;i++) { blks[i]=0 };
    for(i=0;i<x.length;i++) {blks[i>>2]|=x.charCodeAt(i)<<((i%4)*8);}
    blks[i>>2]|=0x80<<((i%4)*8);blks[nblk*16-2]=x.length*8;return blks;
  }
  let i,x=sb(inputString),a=1732584193,b=-271733879,c=-1732584194,d=271733878,olda,oldb,oldc,oldd;
  for(i=0;i<x.length;i+=16) {olda=a;oldb=b;oldc=c;oldd=d;
    a=ff(a,b,c,d,x[i+ 0], 7, -680876936);d=ff(d,a,b,c,x[i+ 1],12, -389564586);c=ff(c,d,a,b,x[i+ 2],17,  606105819);
    b=ff(b,c,d,a,x[i+ 3],22,-1044525330);a=ff(a,b,c,d,x[i+ 4], 7, -176418897);d=ff(d,a,b,c,x[i+ 5],12, 1200080426);
    c=ff(c,d,a,b,x[i+ 6],17,-1473231341);b=ff(b,c,d,a,x[i+ 7],22,  -45705983);a=ff(a,b,c,d,x[i+ 8], 7, 1770035416);
    d=ff(d,a,b,c,x[i+ 9],12,-1958414417);c=ff(c,d,a,b,x[i+10],17,     -42063);b=ff(b,c,d,a,x[i+11],22,-1990404162);
    a=ff(a,b,c,d,x[i+12], 7, 1804603682);d=ff(d,a,b,c,x[i+13],12,  -40341101);c=ff(c,d,a,b,x[i+14],17,-1502002290);
    b=ff(b,c,d,a,x[i+15],22, 1236535329);a=gg(a,b,c,d,x[i+ 1], 5, -165796510);d=gg(d,a,b,c,x[i+ 6], 9,-1069501632);
    c=gg(c,d,a,b,x[i+11],14,  643717713);b=gg(b,c,d,a,x[i+ 0],20, -373897302);a=gg(a,b,c,d,x[i+ 5], 5, -701558691);
    d=gg(d,a,b,c,x[i+10], 9,   38016083);c=gg(c,d,a,b,x[i+15],14, -660478335);b=gg(b,c,d,a,x[i+ 4],20, -405537848);
    a=gg(a,b,c,d,x[i+ 9], 5,  568446438);d=gg(d,a,b,c,x[i+14], 9,-1019803690);c=gg(c,d,a,b,x[i+ 3],14, -187363961);
    b=gg(b,c,d,a,x[i+ 8],20, 1163531501);a=gg(a,b,c,d,x[i+13], 5,-1444681467);d=gg(d,a,b,c,x[i+ 2], 9,  -51403784);
    c=gg(c,d,a,b,x[i+ 7],14, 1735328473);b=gg(b,c,d,a,x[i+12],20,-1926607734);a=hh(a,b,c,d,x[i+ 5], 4,    -378558);
    d=hh(d,a,b,c,x[i+ 8],11,-2022574463);c=hh(c,d,a,b,x[i+11],16, 1839030562);b=hh(b,c,d,a,x[i+14],23,  -35309556);
    a=hh(a,b,c,d,x[i+ 1], 4,-1530992060);d=hh(d,a,b,c,x[i+ 4],11, 1272893353);c=hh(c,d,a,b,x[i+ 7],16, -155497632);
    b=hh(b,c,d,a,x[i+10],23,-1094730640);a=hh(a,b,c,d,x[i+13], 4,  681279174);d=hh(d,a,b,c,x[i+ 0],11, -358537222);
    c=hh(c,d,a,b,x[i+ 3],16, -722521979);b=hh(b,c,d,a,x[i+ 6],23,   76029189);a=hh(a,b,c,d,x[i+ 9], 4, -640364487);
    d=hh(d,a,b,c,x[i+12],11, -421815835);c=hh(c,d,a,b,x[i+15],16,  530742520);b=hh(b,c,d,a,x[i+ 2],23, -995338651);
    a=ii(a,b,c,d,x[i+ 0], 6, -198630844);d=ii(d,a,b,c,x[i+ 7],10, 1126891415);c=ii(c,d,a,b,x[i+14],15,-1416354905);
    b=ii(b,c,d,a,x[i+ 5],21,  -57434055);a=ii(a,b,c,d,x[i+12], 6, 1700485571);d=ii(d,a,b,c,x[i+ 3],10,-1894986606);
    c=ii(c,d,a,b,x[i+10],15,   -1051523);b=ii(b,c,d,a,x[i+ 1],21,-2054922799);a=ii(a,b,c,d,x[i+ 8], 6, 1873313359);
    d=ii(d,a,b,c,x[i+15],10,  -30611744);c=ii(c,d,a,b,x[i+ 6],15,-1560198380);b=ii(b,c,d,a,x[i+13],21, 1309151649);
    a=ii(a,b,c,d,x[i+ 4], 6, -145523070);d=ii(d,a,b,c,x[i+11],10,-1120210379);c=ii(c,d,a,b,x[i+ 2],15,  718787259);
    b=ii(b,c,d,a,x[i+ 9],21, -343485551);a=ad(a,olda);b=ad(b,oldb);c=ad(c,oldc);d=ad(d,oldd);
  }
  return rh(a)+rh(b)+rh(c)+rh(d);
}

