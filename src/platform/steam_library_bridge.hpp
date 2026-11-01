#pragma once

// Steam's own collection methods perform mutations and schedule CloudConfigStore uploads.
// No filesystem edits, injected persistent handlers, credentials, or private-game toggles.
inline constexpr char steam_library_bridge[] = R"STEAM_BRIDGE(
async function(request) {
  const fail = error => ({ok:false,error});
  const app = window.App, store = window.collectionStore, apps = window.appStore;
  const identity = () => String(app?.GetCurrentUser?.()?.strSteamID || '');
  if (!app?.GetServicesInitialized?.() || !app.BHasCurrentUser?.() || apps?.m_bIsInitialized !== true ||
      !store?.collectionsFromStorage || !store.GetCollection?.('favorite') || !store.GetCollection?.('hidden')) return fail('not-ready');
  if (identity() !== request.account) return fail('account-changed');
  const privacyReady = apps.m_privateAppsObserver?.getCurrentResult?.();
  if (privacyReady?.isSuccess !== true) return fail('privacy-unavailable');
  const validID = id => Number.isInteger(id) && id > 0 && id < 0x80000000;
  const member = (collection, id) => collection.apps?.has(id) === true;
  const readNative = async () => {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 2000);
    try {
      const response = await fetch('/cloudstorage/cloud-storage-namespace-1.json', {cache:'no-store',signal:controller.signal});
      if (!response.ok) throw new Error('cache');
      const value = await response.json();
      if (!Array.isArray(value)) throw new Error('cache');
      return new Map(value);
    } finally { clearTimeout(timer); }
  };
  const persisted = (entries, id, change) => {
    const entry = entries.get('user-collections.' + id);
    if (!entry || entry.is_deleted || typeof entry.value !== 'string') return false;
    const data = JSON.parse(entry.value);
    if (change.kind === 'create') return data.id === id;
    if (change.kind === 'name') return data.name === change.name;
    const added = Array.isArray(data.added) && data.added.includes(change.app);
    const removed = Array.isArray(data.removed) && data.removed.includes(change.app);
    return change.value ? added && !removed : !added;
  };
  const ack = [], unavailable = [];
  let error = '';
  // Bounded serial writes: every operation uses the current collection object, never a whole stale snapshot.
  for (const [index, change] of request.changes.entries()) {
    if (identity() !== request.account) return fail('account-changed');
    if (!['member','name','hidden','create'].includes(change.kind)) { error = 'unsupported'; continue; }
    const id = change.kind === 'hidden' ? 'hidden' : change.collection.replace(/^user-collections\./, '');
    let collection = store.GetCollection(id);
    if (change.kind === 'create') {
      if (!/^uc-launchme-[a-f0-9]{16,64}$/.test(id) || typeof change.name !== 'string' || !change.name.trim() || change.name.length > 200) {
        error = 'unsupported'; continue;
      }
      if (!collection) {
        if ([...store.collectionsFromStorage.values()].some(c => c.displayName === change.name)) { error = 'name-conflict'; continue; }
        if (typeof store.NewUnsavedCollection !== 'function') { error = 'unsupported'; continue; }
        collection = store.NewUnsavedCollection(change.name, undefined, []);
        // Steam's id getter reads m_strId. Set our durable id before the first Save so a retry
        // after a lost reply reuses the same collection instead of creating a duplicate.
        if (!collection || typeof collection.m_strId !== 'string' || collection.m_strId !== collection.id ||
            store.collectionsFromStorage.has(collection.id)) { error = 'unsupported'; continue; }
        collection.m_strId = id;
        if (collection.id !== id) { error = 'unsupported'; continue; }
      }
      try { await collection.Save(); } catch { error = 'save-failed'; break; }
      if (identity() !== request.account) return fail('account-changed');
      collection = store.GetCollection(id);
    }
    if (!collection || !store.collectionsFromStorage.has(id)) { error = 'collection-missing'; continue; }
    if (change.kind === 'name') {
      if (store.BIsSystemCollectionId(id) || store.BIsPartnerCollectionId(id) || !change.name || change.name.length > 200) {
        error = 'unsupported'; continue;
      }
      if ([...store.collectionsFromStorage.values()].some(c => c.id !== id && c.displayName === change.name)) {
        error = 'name-conflict'; continue;
      }
      collection.displayName = change.name;
      try { await collection.Save(); } catch { error = 'save-failed'; break; }
    } else if (change.kind !== 'create') {
      if (!validID(change.app) || typeof change.value !== 'boolean') { error = 'unsupported'; continue; }
      const overview = apps.GetAppOverviewByAppID(change.app);
      // No record of the app (a removed demo, an ended playtest): Steam cannot apply it, ever.
      // Reported apart, not as an error, so it never holds the rest of the queue.
      if (!overview || overview.BIsModOrShortcut()) { unavailable.push(index); continue; }
      if (member(collection, change.app) === change.value) {
        // A previous fire-and-forget save may have failed after updating the UI. Retry that
        // persisted intent with an awaitable save, rather than getting stuck on the optimistic value.
        try {
          const native = await readNative();
          if (identity() !== request.account) return fail('account-changed');
          collection = store.GetCollection(id);
          if (collection && member(collection, change.app) === change.value && !persisted(native, id, change)) await collection.Save();
        } catch { error = 'save-failed'; break; }
      } else {
        if (id === 'hidden') store.SetAppsAsHidden([change.app], change.value);
        else if (id === 'favorite') store.SetAppsAsFavorite([change.app], change.value);
        else {
          const edit = collection.AsDragDropCollection();
          if (!edit) { error = 'unsupported'; break; }
          if (change.value) edit.AddApps([overview]); else edit.RemoveApps([overview]);
        }
      }
    }
    // SetAppsAsHidden/AddApps do not return their save promise. Verify Steam's persisted record,
    // not just the optimistic JS membership. Steam itself owns eventual cloud upload/retries.
    let verified = false;
    const deadline = Date.now() + 4500;
    while (Date.now() < deadline) {
      if (identity() !== request.account) return fail('account-changed');
      try {
        const native = await readNative();
        if (identity() !== request.account) return fail('account-changed');
        collection = store.GetCollection(id);
        const current = collection && (change.kind === 'create' ? collection.id === id : change.kind === 'name' ? collection.displayName === change.name : member(collection, change.app) === change.value);
        if (current && persisted(native, id, change)) { verified = true; break; }
      } catch {}
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    if (!verified) { error = 'save-unconfirmed'; break; }
    ack.push(index);
  }
  if (identity() !== request.account) return fail('account-changed');
  const collections = [];
  for (const c of store.collectionsFromStorage.values()) {
    if (c.id === 'hidden' || (store.BIsSystemCollectionId(c.id) && c.id !== 'favorite') || store.BIsPartnerCollectionId(c.id)) continue;
    collections.push({id:'user-collections.' + c.id, name:c.displayName, members:[...c.apps].filter(id => Number.isInteger(id) && id > 0 && id <= 0xffffffff)});
  }
  const hidden = [...store.GetCollection('hidden').apps].filter(validID);
  // Privacy is independent of "hidden"; never mutate it. A loaded observer is required for a complete snapshot.
  const privacy = apps.m_privateAppsObserver?.getCurrentResult?.();
  if (privacy?.isSuccess !== true) return {ok:false,error:'privacy-unavailable'};
  const privateApps = [...apps.m_setPrivateApps].filter(validID);
  const cloud = window.cloudStorageInternalState;
  return {ok:true,account:identity(),collections,hidden,privateApps,ack,unavailable,error,
    cloudPending:!!cloud?.m_mapDirtyKeys?.get(1)?.size};
}
)STEAM_BRIDGE";
