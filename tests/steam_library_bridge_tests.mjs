import {readFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
import vm from 'node:vm';

const header = await readFile(new URL('../src/platform/steam_library_bridge.hpp', import.meta.url), 'utf8');
const source = header.split('R"STEAM_BRIDGE(')[1].split(')STEAM_BRIDGE"')[0];
function fixture() {
  const account = '76561198000000001';
  const entries = new Map(); let writes = 0, mutate = 0;
  const store = {
    collectionsFromStorage:new Map(), GetCollection(id) { return this.collectionsFromStorage.get(id); },
    BIsSystemCollectionId:id => ['favorite','hidden'].includes(id), BIsPartnerCollectionId:()=>false,
    SetAppsAsHidden(ids, yes) { const c = this.GetCollection('hidden'); yes ? c.AddApps(ids.map(appid=>({appid}))) : c.RemoveApps(ids.map(appid=>({appid}))); },
    SetAppsAsFavorite(ids, yes) { const c = this.GetCollection('favorite'); yes ? c.AddApps(ids.map(appid=>({appid}))) : c.RemoveApps(ids.map(appid=>({appid}))); }
  };
  const make = (id, name, saved = true) => {
    const c = {m_strId:id,get id() { return this.m_strId; },displayName:name,apps:new Set(), internalAddedList:new Set(),
      async Save() { ++writes; store.collectionsFromStorage.set(this.id,this); await new Promise(r=>setTimeout(r,10)); entries.set('user-collections.'+this.id,{value:JSON.stringify({id:this.id,name:this.displayName,added:[...this.apps],removed:[]})}); },
      AddApps(values) { ++mutate; for(const v of values) this.apps.add(v.appid); void this.Save(); },
      RemoveApps(values) { ++mutate; for(const v of values) this.apps.delete(v.appid); void this.Save(); },
      AsDragDropCollection() { return this; }
    };
    if (saved) store.collectionsFromStorage.set(id,c); return c;
  };
  store.NewUnsavedCollection = name => make('uc-generated',name,false);
  for(const [id,name] of [['favorite','Favorite'],['hidden','Hidden'],['uc-test','My collection']]) make(id,name);
  const app = {GetServicesInitialized:()=>true,BHasCurrentUser:()=>true,GetCurrentUser:()=>({strSteamID:account})};
  const apps = {m_bIsInitialized:true,GetAppOverviewByAppID:id=>id===620?{appid:id,BIsModOrShortcut:()=>false}:null,
    m_privateAppsObserver:{getCurrentResult:()=>({isSuccess:true})},m_setPrivateApps:new Set([999])};
  const context = vm.createContext({window:{App:app,collectionStore:store,appStore:apps,cloudStorageInternalState:{m_mapDirtyKeys:new Map([[1,new Set(['pending'])]])}},
    fetch:async()=>({ok:true,json:async()=>[...entries]}),AbortController,setTimeout,clearTimeout});
  return {account,app,apps,store,entries,run:vm.runInContext('('+source+')',context), writes:()=>writes,mutations:()=>mutate};
}
{
  const f=fixture();
  let r=await f.run({account:'different',changes:[{kind:'hidden',app:620,value:true}]});
  assert.equal(r.error,'account-changed'); assert.equal(f.writes(),0);
  r=await f.run({account:f.account,changes:[{kind:'hidden',app:620,value:true}]});
  assert.equal(r.ok,true); assert.equal(r.ack.length,1); assert.equal(r.hidden.includes(620),true); assert.equal(r.privateApps.includes(999),true);
  const name='Name "quoted"\\ x\n ); globalThis.pwned=true; //';
  r=await f.run({account:f.account,changes:[{kind:'name',collection:'user-collections.uc-test',name},
    {kind:'member',collection:'user-collections.uc-test',app:620,value:true},{kind:'hidden',app:620,value:false}]});
  assert.equal(r.ack.length,3); assert.equal(r.hidden.length,0); assert.equal(r.collections.find(c=>c.id==='user-collections.uc-test').name,name);
  assert.equal(JSON.parse(f.entries.get('user-collections.uc-test').value).added.includes(620),true);
  assert.equal(r.cloudPending,true); // Local-save acknowledgement is separate from cloud completion.
  r=await f.run({account:f.account,changes:[{kind:'member',collection:'user-collections.missing',app:620,value:true},
    {kind:'member',collection:'user-collections.favorite',app:620,value:true}]});
  assert.equal(r.error,'collection-missing'); assert.deepEqual([...r.ack],[1]);
  // An app Steam has no record of (a removed demo) is reported apart and never holds the queue.
  r=await f.run({account:f.account,changes:[{kind:'hidden',app:123,value:true},{kind:'member',collection:'user-collections.favorite',app:620,value:false}]});
  assert.equal(r.error,''); assert.deepEqual([...r.unavailable],[0]); assert.deepEqual([...r.ack],[1]);
  assert.equal(f.store.GetCollection('favorite').apps.has(620),false);
  const removed=f.mutations();
  r=await f.run({account:f.account,changes:[{kind:'hidden',app:123,value:true}]});
  assert.equal(r.error,''); assert.deepEqual([...r.unavailable],[0]); assert.equal(f.mutations(),removed);
  const before=removed;
  f.apps.m_privateAppsObserver.getCurrentResult=()=>({isSuccess:false});
  r=await f.run({account:f.account,changes:[{kind:'hidden',app:620,value:true}]});
  assert.equal(r.error,'privacy-unavailable'); assert.equal(f.mutations(),before);
}
{
  const f=fixture();
  // Optimistic UI already changed but the previous save failed: retry must actually persist it.
  f.store.GetCollection('hidden').apps.add(620);
  const r=await f.run({account:f.account,changes:[{kind:'hidden',app:620,value:true}]});
  assert.equal(r.ack.length,1); assert.equal(f.writes(),1);
}
console.log('Steam bridge: account guards, native save readback, retry, names and privacy passed');
{
  const f=fixture(), id='user-collections.uc-launchme-0123456789abcdef';
  let result=await f.run({account:f.account,changes:[{kind:'create',collection:id,name:'Yakuza'}]});
  assert.equal(result.ack.length,1); assert.equal(f.store.collectionsFromStorage.size,4);
  assert.equal(JSON.parse(f.entries.get(id).value).name,'Yakuza');
  result=await f.run({account:f.account,changes:[{kind:'create',collection:id,name:'Yakuza'}]});
  assert.equal(result.ack.length,1); assert.equal(f.store.collectionsFromStorage.size,4); // Lost reply/restart: same durable id.
  result=await f.run({account:f.account,changes:[{kind:'create',collection:id+'00',name:'Yakuza'}]});
  assert.equal(result.error,'name-conflict'); assert.equal(f.store.collectionsFromStorage.size,4);
  result=await f.run({account:f.account,changes:[{kind:'member',collection:id,app:620,value:true}]});
  assert.equal(result.ack.length,1); assert.deepEqual(JSON.parse(f.entries.get(id).value).added,[620]);
}
console.log('Steam collection creation, duplicate protection and membership passed');
