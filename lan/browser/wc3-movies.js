/* The movie catalog is generated from the supplied CASC install. Native code
 * owns sequencing; report completion once after natural end or ordinary skip. */
(function () {
  'use strict';
  const video = document.getElementById('movie'), tap = document.getElementById('movie-tap');
  let active = false, generation = 0, current = '';
  window.__movieEvents = [];
  function event(type, extra) {
    window.__movieEvents.push({type, movie: current, at: performance.now(), ...extra});
    if (window.__movieEvents.length > 200) window.__movieEvents.shift();
  }
  function clearTracks() { video.querySelectorAll('track').forEach(track => track.remove()); }
  function finish(reason) {
    if (!active) return;
    event(reason);
    active = false; generation++;
    video.onended = video.onerror = video.onloadedmetadata = null;
    video.pause(); clearTracks(); video.removeAttribute('src'); video.load();
    video.style.display = tap.style.display = 'none';
    try { if (Module._WC3_MovieDone) Module._WC3_MovieDone(); }
    catch (error) { window.__errors.push('movie completion: ' + error); }
  }
  window.addEventListener('keydown', e => {
    if (active && e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); finish('skipped'); }
  }, true);
  video.addEventListener('click', () => finish('skipped'));
  window.loadMovieCatalog = async function () {
    const response = await fetch('movies/index.json?v=' + encodeURIComponent(window.__BUILD));
    if (!response.ok) throw Error('过场视频目录不可用');
    const catalog = await response.json();
    if (catalog.version !== 1 || !catalog.movies || Array.isArray(catalog.movies))
      throw Error('过场视频目录格式无效');
    const movies = new Map();
    for (const [name, entry] of Object.entries(catalog.movies)) {
      if (!/^[a-z0-9_]+$/.test(name)) throw Error('视频名称无效');
      for (const tier of ['1280', '640']) {
        if (entry.tiers?.[tier]?.url !== 'movies/' + name + '_' + tier + '.webm')
          throw Error('视频路径无效: ' + name);
      }
      if (entry.captions && entry.captions.url !== 'movies/' + name + '.zh-CN.vtt')
        throw Error('字幕路径无效: ' + name);
      movies.set(name, entry);
    }
    if (!movies.size) throw Error('过场视频目录为空');
    Module.WC3_MOVIES = movies;
  };
  window.__moviePlay = function (path) {
    const name = String(path).replace(/\\/g, '/').split('/').pop()
      .replace(/\.(mpq|avi|mp4|bik|webm)$/i, '').toLowerCase();
    const entry = Module.WC3_MOVIES?.get(name);
    if (!entry) throw Error('视频未配置: ' + path);
    current = name; active = true;
    const token = ++generation;
    video.pause(); clearTracks(); tap.style.display = 'none';
    video.style.display = 'block';
    if (entry.captions) {
      const track = document.createElement('track');
      track.kind = 'subtitles'; track.srclang = 'zh-CN'; track.label = '简体中文';
      track.default = true; track.src = entry.captions.url; video.append(track);
    }
    let tier = '1280';
    function showTap(error) {
      if (!active || token !== generation) return;
      event('gesture-required', {error: String(error)});
      tap.textContent = '▶ 点击播放'; tap.style.display = 'block';
    }
    function play() {
      const result = video.play();
      if (result?.catch) result.catch(showTap);
    }
    video.onloadedmetadata = () => {
      if (active && token === generation)
        event('metadata', {width: video.videoWidth, height: video.videoHeight, duration: video.duration, tier});
    };
    video.onended = () => { if (token === generation) finish('ended'); };
    video.onerror = () => {
      if (!active || token !== generation) return;
      const error = 'movie ' + name + ' tier ' + tier + ': ' + (video.error?.message || video.error?.code || 'media error');
      window.__errors.push(error); event('error', {error});
      if (tier === '1280') { tier = '640'; video.src = entry.tiers[tier].url; play(); }
      else finish('failed');
    };
    tap.onclick = () => { tap.style.display = 'none'; play(); };
    video.src = entry.tiers[tier].url;
    event('requested', {tier, audioLocale: entry.audioLocale, captions: !!entry.captions});
    play();
  };
})();
