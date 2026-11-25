'use strict';

const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {join} = require('node:path');
const {test} = require('node:test');
const {runInNewContext} = require('node:vm');

const script = readFileSync(join(__dirname, 'browser', 'wc3-chat-input.js'), 'utf8');

function chatSession() {
  const input = new EventTarget();
  const canvas = {};
  const document = {
    activeElement: canvas,
    getElementById(id) { return id === 'canvas' ? canvas : input; },
  };
  for (const element of [input, canvas]) {
    element.focus = () => { document.activeElement = element; };
  }
  input.hidden = true;
  const Module = {};
  runInNewContext(script, {document, Module});
  return {input, canvas, document, editor: Module.gameChatInput};
}

function press(input, key, properties = {}) {
  const event = Object.assign(new Event('keydown', {cancelable: true}), {key, ...properties});
  input.dispatchEvent(event);
  return event.defaultPrevented;
}

test('IME confirmation leaves the editor open until a separate Enter submits', () => {
  const {editor, input, canvas, document} = chatSession();
  editor.open();
  assert.equal(document.activeElement, input);
  assert.equal(input.hidden, false);
  input.value = 'woyaoqian';
  input.dispatchEvent(new Event('compositionstart'));
  assert.equal(press(input, 'Enter'), false);
  assert.equal(editor.take(), undefined);

  input.dispatchEvent(new Event('compositionend'));
  input.value = '我要钱';
  assert.equal(press(input, 'Enter', {keyCode: 229}), false);
  assert.equal(editor.take(), undefined);
  assert.equal(press(input, 'Enter'), true);
  assert.equal(editor.take(), '我要钱');
  assert.equal(editor.take(), undefined);
  editor.close();
  assert.equal(input.hidden, true);
  assert.equal(document.activeElement, canvas);
});

test('Escape discards the draft and blur preserves the newly focused control', () => {
  const {editor, input, canvas, document} = chatSession();
  editor.open();
  input.value = '取消草稿';
  assert.equal(press(input, 'Escape'), true);
  assert.equal(editor.take(), '');
  editor.close();
  assert.equal(document.activeElement, canvas);

  editor.open();
  assert.equal(input.value, '');
  input.value = '离开输入框';
  const otherControl = {};
  document.activeElement = otherControl;
  input.dispatchEvent(new Event('blur'));
  assert.equal(editor.take(), '');
  editor.close();
  assert.equal(document.activeElement, otherControl);
});
