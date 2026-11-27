'use strict';

const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {test} = require('node:test');
const vm = require('node:vm');
const {gzipSync} = require('node:zlib');

const source = readFileSync(require.resolve('./browser/wc3-room-assets.js'), 'utf8');
const assetsHelper = vm.runInNewContext(source + '\nwc3RoomAssets', {
  DecompressionStream, ReadableStream, Response, Uint8Array
});

test('prepares selected effects and audio before admission while leaving other assets compressed', async () => {
  const bytes = new Uint8Array(1024).fill(42);
  const packed = {body:gzipSync(bytes), size:bytes.byteLength};
  const lateEffect = 'effects/cast.mdx';
  const archive = new Map([
    [lateEffect, packed],
    ['sound/cast.ogg', packed],
    ['units/unseen.mdx', packed]
  ]);
  await assetsHelper.expandPreloads(archive, new Set([lateEffect]));
  assert.deepEqual(archive.get(lateEffect), bytes);
  assert.deepEqual(archive.get('sound/cast.ogg'), bytes);
  assert.equal(archive.get('units/unseen.mdx'), packed);
});
