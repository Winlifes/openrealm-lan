/* Transport-contract checks use a console fixture, never a gameplay receipt. */
import {test} from 'node:test';
import assert from 'node:assert/strict';
import dgram from 'node:dgram';
import http from 'node:http';
import net from 'node:net';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {mkdtemp, mkdir, writeFile, readFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {setTimeout as delay} from 'node:timers/promises';
import {WebSocket} from 'ws';

async function upgrade(port, path, origin='https://wc3.test', host=new URL(origin).host) {
  return new Promise((resolve, reject) => {
    const req = http.request({host:'127.0.0.1',port,path,headers:{
      Connection:'Upgrade',Upgrade:'websocket','Sec-WebSocket-Version':'13',
      'Sec-WebSocket-Key':'dGhlIHNhbXBsZSBub25jZQ==',Origin:origin,Host:host} });
    req.on('upgrade',(response,socket)=>{ socket.destroy(); resolve(response.statusCode); });
    req.on('response',response=>{ response.resume(); resolve(response.statusCode); });
    req.on('error',reject); req.setTimeout(3000,()=>req.destroy(Error('upgrade timeout'))); req.end();
  });
}

const lanOrigin = 'https://wc3.test';
const publicOrigin = 'https://wc3-public.test';
const publicHeaders = {Host:new URL(publicOrigin).host};
const scenarioSlots = [
  {player:0,race:1,team:0,color:0,computer:false},
  {player:1,race:2,team:0,color:1,computer:false},
  {player:5,race:2,team:0,color:5,computer:false},
  {player:6,race:3,team:0,color:6,computer:false},
  {player:11,race:4,team:1,color:11,computer:true},
];

async function findGamePorts() {
  for (let attempt = 0; attempt < 20; attempt++) {
    const sockets = [];
    try {
      let base = 0;
      for (let i = 0; i < 4; i++) {
        const socket = dgram.createSocket('udp4');
        sockets.push(socket);
        socket.bind(i === 0 ? 0 : base + i, '127.0.0.1');
        await once(socket, 'listening');
        if (i === 0) base = socket.address().port;
      }
      return base;
    } catch (error) {
      if (error.code !== 'EADDRINUSE' && error.code !== 'ERR_SOCKET_BAD_PORT') throw error;
    } finally {
      for (const socket of sockets) socket.close();
    }
  }
  throw Error('Could not reserve four room fixture ports');
}

async function waitFor(read, predicate, timeout = 3000) {
  const deadline = Date.now() + timeout;
  let value;
  do {
    value = await read();
    if (predicate(value)) return value;
    await delay(25);
  } while (Date.now() < deadline);
  assert.fail(`Unexpected room state: ${JSON.stringify(value)}`);
}

async function startService(t) {
  const root = await mkdtemp(join(tmpdir(),'wc3-room-build-'));
  const build = 'contract-test-r26', dev = join(root,'development');
  const engine = join(root,'engine-releases',build,'bin','openwarcraft3');
  let child;
  const sockets = [];
  t.after(async () => {
    for (const ws of sockets) ws.terminate();
    if (child && child.exitCode === null && child.signalCode === null) {
      child.kill('SIGTERM');
      await once(child,'close');
    }
    await rm(root,{recursive:true,force:true});
  });
  const reservation = net.createServer();
  reservation.listen(0,'127.0.0.1'); await once(reservation,'listening');
  const port = reservation.address().port; await new Promise(resolve=>reservation.close(resolve));
  await mkdir(join(dev,'src'),{recursive:true});
  await mkdir(join(root,'engine-releases',build,'bin'),{recursive:true});
  await writeFile(engine,`#!/usr/bin/env node
import dgram from 'node:dgram';
import readline from 'node:readline';
if(process.argv.includes('+map')||!process.argv.includes('+lobby_start'))process.exit(1);
console.log('fixture args: '+JSON.stringify(process.argv.slice(2)));
const game = dgram.createSocket('udp4');
game.on('message',msg=>{
  if(msg.toString()==='start fixture')console.log('SV_LobbyStartMatch: fixture');
});
game.bind(Number(process.argv[process.argv.indexOf('+game_port')+1]),'127.0.0.1',()=>{
  console.log('Lobby initialized for test map');
});
process.on('SIGTERM',()=>setTimeout(()=>process.exit(0),500));
readline.createInterface({input:process.stdin}).on('line',line=>{
  console.log('fixture command: '+line);
});
`,{mode:0o755});
  await writeFile(join(dev,'maps-catalog.json'),JSON.stringify([
    {id:'test',name:'Test',path:'Test.w3m',publicSnapshotRateBytesPerSecond:48000,
      slots:[{player:0,race:1,team:0,color:0,computer:false}]},
    {id:'scenario',name:'Warchasers',path:'Warchasers.w3m',publicSnapshotRateBytesPerSecond:48000,slots:scenarioSlots},
  ]));
  const gamePortBase = await findGamePorts();
  child = spawn(process.execPath,[fileURLToPath(new URL('./rooms.mjs',import.meta.url))],{
    env:{...process.env,WC3_LAN_ROOT:root,WC3_ENGINE:engine,WC3_API_PORT:String(port),WC3_GAME_PORT_BASE:String(gamePortBase),
      WC3_ORIGIN:lanOrigin,WC3_PUBLIC_ORIGIN:publicOrigin,WC3_PUBLIC_PREFIX:'/wc3',WC3_ENGINE_PROFILE:'1'},
    stdio:['ignore','pipe','pipe']});
  let logs=''; child.stdout.on('data',b=>logs+=b); child.stderr.on('data',b=>logs+=b);
  const request = async (path,options={}) => {
    const req=http.request({host:'127.0.0.1',port,path:'/api/'+path,method:options.method,headers:options.headers});
    const received=once(req,'response');
    req.end(options.body);
    const [response]=await received;
    let body='';
    response.setEncoding('utf8');
    for await (const chunk of response) body+=chunk;
    return {status:response.statusCode,data:JSON.parse(body)};
  };
  const api = async (path,options={}) => {
    const {status,data} = await request(path,options);
    if (options.status) assert.equal(status,options.status,JSON.stringify(data));
    else assert.ok(status>=200 && status<300,JSON.stringify(data));
    return data;
  };
  for(let i=0; !logs.includes('WC3 room service ready'); i++) {
    assert.ok(i<100 && child.exitCode===null,logs); await delay(30);
  }
  const connect = async (room, origin=lanOrigin, autoPong=true) => {
    const ws = new WebSocket(`ws://127.0.0.1:${port}/api/rooms/${room.id}/ws?build=${build}`,
      {origin,headers:{Host:new URL(origin).host},autoPong,handshakeTimeout:3000});
    sockets.push(ws);
    await once(ws,'open');
    return ws;
  };
  return {api,request,connect,port,build,dev};
}

test('rooms require the matching build and stay on their originating site', {timeout:15000}, async t => {
  const {api,connect,port,build,dev} = await startService(t);
  const health=await api('health');
  const room=await api('rooms',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mapId:'test',name:'Winlife 测试房间'})});
  assert.equal(new URL(room.url,'https://wc3.test').searchParams.get('map'),'Test.w3m');
  for(let i=0;;i++) {
    const rooms=await api('rooms');
    if(rooms[0]?.status==='waiting')break;
    assert.ok(i<100 && rooms[0]?.status!=='error',JSON.stringify(rooms)); await delay(30);
  }
  const path=`/api/rooms/${room.id}/ws`;
  const engineLog=await readFile(join(dev,'room-logs',room.id+'.log'),'utf8');
  assert.ok(engineLog.includes('lobby_config 2 1 "Winlife 测试房间"'),engineLog);
  assert.ok(!engineLog.includes('"sv_rate"'),engineLog);
  assert.ok(engineLog.includes('"+set","com_profile","1"'),engineLog);
  const missing=await upgrade(port,path);
  await delay(50);
  const stale=await upgrade(port,path+'?build=old-client');
  await delay(50);
  assert.deepEqual({build:health.build,missing,stale}, {build,missing:426,stale:426});
  await delay(50);
  assert.equal(await upgrade(port,path+'?build='+build,publicOrigin),409);
  assert.equal(await upgrade(port,path+'?build='+build,publicOrigin,'wc3.test'),409);
  assert.equal(await upgrade(port,path+'?build='+build,'https://outside.test'),409);
  assert.deepEqual(await api('rooms',{headers:publicHeaders}),[]);
  await api(`rooms/${room.id}`,{headers:publicHeaders,status:404});
  await api('rooms',{method:'POST',headers:{...publicHeaders,Origin:'https://wc3.test'},
    body:JSON.stringify({mapId:'test'}),status:403});
  const scenarioMap=(await api('maps')).find(m=>m.id==='scenario');
  assert.equal(scenarioMap.players,4,'Authored computer players must not inflate the human player capacity');
  const scenario=await api('rooms',{method:'POST',headers:{...publicHeaders,'Content-Type':'application/json',Origin:publicOrigin},
    body:JSON.stringify({mapId:'scenario',name:'原版合作图'})});
  assert.equal(scenario.slots,4);
  assert.equal(new URL(scenario.url,publicOrigin).pathname,'/wc3/play/game/');
  const publicList=await api('rooms',{headers:publicHeaders});
  assert.deepEqual(publicList.map(r=>r.id),[scenario.id]);
  assert.deepEqual((await api('rooms')).map(r=>r.id),[room.id]);
  assert.equal((await api('health',{headers:publicHeaders})).rooms,1);
  assert.equal((await api('health')).rooms,1);
  assert.equal(new URL(publicList[0].url,publicOrigin).pathname,'/wc3/play/game/');
  assert.equal(new URL(room.url,'https://wc3.test').pathname,'/play/game/');
  assert.equal((await api(`rooms/${scenario.id}`,{headers:publicHeaders})).id,scenario.id);
  await api(`rooms/${scenario.id}`,{status:404});
  for(let i=0;;i++) {
    const current=await api(`rooms/${scenario.id}`,{headers:publicHeaders});
    if(current?.status==='waiting')break;
    assert.ok(i<100 && current?.status!=='error',JSON.stringify(current)); await delay(30);
  }
  const publicPath=`/api/rooms/${scenario.id}/ws?build=${build}`;
  assert.equal(await upgrade(port,publicPath),409);
  await connect(room);
  await connect(scenario,publicOrigin);
  const scenarioLog=await readFile(join(dev,'room-logs',scenario.id+'.log'),'utf8');
  assert.ok(scenarioLog.includes('lobby_config 2 5 "原版合作图"'),scenarioLog);
  assert.ok(scenarioLog.includes('"+set","sv_rate","48000"'),scenarioLog);
  for(const [i,s] of scenarioSlots.entries()) {
    const command=`lobby_slot ${i} 1 ${s.player} ${s.computer?2:0} ${s.race} ${s.team} ${s.color} "${s.computer?'电脑':'开放位置'}"`;
    assert.ok(scenarioLog.includes(command),scenarioLog);
  }
});

