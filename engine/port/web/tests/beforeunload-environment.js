// Node-only browser event fixture; production registration and WASM callbacks run unchanged.
globalThis.window = globalThis;
globalThis.onbeforeunload = null;
globalThis.addEventListener = (name, handler) => {
  if (name === 'beforeunload') globalThis.testBeforeUnload = handler;
};
globalThis.removeEventListener = (name, handler) => {
  if (name === 'beforeunload' && globalThis.testBeforeUnload === handler)
    delete globalThis.testBeforeUnload;
};
