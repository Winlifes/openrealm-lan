const {test} = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const source = fs.readFileSync(__dirname + '/wc3-movies.js', 'utf8');
function fixture(catalog) {
  const handlers = {}, tracks = [], events = {}, tap = {style:{}}, video = {
    style:{}, videoWidth:1280, videoHeight:533, duration:7,
    addEventListener:(name, fn)=>events[name]=fn,
    pause(){}, load(){}, play(){return Promise.resolve();},
    querySelectorAll:()=>[...tracks], append(track){tracks.push(track);}, removeAttribute(){this.src='';},
  };
  let completed = 0;
  const window = {__errors:[], __BUILD:'test',addEventListener:(name, fn)=>handlers[name]=fn};
  const context = {window, Module:{_WC3_MovieDone(){completed++;}}, performance:{now:()=>1},
    document:{getElementById:id=>id==='movie'?video:tap, createElement:()=>{
      const track={remove(){tracks.splice(tracks.indexOf(track),1);}};return track;
    }}, fetch:async()=>({ok:true,json:async()=>catalog})};
  vm.runInNewContext(source, context);
  return {window, video, tap, tracks, events, handlers, context, completed:()=>completed};
}
const entry = name=>({tiers:{1280:{url:`movies/${name}_1280.webm`},640:{url:`movies/${name}_640.webm`}},
  audioLocale:'zhCN',captions:{url:`movies/${name}.zh-CN.vtt`}});
const catalog = {version:1,movies:{intro:entry('intro'),outro:entry('outro')}};
test('catalog rejects foreign paths instead of starting arbitrary media',async()=>{
  const f=fixture({version:1,movies:{intro:{tiers:{1280:{url:'https://other.test/video'}}}}});
  await assert.rejects(f.window.loadMovieCatalog(),/视频路径无效/);
});
test('natural end completes once and releases tracks and media',async()=>{
  const f=fixture(catalog);await f.window.loadMovieCatalog();f.window.__moviePlay('Movies\\INTRO.avi');
  assert.equal(f.video.src,'movies/intro_1280.webm');assert.equal(f.tracks.length,1);
  f.video.onloadedmetadata(); const ended=f.video.onended;ended();ended();
  assert.equal(f.completed(),1);assert.equal(f.video.src,'');assert.equal(f.tracks.length,0);
  assert.equal(f.video.style.display,'none');
  assert.ok(f.window.__movieEvents.some(e=>e.type==='metadata'&&e.width===1280));
});
test('Escape skips once and stale completion cannot end a replacement movie',async()=>{
  const f=fixture(catalog);await f.window.loadMovieCatalog();f.window.__moviePlay('intro');
  const stale=f.video.onended;f.window.__moviePlay('outro');stale();assert.equal(f.completed(),0);
  let prevented=false;f.handlers.keydown({key:'Escape',preventDefault(){prevented=true;},stopPropagation(){}});
  f.events.click();assert.equal(f.completed(),1);assert.equal(prevented,true);
});
test('media failure reports both tiers before releasing the engine gate',async()=>{
  const f=fixture(catalog);await f.window.loadMovieCatalog();f.window.__moviePlay('intro');
  f.video.onerror();assert.equal(f.video.src,'movies/intro_640.webm');assert.equal(f.completed(),0);
  f.video.onerror();assert.equal(f.completed(),1);assert.equal(f.window.__errors.length,2);
});
test('blocked autoplay offers a click and a stale rejection cannot cover the next movie',async()=>{
  const f=fixture(catalog);let reject;
  f.video.play=()=>new Promise((resolve,bad)=>reject=bad);
  await f.window.loadMovieCatalog();f.window.__moviePlay('intro');const oldReject=reject;
  f.window.__moviePlay('outro');oldReject(Error('old request'));await Promise.resolve();
  assert.equal(f.tap.style.display,'none');reject(Error('gesture needed'));await Promise.resolve();
  assert.equal(f.tap.style.display,'block');f.video.play=()=>Promise.resolve();f.tap.onclick();
  assert.equal(f.tap.style.display,'none');assert.equal(f.completed(),0);
});
