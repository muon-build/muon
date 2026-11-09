// SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
// SPDX-License-Identifier: GPL-3.0-only
function notFound()
{
    return new Response("not found", { status: 404 });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    const cache = caches.default;
    const cacheKey = new Request(url.origin + url.pathname);
    const hit = await cache.match(cacheKey);
    if (hit) return hit;

    let parts;
    try {
      parts = new URL(request.url).pathname.split("/").filter(Boolean).map(decodeURIComponent);
    } catch {
      return notFound();
    }

    if (parts[0] !== "reference" || parts.length > 2) {
      return notFound();
    }
    const version = parts[1] ?? "edge";

    let obj = await env.BUCKET.get(`${version}/docs/reference.html`, {
      onlyIf: request.headers, // If-None-Match / If-Modified-Since
    });
    if (!obj) return notFound();

    const headers = new Headers();
    obj.writeHttpMetadata(headers);
    headers.set("etag", obj.httpEtag);
    if (!headers.has("content-type")) headers.set("content-type", "text/html; charset=utf-8");
    if (!headers.has("cache-control")) headers.set("cache-control", "public, max-age=300");

    if (!("body" in obj)) return new Response(null, { status: 304, headers });
    const res = new Response(request.method === "HEAD" ? null : obj.body, { headers });
    await cache.put(cacheKey, res.clone());
    return res;
  },
};
