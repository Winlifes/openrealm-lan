import http from 'node:http';
import dgram from 'node:dgram';
import {spawn} from 'node:child_process';
import {readFileSync, mkdirSync, createWriteStream, realpathSync} from 'node:fs';
import {homedir} from 'node:os';
import {join, basename, dirname} from 'node:path';
import {randomBytes} from 'node:crypto';
import {EventEmitter} from 'node:events';
import {setTimeout as delay} from 'node:timers/promises';
import {WebSocketServer} from 'ws';

export const MAX_BUFFERED_DATAGRAM_BYTES = 256 * 1024;
export function forwardDatagram(ws, data) {
  // Preserve UDP congestion semantics. Netchan owns reliable retransmission;
  // an old WebSocket backlog must not turn a temporary stall into a disconnect.
  if (ws.readyState !== ws.OPEN || ws.bufferedAmount > MAX_BUFFERED_DATAGRAM_BYTES) return false;
  ws.send(data);
  return true;
}

const transportRevision = 'udp-backpressure-r90';
const traceTransport = process.env.WC3_TRACE_TRANSPORT === '1';
const root = process.env.WC3_LAN_ROOT || join(homedir(), '.local/share/wc3-lan');
const dev = join(root, 'development');
/* A running dylib must never be overwritten by the next build. The deployment
 * script switches this immutable runtime only after the checks pass. */
const engine = realpathSync(process.env.WC3_ENGINE || join(root, 'engine-current/bin/openwarcraft3'));
/* Both artifacts use one immutable release name. Derive it from the resolved
 * executable, so a later symlink switch cannot relabel existing processes. */
const build = basename(dirname(dirname(engine)));
const maps = JSON.parse(readFileSync(join(dev, 'maps-catalog.json'), 'utf8'));
const rooms = new Map();
const maxRooms = 4, firstPort = Number(process.env.WC3_GAME_PORT_BASE || 27970);
if (!Number.isInteger(firstPort) || firstPort<1024 || firstPort+maxRooms>65536)
  throw Error('Invalid WC3_GAME_PORT_BASE');
