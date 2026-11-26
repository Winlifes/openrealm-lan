const assert = require('node:assert/strict');
const {createHash} = require('node:crypto');
const {readFileSync} = require('node:fs');
const {runInThisContext} = require('node:vm');
const {gzipSync} = require('node:zlib');
const test = require('node:test');
const source = readFileSync(__dirname + '/wc3-room-assets.js', 'utf8');
const {readBinary, openStartupCache, unpack, inflate, retain, expandAudio} = runInThisContext(source + '\nwc3RoomAssets');

test('startup downloads report progress and reject incomplete or corrupted files', async () => {
  const manifest = {bytes:4, sha256:createHash('sha256').update('abcd').digest('hex')};
  const progress = [];
  const body = new ReadableStream({start(controller) {
    controller.enqueue(Buffer.from('ab'));
    controller.enqueue(Buffer.from('cd'));
    controller.close();
  }});
  assert.equal(Buffer.from(await readBinary(new Response(body), manifest, n => progress.push(n))).toString(), 'abcd');
  assert.deepEqual(progress, [2, 4]);
  await assert.rejects(readBinary(new Response('abc'), manifest), /Incomplete/);
  await assert.rejects(readBinary(new Response('abcde'), manifest), /expected size/);
  await assert.rejects(readBinary(new Response('abce'), manifest), /checksum/);
});

test('startup assets survive a new room and replace only the changed logical file', async () => {
  const stored = new Map();
  const cache = {
    async match(key) { return stored.get(key)?.clone(); },
    async put(key, response) { stored.set(key, response); }
  };
  const storage = {async open() { return cache; }};
  const describe = bytes => ({bytes:bytes.length, sha256:createHash('sha256').update(bytes).digest('hex')});
  const map = Buffer.from('map data');
  const revised = Buffer.from('new map data');
  const mapKey = 'maps/frozenthrone/example.w3x';
  const scope = new URL('https://public.example/wc3/');
  const first = await openStartupCache(scope, storage);
  await first.write(mapKey, describe(map), map);
  const next = await openStartupCache(scope, storage);
  assert.deepEqual(Buffer.from(await next.read(mapKey, describe(map))), map);
  assert.equal(await next.read(mapKey, describe(revised)), null);
  await next.write(mapKey, describe(revised), revised);
  assert.equal(stored.size, 1);
  assert.deepEqual(Buffer.from(await first.read(mapKey, describe(revised))), revised);
  const [key] = stored.keys();
  stored.set(key, new Response(Buffer.alloc(revised.length), {headers:stored.get(key).headers}));
  assert.equal(await next.read(mapKey, describe(revised)), null);
});

test('unavailable storage remains an ordinary cache miss', async () => {
  assert.equal(await openStartupCache('https://lan.example/', {
    async open() { throw new Error('Storage blocked'); }
  }), null);
  const access = await openStartupCache('https://lan.example/', {async open() {
    return {
      async match() { throw new Error('Storage was cleared'); },
      async put() { throw new Error('Out of quota'); }
    };
  }});
  const bytes = Buffer.from('engine');
  const file = {bytes:bytes.length, sha256:createHash('sha256').update(bytes).digest('hex')};
  assert.equal(await access.read('openwarcraft3.wasm', file), null);
  await access.write('openwarcraft3.wasm', file, bytes);
});

test('startup bundles expand only the requested asset and detect invalid entries', async () => {
  function record(data, compressed) {
    const body = compressed ? gzipSync(data) : data;
    const header = Buffer.alloc(8);
    header.writeUInt32LE(body.length);
    header.writeUInt32LE(data.length, 4);
    return Buffer.concat([header, body]);
  }
  const terrain = Buffer.alloc(128, 1);
  const model = Buffer.from('model');
  const bundle = new Uint8Array(Buffer.concat([record(terrain, true), record(model, false)]));
  const paths = ['terrain.dds', 'unit.mdx'];
  const assets = unpack(bundle, paths);
  assert.ok(assets.get(paths[0]).body.byteLength < terrain.byteLength);
  assert.deepEqual(Buffer.from(await inflate(assets.get(paths[0]))), terrain);
  const expanded = inflate(assets.get(paths[1]));
  assert.deepEqual(Buffer.from(expanded), model);
  assert.ok(expanded.buffer !== bundle.buffer);
  assert.throws(() => unpack(bundle.subarray(0, -1), paths), /Invalid bundled asset size/);
  assert.throws(() => unpack(bundle, [paths[0], paths[0]]), /Invalid asset bundle/);
  assert.throws(() => unpack(bundle, paths.slice(0, 1)), /Unexpected data/);
  retain(assets, new Set([paths[0]]));
  assert.deepEqual([...assets.keys()], [paths[0]]);
  assert.notEqual(assets.get(paths[0]).body.buffer, bundle.buffer);
  assert.deepEqual(Buffer.from(await inflate(assets.get(paths[0]))), terrain);
});

test('room audio is synchronously readable after resource preparation', async () => {
  const clip = Buffer.alloc(128, 4);
  const packed = {body:gzipSync(clip), size:clip.length};
  const audio = 'units/test/attack.ogg';
  const model = 'units/test/unit.mdx';
  const assets = new Map([[audio, packed], [model, packed]]);
  await expandAudio(assets);
  assert.ok(assets.get(audio) instanceof Uint8Array);
  assert.deepEqual(Buffer.from(assets.get(audio)), clip);
  assert.strictEqual(assets.get(model), packed);
  retain(assets);
  assert.deepEqual(Buffer.from(assets.get(audio)), clip);
});
