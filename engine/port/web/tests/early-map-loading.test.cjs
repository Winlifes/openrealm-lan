/* Browser-only placeholder display, no game assets. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {makeEarlyMapLoading} = require('../site/early-map-loading.js');
test('placeholder waits for the authored frame and post-registration progress', () => {
  const frames = [];
  const old = globalThis.requestAnimationFrame;
  globalThis.requestAnimationFrame = fn => { frames.push(fn); };
  const node = {hidden: true};
  const show = makeEarlyMapLoading({getElementById: id => id === 'early-map-loading' ? node : null});
  try {
    show(true, 0, false);
    assert.equal(node.hidden, false);
    show(true, 0.4, false); // Progress alone is insufficient.
    assert.equal(node.hidden, false);
    show(true, 0.39, true); // An early layout is also insufficient.
    assert.equal(node.hidden, false);
    show(true, 0.4, true);
    assert.equal(node.hidden, false);
    frames.shift()();
    frames.shift()();
    assert.equal(node.hidden, true);
  } finally { globalThis.requestAnimationFrame = old; }
});
test('a progress element stays visible through the first match frame', () => {
  const frames = [];
  const old = globalThis.requestAnimationFrame;
  globalThis.requestAnimationFrame = fn => { frames.push(fn); };
  const overlay = {hidden: true};
  const bar = {value: 0.5, removeAttribute(key) { delete this[key]; }};
  const show = makeEarlyMapLoading({getElementById: id => id === 'early-map-loading' ? overlay : bar});
  try {
    show(true, 0, false);
    assert.equal(overlay.hidden, false);
    assert.equal(bar.value, undefined);
    for (const value of [0.1, 0.4, 0.75, 1]) {
      show(true, value, true);
      assert.equal(overlay.hidden, false);
      assert.equal(bar.value, value);
    }
    show(false, 1, true);
    frames.shift()();
    frames.shift()();
    assert.equal(overlay.hidden, true);
  } finally { globalThis.requestAnimationFrame = old; }
});
test('missing placeholder is harmless', () => {
  assert.doesNotThrow(() => makeEarlyMapLoading({getElementById: () => null})(true, 0, false));
});
