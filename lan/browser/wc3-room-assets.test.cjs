const assert = require('node:assert/strict');
const {createHash} = require('node:crypto');
const {readFileSync} = require('node:fs');
const {runInThisContext} = require('node:vm');
const {gzipSync} = require('node:zlib');
const test = require('node:test');
const source = readFileSync(__dirname + '/wc3-room-assets.js', 'utf8');
const {readBinary, unpack, inflate} = runInThisContext(source + '\nwc3RoomAssets');

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
});
