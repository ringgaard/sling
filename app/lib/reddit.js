// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Reddit utility library.

function traverse(obj, path) {
  if (obj == undefined || obj == null) return undefined;
  for (let p of path) {
    if (p in obj) {
      obj = obj[p];
    } else {
      return undefined;
    }
  }
  return obj;
}

function media(posting) {
  let media_id = traverse(posting, ["gallery_data", "items", 0, "media_id"]);
  if (media_id) {
    let media = traverse(posting, ["media_metadata", media_id, "p", 0]);
    if (media) {
      return {url: media["u"], width: media["x"], height: media["y"]};
    }
  }
  return undefined;
}

function preview(posting) {
  let preview = traverse(posting, ["preview", "images", 0, "resolutions", 0]);
  if (preview) {
    return {
      url: preview["url"],
      width: preview["width"],
      height: preview["height"]
    };
  }
  return undefined;
}

function thumbnail(posting) {
  // Get thumbnail.
  let url = posting["thumbnail"];
  if (url != null && url != "" && url != "nsfw" && url != "default") {
    return {
      url: url,
      width: posting["thumbnail_width"],
      height: posting["thumbnail_height"]
    };
  }

  return undefined;
}

function thumbsearch(posting) {
  // Get thumbnail.
  let thumb = thumbnail(posting);

  // Get preview.
  if (!thumb) {
    thumb = preview(posting);
  }

  // Get first media.
  if (!thumb) {
    thumb = media(posting);
  }

  // Get cross-posting.
  if (!thumb) {
    let xpost = traverse(posting, ["crosspost_parent_list", 0]);
    if (xpost) {
      thumb = thumbsearch(xpost);
    }
  }

  return thumb;
}

export function reddit_thumbnail(posting, size) {
  // Search for thumbnail from posting.
  let thumb = thumbsearch(posting);
  if (thumb) {
    // Unescape url.
    thumb.url = thumb.url.replaceAll("&amp;", "&");

    // Scale to size.
    if (size && thumb.width && thumb.height) {
      thumb.height = Math.round(size * thumb.height / thumb.width);
      thumb.width = size;
    }
  } else {
    thumb = {url: "data:,", width: size, height: size};
  }

  return thumb;
}

