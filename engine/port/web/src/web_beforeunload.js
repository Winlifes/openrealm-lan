// Emscripten 4.0.23's HTML5 bridge omits the pointer conversion on this
// callback's return value. Keep the native warning/cancellation contract.
addToLibrary({
  $registerBeforeUnloadEventCallback__deps: ['$JSEvents', '$findEventTarget'],
  $registerBeforeUnloadEventCallback: (target, userData, useCapture, callbackfunc, eventTypeId, eventTypeString) => {
    var handlerFunc = e => {
      var message = {{{ makeDynCall('pipp', 'callbackfunc') }}}(eventTypeId, 0, userData);
      {{{ from64('message') }}};
      if (message) message = UTF8ToString(message);
      if (message) {
        e.preventDefault();
        e.returnValue = message;
        return message;
      }
    };
    return JSEvents.registerOrRemoveHandler({
      target: findEventTarget(target), eventTypeString, eventTypeId,
      userData, callbackfunc, handlerFunc, useCapture
    });
  }
});
