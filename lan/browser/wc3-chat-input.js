(() => {
  'use strict';
  const input = document.getElementById('game-chat-message');
  const canvas = document.getElementById('canvas');
  let composing = false;
  let pending;

  input.addEventListener('compositionstart', () => { composing = true; });
  input.addEventListener('compositionend', () => { composing = false; });
  input.addEventListener('keypress', event => event.stopPropagation());
  input.addEventListener('keyup', event => event.stopPropagation());
  input.addEventListener('keydown', event => {
    event.stopPropagation();
    if (input.hidden || composing || event.isComposing || event.keyCode === 229) return;
    if (event.key !== 'Enter' && event.key !== 'Escape') return;
    event.preventDefault();
    if (pending === undefined) pending = event.key === 'Enter' ? input.value : '';
  });
  input.addEventListener('blur', () => {
    if (!input.hidden && pending === undefined) pending = '';
  });

  Module['gameChatInput'] = {
    open() {
      composing = false;
      pending = undefined;
      input.value = '';
      input.hidden = false;
      input.focus({preventScroll: true});
    },
    close() {
      const restoreFocus = document.activeElement === input;
      input.hidden = true;
      input.value = '';
      pending = undefined;
      if (restoreFocus) canvas.focus({preventScroll: true});
    },
    take() {
      const line = pending;
      pending = undefined;
      return line;
    },
  };
})();
