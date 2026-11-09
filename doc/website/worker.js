// SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
// SPDX-License-Identifier: GPL-3.0-only
function notFound()
{
    return new Response("not found", { status: 404 });
}

function insert(tree, parts, obj) {
  const name = parts[0];
  if (parts.length === 1) {
    tree[name] = { name, size: obj.size };
  } else {
    const dir = (tree[name] ??= { name, children: {} });
    insert(dir.children, parts.slice(1), obj);
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname !== "/releases") return notFound();

    const cache = caches.default;
    const cacheKey = new Request(url.origin + url.pathname);
    const hit = await cache.match(cacheKey);
    if (hit) return hit;

    const tree = {};
    let cursor;
    do {
      const page = await env.BUCKET.list({
        cursor,
      });
      for (const obj of page.objects) {
        const parts = obj.key.split("/").filter(Boolean);
        if (parts.length) insert(tree, parts, obj);
      }
      cursor = page.truncated ? page.cursor : undefined;
    } while (cursor);

    const headers = new Headers();
    headers.set("content-type", "text/html; charset=utf-8");
    headers.set("cache-control", "public, max-age=300");

    const res = new Response(JSON.stringify(tree), { headers });
    await cache.put(cacheKey, res.clone());
    return res;
  },
};
