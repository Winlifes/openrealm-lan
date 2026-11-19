/* Execute the production EM_JS metadata query without a browser or game data. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../../../common/common.c'), 'utf8');
function query(module, filename) {
  const match = source.match(/EM_JS\(int, WC3_RemoteFileExists, [^\n]+\{\n([\s\S]*?)\n\}\);/);
  assert.ok(match, 'remote metadata query must be available to VFS existence probes');
  const decode = pointer => { assert.equal(pointer, 64); return filename; };
  return new Function('Module', 'UTF8ToString', 'filenameC', match[1])(module, decode, 64n);
}
test('remote-only portraits exist before their first lazy read', () => {
  const filename = 'units/human/peasant/peasant_portrait.mdx';
  assert.equal(query({WC3_ASSET_INDEX: new Set([filename])}, 'Units\\Human\\Peasant\\Peasant_Portrait.mdx'), 1);
  assert.equal(query({WC3_ASSET_INDEX: new Set([filename])}, 'Units\\Human\\Peasant\\Missing.mdx'), 0);
  assert.match(source, /bool FS_FileExists\([\s\S]*?return WC3_RemoteFileExists\(fileName\);/);
});
test('local-only browser backends keep missing-file results without a mirror index', () => {
  assert.equal(query({}, 'Units\\Human\\Peasant\\Peasant_Portrait.mdx'), 0);
  assert.equal(query({WC3_ASSET_INDEX: new Set()}, ''), 0);
});
