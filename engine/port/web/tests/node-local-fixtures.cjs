/* Headless Node engine tests use the real local fixture archives. Missing
 * optional loose files must remain misses, rather than fetching browser URLs.
 * This adapter supplies no game data and is never included in browser builds. */
global.XMLHttpRequest = class LocalFixturesOnly {
  status = 404;
  responseText = '';
  open(method, url, asynchronous) {
    if (method !== 'GET' || asynchronous) throw new Error('Expected a synchronous fixture lookup');
  }
  overrideMimeType() {}
  send() {}
};
