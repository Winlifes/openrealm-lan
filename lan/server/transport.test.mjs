/* Real TCP congestion of the bridge's opaque UDP datagrams; no game simulation. */
import {test, before, after} from 'node:test';
import assert from 'node:assert/strict';
import {once} from 'node:events';
import {mkdtemp, mkdir, writeFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {setTimeout as delay, setImmediate as nextTurn} from 'node:timers/promises';
import {WebSocket, WebSocketServer} from 'ws';

let fixture, forwardDatagram;
before(async () => {
  fixture = await mkdtemp(join(tmpdir(), 'wc3-datagram-policy-'));
  await mkdir(join(fixture, 'development'), {recursive:true});
  await writeFile(join(fixture, 'development/maps-catalog.json'), '[]');
  const engine = join(fixture, 'engine-releases/transport-fixture/bin/openwarcraft3');
  await mkdir(join(engine, '..'), {recursive:true});
  await writeFile(engine, 'not executed by this transport test');
  const env = {WC3_LAN_ROOT:fixture, WC3_ENGINE:engine, WC3_API_PORT:'0'};
  const previous = Object.fromEntries(Object.keys(env).map(key => [key, process.env[key]]));
  Object.assign(process.env, env);
  try {
    const module = await import('./rooms.mjs');
    forwardDatagram = module.forwardDatagram;
    if (!module.server.listening) await once(module.server, 'listening');
    await new Promise(resolve => module.server.close(resolve));
  } finally {
    for (const [key, value] of Object.entries(previous)) {
      if (value === undefined) delete process.env[key];
      else process.env[key] = value;
    }
  }
});
after(async () => { if (fixture) await rm(fixture, {recursive:true, force:true}); });

async function waitFor(predicate, message) {
  for (let i=0; i<300; i++) {
    if (predicate()) return;
    await delay(10);
  }
  assert.fail(message);
}

test('a stalled TCP reader cannot close its UDP bridge or hold another peer, and resumes with fresh bytes',
  {timeout:20000}, async () => {
    const listener = new WebSocketServer({host:'127.0.0.1', port:0, perMessageDeflate:false});
    await once(listener, 'listening');
    const url = `ws://127.0.0.1:${listener.address().port}`;
    let slow, fast, slowPeer, fastPeer;
    const slowMarkers = new Set(), fastMarkers = new Set();
    let slowCloses=0, dropped=0, maxBuffered=0, fastPackets=0;
    try {
      let accepted = once(listener, 'connection');
      slow = new WebSocket(url); await once(slow, 'open');
      [slowPeer] = await accepted;
      accepted = once(listener, 'connection');
      fast = new WebSocket(url); await once(fast, 'open');
      [fastPeer] = await accepted;
      slow.on('close', () => slowCloses++);
      slow.on('error', () => {});
      fast.on('error', () => {});
      slow.on('message', data => { if (data.length<100) slowMarkers.add(data.toString()); });
      fast.on('message', data => { fastPackets++; if (data.length<100) fastMarkers.add(data.toString()); });
      slow._socket.pause(); // Only this test-owned TCP stream stops consuming bytes.
      for (let sequence=0; sequence<4096 && slowPeer.readyState===WebSocket.OPEN; sequence++) {
        const packet = Buffer.alloc(16384, sequence & 255);
        packet.writeUInt32LE(sequence, 0);
        if (!forwardDatagram(slowPeer, packet)) dropped++;
        maxBuffered = Math.max(maxBuffered, slowPeer.bufferedAmount);
        if (sequence % 32 === 0) {
          forwardDatagram(fastPeer, packet);
          await nextTurn();
        }
      }
      assert.ok(dropped>0, 'the real stalled connection must exercise congestion');
      assert.ok(maxBuffered<=256*1024+16384+16,
        `queued ${maxBuffered} bytes instead of bounding latency and dropping UDP datagrams`);
      assert.equal(slowPeer.readyState, WebSocket.OPEN, 'congestion must not disconnect the player');
      assert.equal(slowCloses, 0);
      assert.ok(slow._socket.isPaused());
      await waitFor(() => fastPeer.bufferedAmount===0, 'the independent peer did not drain');
      assert.ok(forwardDatagram(fastPeer, Buffer.from('fast-peer-remains-live')));
      await waitFor(() => fastMarkers.has('fast-peer-remains-live'), 'another peer was held by the stalled reader');
      assert.ok(fastPackets>0);
      slow._socket.resume();
      await waitFor(() => slowPeer.bufferedAmount===0, 'the resumed connection did not drain');
      const fresh = Buffer.from('fresh-opaque-datagram-after-congestion');
      assert.ok(forwardDatagram(slowPeer, fresh));
      await waitFor(() => slowMarkers.has(fresh.toString()), 'fresh datagram bytes did not arrive after recovery');
      assert.equal(slowCloses, 0);
      console.log(JSON.stringify({fixture:'real-local-TCP', maxBuffered, dropped, fastPackets,
        recoveredWithoutReconnect:true, gameSimulation:false}));
    } finally {
      slow?.terminate(); fast?.terminate();
      for (const peer of listener.clients) peer.terminate();
      await new Promise(resolve => listener.close(resolve));
    }
  });