const origin = process.env.WC3_ORIGIN || 'https://localhost:8444';
const publicOrigin = process.env.WC3_PUBLIC_ORIGIN;
const publicHost = publicOrigin ? new URL(publicOrigin).host : null;
const publicPrefix = process.env.WC3_PUBLIC_PREFIX || '';
if (!/^(?:\/[A-Za-z0-9_-]+)*$/.test(publicPrefix)) throw Error('Invalid WC3_PUBLIC_PREFIX');
const allowedOrigins = new Set([origin, publicOrigin].filter(Boolean));
const acceptsOrigin = request => !request.headers.origin || allowedOrigins.has(request.headers.origin);
const safeText = (value, fallback) => String(value || fallback).replace(/[\x00-\x1f\x7f"\\]/g, ' ').trim().slice(0, 40) || fallback;
const quoted = value => '"' + String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
const humanCapacity = map => map.slots.filter(slot=>!slot.computer).length;
mkdirSync(join(dev, 'room-logs'), {recursive:true});

function publicRoom(room, request) {
  const prefix = request.headers.host === publicHost ? publicPrefix : '';
  return {id:room.id, name:room.name, map:room.map.name, mapId:room.map.id,
    slots:humanCapacity(room.map), players:room.peers.size, status:room.status, error:room.error,
    build,url:prefix + '/play/game/?' + new URLSearchParams({room:room.id,map:room.map.path})};
}
function writeCommand(room, text) {
  if (!room.child || room.child.exitCode !== null || !room.child.stdin.writable) throw Error('游戏进程已停止');
  room.child.stdin.write(text + '\n');
}
async function waitLog(room, text, timeout = 30000) {
  if (room.logTail.includes(text)) return;
  await new Promise((resolve, reject) => {
    const finish = error => { clearTimeout(timer); room.events.off('log', check); room.events.off('exit', exited); error ? reject(error) : resolve(); };
    const check = () => { if(room.logTail.includes(text))finish(); };
    const exited = () => finish(Error('游戏进程提前退出'));
    const timer = setTimeout(() => finish(Error('游戏进程准备超时')), timeout);
    room.events.on('log', check); room.events.on('exit', exited);
  });
}
function stopRoom(room) {
  if (room.stopping) return;
  room.stopping = true;
  for (const ws of room.peers) ws.close(1001, '房间已关闭');
  room.child?.kill('SIGTERM');
  const child = room.child;
  setTimeout(() => { if(child && child.exitCode === null)child.kill('SIGKILL'); }, 3000).unref();
  rooms.delete(room.id);
}
async function initializeRoom(room) {
  try {
    room.child = spawn(engine, ['-data', join(root,'data-current'), '+dedicated','1',
      '+game_port',String(room.port), '+fs_expansion','1', '+set','wc3_locale','zhCN',
      '+set','sv_local_signon_batch','1',
      '+set','com_frame_limit','0', '+set','com_fast_forward','0',
      '+lobby_start', room.map.path],
      {cwd:join(dev,'src'), stdio:['pipe','pipe','pipe']});
    const output = createWriteStream(join(dev, 'room-logs', room.id + '.log'));
    const append = data => {
      output.write(data); room.logTail = (room.logTail + data.toString()).slice(-160000);
      if (room.status === 'waiting' && room.logTail.includes('SV_LobbyStartMatch:')) room.status = 'playing';
      room.events.emit('log');
    };
    room.child.stdout.on('data', append); room.child.stderr.on('data', append);
    room.child.on('error', error => { room.error = error.message; room.events.emit('exit'); });
    room.child.on('close', (code, signal) => {
      output.end(`\n[room] process closed: code=${code} signal=${signal}\n`); room.events.emit('exit');
      if (!room.stopping) {
        room.status='error'; room.error ||= `游戏进程已退出（${signal || code}）`;
        for (const ws of room.peers) ws.close(1011, '游戏进程已退出');
      }
    });
    await waitLog(room, 'Lobby initialized for');
    writeCommand(room, `lobby_config 2 ${room.map.slots.length} ${quoted(room.name)}`);
    /* stdin's current console reader accepts one line per readiness event. Send
     * individually, after real startup markers, rather than fixed boot sleeps. */
    await delay(100);
    for (let i=0;i<room.map.slots.length;i++) {
      const s=room.map.slots[i];
      writeCommand(room, `lobby_slot ${i} 1 ${s.player} ${s.computer?2:0} ${s.race} ${s.team} ${s.color} ${quoted(s.computer?'电脑':'开放位置')}`);
      await delay(100);
    }
    room.logTail = ''; room.status = 'waiting'; room.emptySince = Date.now();
    writeCommand(room, 'sv_net_status');
  } catch(error) { room.status='error'; room.error=error.message; room.child?.kill('SIGTERM'); }
}

function json(response, code, body) {
  response.writeHead(code, {'Content-Type':'application/json; charset=utf-8','Cache-Control':'no-store'});
  response.end(JSON.stringify(body));
}
export const server = http.createServer(async (request,response) => {
  try {
    const url = new URL(request.url, origin);
    if (request.method==='GET' && url.pathname==='/api/health') return json(response,200,{ok:true,rooms:rooms.size,build,transportRevision});
    if (request.method==='GET' && url.pathname==='/api/maps') return json(response,200,maps.map(map=>({id:map.id,name:map.name,players:humanCapacity(map)})));
    if (request.method==='GET' && url.pathname==='/api/rooms') return json(response,200,[...rooms.values()].map(room=>publicRoom(room,request)));
    if (request.method==='POST' && url.pathname==='/api/rooms') {
      if (!acceptsOrigin(request)) return json(response,403,{error:'请从游戏网站创建房间'});
      if (rooms.size>=maxRooms) return json(response,409,{error:'目前最多同时运行 4 个房间，请等待空闲房间'});
      let text=''; for await (const chunk of request) { text+=chunk; if(text.length>4096)throw Error('请求过大'); }
      const input=JSON.parse(text), map=maps.find(m=>m.id===input.mapId);
      if(!map) return json(response,400,{error:'请选择有效地图'});
      const ports=new Set([...rooms.values()].map(r=>r.port));
      const port=Array.from({length:maxRooms},(_,i)=>firstPort+i).find(p=>!ports.has(p));
      const room={id:randomBytes(9).toString('hex'),name:safeText(input.name,'局域网对局'),map,port,status:'starting',
        peers:new Set(),events:new EventEmitter(),logTail:'',emptySince:Date.now(),created:Date.now()};
      rooms.set(room.id,room); void initializeRoom(room);
      return json(response,202,publicRoom(room,request));
    }
    return json(response,404,{error:'没有这个接口'});
  } catch(error) { json(response,400,{error:error.message}); }
});

const wss=new WebSocketServer({noServer:true,maxPayload:65507,perMessageDeflate:false});
server.on('upgrade',(request,socket,head)=>{
  const url=new URL(request.url,origin);
  const match=url.pathname.match(/^\/api\/rooms\/([a-f0-9]{18})\/ws$/);
  const room=match&&rooms.get(match[1]);
  if (!acceptsOrigin(request) || !room || room.status!=='waiting' ||
      room.peers.size>=humanCapacity(room.map)) {
    socket.end('HTTP/1.1 409 Conflict\r\nConnection: close\r\n\r\n'); return;
  }
  if(url.searchParams.get('build')!==build) {
    socket.end('HTTP/1.1 426 Upgrade Required\r\nConnection: close\r\nContent-Length: 0\r\n\r\n'); return;
  }
  wss.handleUpgrade(request,socket,head,ws=>wss.emit('connection',ws,room));
});
wss.on('connection',(ws,room)=>{
  const udp=dgram.createSocket('udp4'); room.peers.add(ws); room.emptySince=null;
  let forwarded=0, congestionDrops=0, peakBuffered=0, lastDropLog=0;
  const trace=(event,extra={})=>{
    if(traceTransport || event==='close' || event==='error') console.error('WC3_TRANSPORT '+JSON.stringify({at:new Date().toISOString(),
      event,room:room.id,forwarded,congestionDrops,peakBuffered,...extra}));
  };
  const close=(code,reason)=>{
    trace('close',{code,reason:String(reason||'').slice(0,120),buffered:ws.bufferedAmount});
    room.peers.delete(ws); if(!room.peers.size)room.emptySince=Date.now();
    try {
      const port=udp.address().port;
      if(!room.stopping) {
        room.consoleQueue=(room.consoleQueue||Promise.resolve()).then(async()=>{
          writeCommand(room,'client_drop_endpoint '+port); await delay(100);
        }).catch(()=>{});
      }
    } catch {}
    try{udp.close();}catch{}
  };
  udp.on('message',(message,address)=>{
    if(address.address!=='127.0.0.1'||address.port!==room.port)return;
    if(ws.readyState!==ws.OPEN)return;
    if(forwardDatagram(ws,message))forwarded++;
    else {
      congestionDrops++;
      const now=Date.now();
      if(congestionDrops===1 || now-lastDropLog>=1000){
        lastDropLog=now; trace('drop-backpressure',{buffered:ws.bufferedAmount});
      }
    }
    peakBuffered=Math.max(peakBuffered,ws.bufferedAmount);
  });
  udp.on('error',()=>ws.close(1011,'联机通道异常'));
  ws.on('message',(data,isBinary)=>{
    if(!isBinary || data.length>65507)return;
    udp.send(data,room.port,'127.0.0.1',error=>{if(error)ws.close(1011,'联机通道异常');});
  });
  ws.on('close',close); ws.on('error',error=>trace('error',{message:error.message}));
});
setInterval(()=>{
  for(const room of rooms.values()) {
    if(room.status==='starting' && Date.now()-room.created>45000)stopRoom(room);
    else if(room.emptySince && Date.now()-room.emptySince>(room.status==='error'?10000:90000))stopRoom(room);
  }
},5000).unref();
for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>{
  for(const room of rooms.values())stopRoom(room);
  server.close(()=>process.exit(0)); setTimeout(()=>process.exit(0),4000).unref();
});
server.listen(Number(process.env.WC3_API_PORT||18780),'127.0.0.1',()=>console.log('WC3 room service ready'));
