'use strict';
const el=id=>document.getElementById(id);
let maps=[], pending=null, submitting=false, roomsSnapshot='', refreshTimer;
el('nickname').value=localStorage.getItem('winlife-wc3-nickname')||'';
function nickname(){const value=el('nickname').value.replace(/["\\+;\x00-\x1f]/g,' ').trim().slice(0,24);if(!value){el('nickname').focus();throw Error('请先设置昵称');}localStorage.setItem('winlife-wc3-nickname',value);return value;}
async function api(path,options){const response=await fetch('/api/'+path,{cache:'no-store',...options});const data=await response.json();if(!response.ok)throw Error(data.error||'联机服务暂时不可用');return data;}
function showMaps(){const selected=el('map').value,search=el('map-search').value.trim().toLowerCase();el('map').replaceChildren();for(const m of maps){if(search&&!m.name.toLowerCase().includes(search))continue;const option=document.createElement('option');option.value=m.id;option.textContent=m.name+' · '+m.players+' 人';el('map').append(option);}if([...el('map').options].some(x=>x.value===selected))el('map').value=selected;}
async function refresh(){const pendingRoom=pending;try{const rooms=await api('rooms');const snapshot=JSON.stringify(rooms);if(snapshot!==roomsSnapshot){roomsSnapshot=snapshot;el('rooms').replaceChildren();if(!rooms.length){const empty=document.createElement('p');empty.className='room-empty';empty.textContent='还没有房间，创建一个邀请朋友加入。';el('rooms').append(empty);}for(const room of rooms){const row=document.createElement('div');row.className='room-row';const detail=document.createElement('div'),title=document.createElement('strong'),text=document.createElement('p'),state=document.createElement('small'),button=document.createElement('button');title.textContent=room.name;text.textContent=room.map+' · '+room.players+'/'+room.slots+' 人';state.textContent=({starting:'正在准备',waiting:'等待玩家',playing:'对局中',error:'准备失败'})[room.status]||room.status;if(room.error)state.textContent+='：'+room.error;detail.append(title,text,state);button.textContent='加入';button.disabled=room.status!=='waiting';button.onclick=()=>{try{nickname();location.assign(room.url);}catch(error){el('room-message').textContent=error.message;}};row.append(detail,button);el('rooms').append(row);}}
if(pendingRoom&&pending===pendingRoom){const room=rooms.find(r=>r.id===pendingRoom);if(room?.status==='waiting')location.assign(room.url);else if(!room||room.status==='error'){pending=null;submitting=false;el('create').disabled=false;el('room-message').textContent=room?.error||'房间准备失败，请重试';}}}catch(error){el('room-message').textContent=error.message;}}
el('create-room').onsubmit=async event=>{event.preventDefault();if(submitting)return;try{const name=nickname();if(!el('map').value)throw Error('请选择地图');submitting=true;el('create').disabled=true;el('room-message').textContent='正在准备房间，请稍候…';const room=await api('rooms',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mapId:el('map').value,name:el('room-name').value.trim()||name+' 的房间'})});pending=room.id;scheduleRefresh();}catch(error){submitting=false;el('create').disabled=false;el('room-message').textContent=error.message;}};
el('map-search').oninput=showMaps;el('refresh').onclick=refresh;
api('maps').then(data=>{maps=data;showMaps();}).catch(error=>{el('room-message').textContent=error.message;});
function scheduleRefresh(){
  clearTimeout(refreshTimer);
  refreshTimer=setTimeout(async()=>{
    if(document.visibilityState==='visible'||pending)await refresh();
    scheduleRefresh();
  },pending?350:2500);
}
void refresh();scheduleRefresh();