test('a room closes with its last peer and releases the capacity for a replacement', {timeout:10000}, async t => {
  const {api, request, connect, dev} = await startService(t);
  const create = (mapId, status=202) => api('rooms',{method:'POST',body:JSON.stringify({mapId}),status});
  const room = await create('scenario');
  const path = `rooms/${room.id}`;
  await waitFor(() => api(path), state => state.status === 'waiting' && state.players === 0);
  const host = await connect(room);
  const guest = await connect(room);
  host.send(Buffer.from('start fixture'));
  await waitFor(() => api(path), state => state.status === 'playing' && state.players === 2);

  guest.terminate();
  await waitFor(() => api(path), state => state.status === 'playing' && state.players === 1);

  await Promise.all([create('test'), create('test'), create('test')]);
  await create('test',409);
  host.close();
  await once(host,'close');
  await waitFor(() => request(path), response => response.status === 404, 400);
  assert.ok((await api('rooms')).every(current => current.id !== room.id));

  const replacement = await create('test');
  await waitFor(() => api(`rooms/${replacement.id}`), state => state.status === 'waiting');
  const oldLog = await readFile(join(dev,'room-logs',room.id+'.log'),'utf8');
  assert.ok(oldLog.includes('[room] process closed'), oldLog);
});

test('the heartbeat closes a silent last peer and keeps a responding player connected', {timeout:26000}, async t => {
  const {api, request, connect} = await startService(t);
  const create = () => api('rooms',{method:'POST',body:JSON.stringify({mapId:'test'}),status:202});
  const [silentRoom, liveRoom] = await Promise.all([create(), create()]);
  await Promise.all([silentRoom,liveRoom].map(room => waitFor(() => api(`rooms/${room.id}`), state => state.status === 'waiting')));
  const silent = await connect(silentRoom,lanOrigin,false);
  const live = await connect(liveRoom);
  const closed = once(silent,'close');
  await once(live,'ping');
  await closed;
  await waitFor(() => request(`rooms/${silentRoom.id}`), response => response.status === 404, 400);
  assert.equal((await api(`rooms/${liveRoom.id}`)).players,1);
  assert.equal(live.readyState,WebSocket.OPEN);
});
