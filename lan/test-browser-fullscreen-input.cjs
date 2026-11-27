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
    removeEventListener(type, listener) { if (listeners.get(type) === listener) listeners.delete(type); },
    dispatchEvent(event) { return listeners.get(event.type)?.(event); },
    focus() {},
    setAttribute() {},
  };
}

function fixture({touch = false, delayedLock = false, fullscreenFails = false, userAgent = '', speed} = {}) {
  const calls = [];
  const document = target();
  const canvas = target();
  const container = target();
  const fullscreen = target();
  const matchFullscreen = { ...target(), hidden: true };
  const lock = target();
  const speedControl = {...target(), value:'', parentElement:{hidden:true}};
  const status = {textContent:''};
  const elements = new Map([
    ['canvas', canvas], ['game-container', container], ['fullscreen-button', fullscreen],
    ['match-fullscreen-button', matchFullscreen],
    ['pointer-lock-button', lock], ['display-status', status],
    ['pointer-lock-speed', speedControl],
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
    assert.ok(activated, 'fullscreen requires an active user gesture');
    activated = false;
    calls.push('fullscreen');
    if (fullscreenFails) return Promise.reject(Error('Denied'));
    document.fullscreenElement = container;
    document.dispatchEvent({type:'fullscreenchange'});
  };
  document.exitFullscreen = () => {
    calls.push('exit');
    document.fullscreenElement = null;
    document.dispatchEvent({type:'fullscreenchange'});
  };
  const navigator = {userAgent, userActivation:{get isActive() { return activated; }}};
  const window = target();
  window.matchMedia = () => ({matches:touch});
  const storage = new Map(speed ? [['winlife-wc3-pointer-speed', speed]] : []);
  window.localStorage = {getItem:key=>storage.get(key), setItem:(key,value)=>storage.set(key,value)};
  runInNewContext(input, {
    document, window, navigator, location:{search:''}, URLSearchParams,
    performance, requestAnimationFrame() {}, setInterval() {}, setTimeout() {}, clearTimeout() {},
    MouseEvent: class { constructor(type) { this.type = type; } },
  });
  const activate = value => { activated = value; };
  return {calls, document, canvas, fullscreen, matchFullscreen, finishLock, status, window, activate, lock, speedControl, storage};
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

test('an activated match start enters fullscreen once and respects a later exit', async () => {
  const {calls, fullscreen, matchFullscreen, window, activate} = fixture();
  assert.equal(matchFullscreen.hidden, true);
  await window.openrealmEnterMatchFullscreen();
  assert.deepEqual(calls, ['lock', 'fullscreen']);
  assert.equal(matchFullscreen.hidden, true);
  await fullscreen.dispatchEvent({type:'click'});
  activate(true);
  await window.openrealmEnterMatchFullscreen();
  assert.deepEqual(calls, ['lock', 'fullscreen', 'exit', 'unlock']);
  assert.equal(matchFullscreen.hidden, true);
});

test('a remote match start offers a loading-screen button for the next gesture', async () => {
  const {calls, matchFullscreen, window, activate} = fixture();
  activate(false);
  await window.openrealmEnterMatchFullscreen();
  assert.deepEqual(calls, []);
  assert.equal(matchFullscreen.hidden, false);
  activate(true);
  await matchFullscreen.dispatchEvent({type:'click'});
  assert.deepEqual(calls, ['lock', 'fullscreen']);
  assert.equal(matchFullscreen.hidden, true);
});

test('leaving fullscreen before a match keeps the windowed mode at loading', async () => {
  const {calls, fullscreen, matchFullscreen, window, activate} = fixture();
  await fullscreen.dispatchEvent({type:'click'});
  await fullscreen.dispatchEvent({type:'click'});
  activate(true);
  await window.openrealmEnterMatchFullscreen();
  assert.deepEqual(calls, ['lock', 'fullscreen', 'exit', 'unlock']);
  assert.equal(matchFullscreen.hidden, true);
});

test('locked Safari mouse motion keeps fractional deltas and uses the selected speed', async () => {
  const {canvas, document, window, lock, speedControl, storage} = fixture({
    userAgent:'Mozilla/5.0 (Macintosh) AppleWebKit/605.1.15 Version/26.1 Safari/605.1.15',
  });
  const motion = (x,y) => {
    const event = {type:'mousemove', target:canvas, movementX:x, movementY:y};
    window.dispatchEvent(event);
    return [event.movementX,event.movementY];
  };
  assert.equal(speedControl.value,'1.5');
  assert.deepEqual(motion(0.5,-0.5),[0.5,-0.5]);
  await lock.dispatchEvent({type:'click'});
  assert.deepEqual(motion(0.5,-0.5),[0,0]);
  assert.deepEqual(motion(0.5,-0.5),[1,-1]);
  document.exitPointerLock();
  await lock.dispatchEvent({type:'click'});
  assert.deepEqual(motion(0.5,-0.5),[0,0]);
  speedControl.value = '2';
  speedControl.dispatchEvent({type:'change'});
  assert.deepEqual(motion(1,-1),[2,-2]);
  assert.equal(storage.get('winlife-wc3-pointer-speed'),'2');
});

test('the canvas consumes page navigation gestures and active matches warn on tab close', () => {
  const {canvas, window} = fixture();
  function dispatch(target, type, values = {}) {
    const event = {type, defaultPrevented:false, preventDefault() { this.defaultPrevented = true; }, ...values};
    target.dispatchEvent(event);
    return event.defaultPrevented;
  }
  assert.equal(dispatch(canvas,'auxclick',{button:4}),true);
  assert.equal(dispatch(canvas,'mouseup',{button:2}),false);
  assert.equal(dispatch(canvas,'wheel',{deltaX:80,deltaY:2}),true);
  assert.equal(dispatch(window,'beforeunload'),false);
  window.openrealmSetMatchExitGuard(true);
  assert.equal(dispatch(window,'beforeunload'),true);
  window.openrealmSetMatchExitGuard(false);
  assert.equal(dispatch(window,'beforeunload'),false);
});
