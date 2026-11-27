'use strict';

const wc3RoomAssets = (() => {
  async function readBinary(response, file, progress = () => {}) {
    if (!Number.isSafeInteger(file?.bytes) || file.bytes < 1 || !response.body)
      throw new Error('Missing download size or response body');
    const bytes = new Uint8Array(file.bytes);
    const reader = response.body.getReader();
    let offset = 0;
    try {
      for (;;) {
        const {done, value} = await reader.read();
        if (done) break;
        if (value.byteLength > bytes.byteLength - offset) throw new Error('Download exceeded its expected size');
        bytes.set(value, offset);
        offset += value.byteLength;
        if (offset < bytes.byteLength) progress(offset);
      }
    } catch (error) {
      void reader.cancel().catch(() => {});
      throw error;
    } finally {
      reader.releaseLock();
    }
    if (offset !== bytes.byteLength) throw new Error('Incomplete download');
    if (file.sha256 && globalThis.crypto?.subtle) {
      const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
      const hash = Array.from(digest, byte => byte.toString(16).padStart(2, '0')).join('');
      if (hash !== file.sha256) throw new Error('Download checksum mismatch');
    }
    progress(offset);
    return bytes;
  }

  async function openStartupCache(root, storage = globalThis.caches) {
    if (!globalThis.crypto?.subtle) return null;
    try {
      const cache = await storage?.open('openrealm-room-assets-v1');
      if (!cache) return null;
      const key = name => new URL('play/.room-cache/' + encodeURIComponent(name), root).href;
      const hashHeader = 'X-OpenRealm-Asset-SHA256';
      return {
        async read(name, file) {
          if (!file?.sha256) return null;
          try {
            const response = await cache.match(key(name));
            if (response?.headers.get(hashHeader) === file.sha256)
              return await readBinary(response, file);
          } catch {
            // Cache access and integrity failures both fall back to the network.
          }
          return null;
        },
        async write(name, file, bytes) {
          if (!file?.sha256) return;
          try {
            // A new revision overwrites the same logical file, so old versions
            // do not add up and unchanged files are shared across releases.
            await cache.put(key(name), new Response(bytes, {headers:{[hashHeader]:file.sha256}}));
          } catch {
            // The room still works when the browser has no available storage.
          }
        }
      };
    } catch {
      return null;
    }
  }

  function unpack(bytes, paths) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const assets = new Map();
    let offset = 0;
    for (const path of paths) {
      if (offset + 8 > bytes.byteLength || assets.has(path)) throw new Error('Invalid asset bundle');
      const stored = view.getUint32(offset, true);
      const size = view.getUint32(offset + 4, true);
      offset += 8;
      if (!stored || stored > size || stored > bytes.byteLength - offset) throw new Error('Invalid bundled asset size');
      // Each unopened entry owns its compressed bytes after the download is released.
      assets.set(path, {body:bytes.slice(offset, offset + stored), size});
      offset += stored;
    }
    if (offset !== bytes.byteLength) throw new Error('Unexpected data in asset bundle');
    return assets;
  }

  function inflate({body, size}) {
    // Copy an uncompressed entry before mounting so it does not retain its bundle.
    if (body.byteLength === size) return body.slice();
    const compressed = new ReadableStream({start(controller) { controller.enqueue(body); controller.close(); }});
    const expanded = compressed.pipeThrough(new DecompressionStream('gzip'));
    return readBinary(new Response(expanded), {bytes:size});
  }

  function isAudio(path) {
    return /\.(wav|ogg|mp3|flac)$/i.test(path);
  }

  async function expandPreloads(assets, readyArt) {
    for (const [path, asset] of assets) {
      if ((isAudio(path) || readyArt.has(path)) && !(asset instanceof Uint8Array))
        assets.set(path, await inflate(asset));
    }
  }

  return {readBinary, openStartupCache, unpack, inflate, expandPreloads};
})();
