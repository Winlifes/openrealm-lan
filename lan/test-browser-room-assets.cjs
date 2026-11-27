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

test('prepares effects, audio and current-map controls while leaving unrelated assets compressed', async () => {
  const bytes = new Uint8Array(1024).fill(42);
  const packed = {body:gzipSync(bytes), size:bytes.byteLength};
  const lateEffect = 'effects/cast.mdx';
  const mapButton = 'replaceabletextures/commandbuttons/btnunit.dds';
  const disabledMapButton = 'replaceabletextures/commandbuttonsdisabled/disbtnunit.dds';
  const unrelatedButton = 'replaceabletextures/commandbuttons/btnunseen.dds';
  const firstSelectionBadge = 'ui/widgets/console/human/infocard-neutral-attack-siege.dds';
  const tooltipIcon = 'ui/widgets/tooltips/human/tooltipgoldicon.dds';
  const menuButton = 'ui/widgets/escmenu/human/human-options-button-border-down.dds';
  const sharedButton = 'replaceabletextures/commandbuttons/btnselectheroon.dds';
  const archive = new Map([
    [lateEffect, packed],
    ['sound/cast.ogg', packed],
    [mapButton, packed],
    [disabledMapButton, packed],
    [unrelatedButton, packed],
    [firstSelectionBadge, packed],
    [tooltipIcon, packed],
    [menuButton, packed],
    [sharedButton, packed],
    ['units/unseen.mdx', packed]
  ]);
  await assetsHelper.expandPreloads(archive, new Set([lateEffect]), new Set([mapButton, disabledMapButton, 'units/unseen.mdx']));
  for (const path of [lateEffect, 'sound/cast.ogg', mapButton, disabledMapButton, firstSelectionBadge, tooltipIcon, menuButton, sharedButton])
    assert.deepEqual(archive.get(path), bytes, path);
  assert.equal(archive.get('units/unseen.mdx'), packed);
  assert.equal(archive.get(unrelatedButton), packed);
});
