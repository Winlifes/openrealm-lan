/* Transport-contract checks use a console fixture, never a gameplay receipt. */
import {test} from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {mkdtemp, mkdir, writeFile, readFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {setTimeout as delay} from 'node:timers/promises';

async function upgrade(port, path) {
  return new Promise((resolve, reject) => {
    const req = http.request({host:'127.0.0.1',port,path,headers:{
      Connection:'Upgrade',Upgrade:'websocket','Sec-WebSocket-Version':'13',
      'Sec-WebSocket-Key':'dGhlIHNhbXBsZSBub25jZQ==',Origin:'https://wc3.test'} });
    req.on('upgrade',(response,socket)=>{ socket.destroy(); resolve(response.statusCode); });
    req.on('response',response=>{ response.resume(); resolve(response.statusCode); });
    req.on('error',reject); req.setTimeout(3000,()=>req.destroy(Error('upgrade timeout'))); req.end();
  });
}

test('only the browser build paired with the immutable runtime can join', {timeout:15000}, async () => {
  const root = await mkdtemp(join(tmpdir(),'wc3-room-build-'));
  const build = 'contract-test-r26', dev = join(root,'development');
  const engine = join(root,'engine-releases',build,'bin','openwarcraft3');
  const reservation = net.createServer();
  reservation.listen(0,'127.0.0.1'); await once(reservation,'listening');
  const port = reservation.address().port; await new Promise(resolve=>reservation.close(resolve));
  let child;
  try {
    await mkdir(join(dev,'src'),{recursive:true});
    await mkdir(join(root,'engine-releases',build,'bin'),{recursive:true});
    await writeFile(engine,`#!/usr/bin/env node
import readline from 'node:readline';
console.log('Server initialized.');
readline.createInterface({input:process.stdin}).on('line',line=>{
  console.log('fixture command: '+line);
  if(line.startsWith('lobby_start '))console.log('Lobby initialized for test map');
});
`,{mode:0o755});
    const scenarioSlots=[
      {player:0,race:1,team:0,color:0,computer:false},
      {player:1,race:2,team:0,color:1,computer:false},
      {player:5,race:2,team:0,color:5,computer:false},
      {player:6,race:3,team:0,color:6,computer:false},
      {player:11,race:4,team:1,color:11,computer:true},
    ];
    await writeFile(join(dev,'maps-catalog.json'),JSON.stringify([
      {id:'test',name:'Test',path:'Test.w3m',slots:[{player:0,race:1,team:0,color:0,computer:false}]},
      {id:'scenario',name:'Warchasers',path:'Warchasers.w3m',slots:scenarioSlots},
    ]));
    child = spawn(process.execPath,[fileURLToPath(new URL('./rooms.mjs',import.meta.url))],{
      env:{...process.env,WC3_LAN_ROOT:root,WC3_ENGINE:engine,WC3_API_PORT:String(port),WC3_ORIGIN:'https://wc3.test'},
      stdio:['ignore','pipe','pipe']});
    let logs=''; child.stdout.on('data',b=>logs+=b); child.stderr.on('data',b=>logs+=b);
    const api = async (path,options) => {
      const response=await fetch(`http://127.0.0.1:${port}/api/${path}`,options);
      assert.ok(response.ok,await response.clone().text()); return response.json();
    };
    for(let i=0; !logs.includes('WC3 room service ready'); i++) {
      assert.ok(i<100 && child.exitCode===null,logs); await delay(30);
    }
    const health=await api('health');
    const room=await api('rooms',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({mapId:'test',name:'Winlife 测试房间'})});
    for(let i=0;;i++) {
      const rooms=await api('rooms');
      if(rooms[0]?.status==='waiting')break;
      assert.ok(i<100 && rooms[0]?.status!=='error',JSON.stringify(rooms)); await delay(30);
    }
    const path=`/api/rooms/${room.id}/ws`;
    const engineLog=await readFile(join(dev,'room-logs',room.id+'.log'),'utf8');
    assert.ok(engineLog.includes('lobby_config 2 1 "Winlife 测试房间"'),engineLog);
    const missing=await upgrade(port,path);
    await delay(50);
    const stale=await upgrade(port,path+'?build=old-client');
    await delay(50);
    const matching=await upgrade(port,path+'?build='+build);
    assert.deepEqual({build:health.build,missing,stale,matching},
      {build,missing:426,stale:426,matching:101});
    const scenarioMap=(await api('maps')).find(m=>m.id==='scenario');
    assert.equal(scenarioMap.players,4,'Authored computer players must not inflate the human player capacity');
    const scenario=await api('rooms',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({mapId:'scenario',name:'原版合作图'})});
    assert.equal(scenario.slots,4);
    for(let i=0;;i++) {
      const current=(await api('rooms')).find(r=>r.id===scenario.id);
      if(current?.status==='waiting')break;
      assert.ok(i<100 && current?.status!=='error',JSON.stringify(current)); await delay(30);
    }
    const scenarioLog=await readFile(join(dev,'room-logs',scenario.id+'.log'),'utf8');
    assert.ok(scenarioLog.includes('lobby_config 2 5 "原版合作图"'),scenarioLog);
    for(const [i,s] of scenarioSlots.entries()) {
      const command=`lobby_slot ${i} 1 ${s.player} ${s.computer?2:0} ${s.race} ${s.team} ${s.color} "${s.computer?'电脑':'开放位置'}"`;
      assert.ok(scenarioLog.includes(command),scenarioLog);
    }
  } finally {
    if(child && child.exitCode===null) { child.kill('SIGTERM'); await once(child,'close'); }
    await rm(root,{recursive:true,force:true});
  }
});
