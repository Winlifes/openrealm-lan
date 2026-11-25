'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {join} = require('node:path');
const {runInNewContext} = require('node:vm');

const input = readFileSync(join(__dirname, 'browser/wc3-input.js'), 'utf8');

function target() {
  const listeners = new Map();
  return {
    addEventListener(type, listener) { listeners.set(type, listener); },
    dispatchEvent(event) { return listeners.get(event.type)?.(event); },
    focus() {},
    setAttribute() {},
  };
}

function fixture({touch = false, delayedLock = false, fullscreenFails = false} = {}) {
  const calls = [];
  const document = target();
  const canvas = target();
  const container = target();
  const fullscreen = target();
  const lock = target();
  const status = {textContent:''};
  const elements = new Map([
    ['canvas', canvas], ['game-container', container], ['fullscreen-button', fullscreen],
    ['pointer-lock-button', lock], ['display-status', status],
  ]);
  document.getElementById = id => elements.get(id);
  document.documentElement = {classList:{add() {}}};
  container.classList = {toggle() {}};
  canvas.getBoundingClientRect = () => ({left:0,top:0,width:1280,height:720});
  let activated = true;
  const finishLock = () => {
    document.pointerLockElement = canvas;
    document.dispatchEvent({type:'pointerlockchange'});
  };
  canvas.requestPointerLock = () => {
    assert.ok(activated, 'request the pointer lock before fullscreen consumes activation');
    calls.push('lock');
    if (!delayedLock) finishLock();
  };
  document.exitPointerLock = () => {
    calls.push('unlock');
    document.pointerLockElement = null;
    document.dispatchEvent({type:'pointerlockchange'});
  };
  container.requestFullscreen = () => {
    activated = false;
    calls.push('fullscreen');
    if (fullscreenFails) return Promise.reject(Error('Denied'));
    document.fullscreenElement = container;
    document.dispatchEvent({type:'fullscreenchange'});
  };
  const window = target();
  window.matchMedia = () => ({matches:touch});
  runInNewContext(input, {
    document, window, location:{search:''}, URLSearchParams,
    performance, requestAnimationFrame() {}, setInterval() {}, setTimeout() {}, clearTimeout() {},
    MouseEvent: class { constructor(type) { this.type = type; } },
  });
  return {calls, document, canvas, fullscreen, finishLock, status};
}

test('fullscreen also locks the mouse and Escape releases it without relocking', async () => {
  const {calls, document, canvas, fullscreen, status} = fixture();
  await fullscreen.dispatchEvent({type:'click'});
  assert.deepEqual(calls, ['lock', 'fullscreen']);
  assert.equal(document.pointerLockElement, canvas);
  assert.match(status.textContent, /Esc/);
  document.dispatchEvent({type:'keydown', key:'Escape', preventDefault() {}, stopImmediatePropagation() {}});
  assert.deepEqual(calls, ['lock', 'fullscreen', 'unlock']);
  assert.equal(document.pointerLockElement, null);
});

test('a cancelled fullscreen request releases its late pointer lock', async () => {
  const {calls, document, fullscreen, finishLock, status} = fixture({delayedLock:true, fullscreenFails:true});
  await fullscreen.dispatchEvent({type:'click'});
  finishLock();
  assert.deepEqual(calls, ['lock', 'fullscreen', 'unlock']);
  assert.equal(document.pointerLockElement, null);
  assert.match(status.textContent, /无法切换全屏/);
});

test('touch fullscreen keeps the ordinary pointer behavior', async () => {
  const {calls, fullscreen} = fixture({touch:true});
  await fullscreen.dispatchEvent({type:'click'});
  assert.deepEqual(calls, ['fullscreen']);
});
