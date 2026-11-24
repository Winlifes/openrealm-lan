'use strict';
const el=id=>document.getElementById(id);
let maps=[], roomsSnapshot='', refreshTimer;
el('nickname').value=localStorage.getItem('winlife-wc3-nickname')||'';
function nickname(){const value=el('nickname').value.replace(/["\\+;\x00-\x1f]/g,' ').trim().slice(0,24);if(!value){el('nickname').focus();throw Error('请先设置昵称');}localStorage.setItem('winlife-wc3-nickname',value);return value;}
async function api(path,options){const response=await fetch('api/'+path,{cache:'no-store',...options});const data=await response.json();if(!response.ok)throw Error(data.error||'联机服务暂时不可用');return data;}
function showMaps(){const selected=el('map').value,search=el('map-search').value.trim().toLowerCase();el('map').replaceChildren();for(const m of maps){if(search&&!m.name.toLowerCase().includes(search))continue;const option=document.createElement('option');option.value=m.id;option.textContent=m.name+' · '+m.players+' 人';el('map').append(option);}if([...el('map').options].some(x=>x.value===selected))el('map').value=selected;}
async function refresh(){try{const rooms=await api('rooms');const snapshot=JSON.stringify(rooms);if(snapshot!==roomsSnapshot){roomsSnapshot=snapshot;el('rooms').replaceChildren();if(!rooms.length){const empty=document.createElement('p');empty.className='room-empty';empty.textContent='还没有房间，创建一个邀请朋友加入。';el('rooms').append(empty);}for(const room of rooms){const row=document.createElement('div');row.className='room-row';const detail=document.createElement('div'),title=document.createElement('strong'),text=document.createElement('p'),state=document.createElement('small'),button=document.createElement('button');title.textContent=room.name;text.textContent=room.map+' · '+room.players+'/'+room.slots+' 人';state.textContent=({starting:'正在准备',waiting:'等待玩家',playing:'对局中',error:'准备失败'})[room.status]||room.status;if(room.error)state.textContent+='：'+room.error;detail.append(title,text,state);button.textContent='加入';button.disabled=room.status!=='waiting';button.onclick=()=>{try{nickname();location.assign(room.url);}catch(error){el('room-message').textContent=error.message;}};row.append(detail,button);el('rooms').append(row);}}}catch(error){el('room-message').textContent=error.message;}}
el('create-room').onsubmit=event=>{
  event.preventDefault();
  try {
    const player = nickname();
    const map = maps.find(item => item.id === el('map').value);
    if (!map) throw Error('请选择地图');
    const draft = {mapId:map.id, mapName:map.name, name:el('room-name').value.trim() || player+' 的房间'};
    sessionStorage.setItem('winlife-wc3-room-draft', JSON.stringify(draft));
    location.assign('play/game/?'+new URLSearchParams({create:map.id}));
  } catch (error) { el('room-message').textContent=error.message; }
};
el('map-search').oninput=showMaps;el('refresh').onclick=refresh;
api('maps').then(data=>{maps=data;showMaps();}).catch(error=>{el('room-message').textContent=error.message;});
function scheduleRefresh(){
  clearTimeout(refreshTimer);
  refreshTimer=setTimeout(async()=>{
    if(document.visibilityState==='visible')await refresh();
    scheduleRefresh();
  },2500);
}
void refresh();scheduleRefresh();
