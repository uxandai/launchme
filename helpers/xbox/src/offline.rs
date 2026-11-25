// Offline play on the account's designated offline PC, as the Xbox app's "Offline permissions"
// (one PC per account, a few changes a year). Ported from the Nexus backend's license cache and
// `offline-pc` command (https://github.com/domsvoboda/nexus-backend, GPL-3.0, its authors and the
// Xodus authors).
//
// Microsoft issues the designated device's content license as Machine-bound and lease-required,
// with an offline lease (its policy says `isOffline`, expiry about 30 days, renewed by every online
// launch). The cache keeps the license block (its content keys stay wrapped by this device's key, so
// the file alone decrypts nothing) with the lease exactly as issued. Nothing written here decides
// whether an entry may be used: every load checks the lease again (its type, expiry and device),
// the account and device registration, and that the clock did not move behind the last use. Every
// revocation the helper learns of removes the entries: this device not (or no longer) designated,
// a key license that is not device-bound, another account, a Store device-limit refusal.
//
// <profile>/license-cache/<content id>.json: one entry per package, sealed with an HMAC keyed by
// the device registration's secret (in the token store, never in the cache).
// <profile>/offline.json: what Launchme shows (the designation, until when each game starts offline).
// It grants nothing; the cache entries are what the launch checks.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use base64::prelude::*;
use hmac::{Hmac, Mac};
use serde_json::{Value, json};
use sha2::Sha256;
use xodus::tokens::TokenManager;

use crate::licensing::{self, Document, LeaseCheck};
use crate::output::{Failure, Outcome};

const SCHEMA: u64 = 1;
const MAX_FILE_BYTES: u64 = 160 * 1024;
const MAX_BLOCK_CHARS: usize = 48 * 1024;
pub const MAX_LEASE_CHARS: usize = 96 * 1024;
const OWNER_FILE: &str = "owner.holder";
const REVOKED_FILE: &str = ".revoked";
const INDEX_FILE: &str = "offline.json";
const DOMAIN: &[u8] = b"launchme-xbox-offline-license-v1\0";
// Upper bound on offline use after Microsoft issued the lease, whatever the lease says.
pub const MAX_OFFLINE_SECONDS: u64 = 30 * 24 * 60 * 60;
// Backwards clock movement tolerated before a stored license is refused.
pub const CLOCK_SKEW_SECONDS: u64 = 10 * 60;

pub fn now() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

// Who an entry belongs to: the signed-in account and this device registration.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Holder {
    pub puid: String,
    pub device: String,
}

// The holder from the token store alone (no network): None without a user or a device registration.
pub fn holder(tokens: &TokenManager) -> Option<(Holder, Vec<u8>)> {
    let user = tokens.get_user().ok()?;
    let device = tokens.get_device_license().ok()?;
    // Microsoft often leaves the license's DeviceId empty: the device account's PUID, then the
    // hardware id, stand in. A new registration (a reset) changes it.
    let id = [("device", &device.device_id), ("puid", &device.puid), ("hwid", &device.hwid)]
        .into_iter()
        .find(|(_, value)| !value.is_empty())
        .map(|(kind, value)| format!("{kind}:{value}"))?;
    if user.puid.is_empty() || device.password.is_empty() {
        return None;
    }
    Some((Holder { puid: user.puid, device: id }, device.password.into_bytes()))
}

pub fn cache_root(profile: &Path) -> PathBuf {
    profile.join("license-cache")
}

pub struct Cached {
    pub splicense_block: String,
    pub until: u64,
}

// Why no stored license is usable (already acted on: an entry that can never be valid again is gone).
#[derive(Debug, PartialEq, Eq)]
pub enum Unusable {
    Missing,
    Expired,
    Invalid,
    Account,
    Clock,
}

impl Unusable {
    pub fn message(&self) -> &'static str {
        match self {
            Unusable::Missing => "no offline license is stored for this game: start it once while online on the designated offline PC",
            Unusable::Expired => "the offline license expired: connect to the internet and start the game once",
            Unusable::Invalid => "the stored offline license is not valid: connect to the internet and start the game once",
            Unusable::Account => "the stored offline licenses belonged to another account or device and were removed",
            Unusable::Clock => "the system clock is behind the last use of this game: correct the date and time",
        }
    }
}

fn io_error() -> Failure {
    Failure::new("storage", "the offline license cache could not be accessed")
}

fn seal(secret: &[u8], data: &[u8]) -> Option<String> {
    if secret.is_empty() {
        return None;
    }
    let mut mac = Hmac::<Sha256>::new_from_slice(secret).ok()?;
    mac.update(DOMAIN);
    mac.update(data);
    Some(BASE64_STANDARD.encode(mac.finalize().into_bytes()))
}

fn verify(secret: &[u8], data: &[u8], tag: &str) -> bool {
    if secret.is_empty() || tag.len() != 44 {
        return false;
    }
    let Ok(tag) = BASE64_STANDARD.decode(tag) else { return false };
    let Ok(mut mac) = Hmac::<Sha256>::new_from_slice(secret) else { return false };
    mac.update(DOMAIN);
    mac.update(data);
    mac.verify_slice(&tag).is_ok()
}

fn read_bounded(path: &Path, limit: u64) -> std::io::Result<Vec<u8>> {
    let metadata = std::fs::symlink_metadata(path)?;
    if !metadata.is_file() || metadata.len() > limit {
        return Err(std::io::ErrorKind::InvalidData.into());
    }
    let bytes = std::fs::read(path)?;
    if bytes.len() as u64 > limit {
        return Err(std::io::ErrorKind::InvalidData.into());
    }
    Ok(bytes)
}

// A fresh private file next to the target, then renamed over it.
fn write_atomic(path: &Path, bytes: &[u8]) -> Result<(), Failure> {
    use std::io::Write;
    let parent = path.parent().ok_or_else(io_error)?;
    let part = parent.join(format!(".{}.tmp", uuid::Uuid::new_v4().simple()));
    let result = (|| -> std::io::Result<()> {
        let mut options = std::fs::OpenOptions::new();
        options.write(true).create_new(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let mut file = options.open(&part)?;
        file.write_all(bytes)?;
        file.sync_all()?;
        drop(file);
        std::fs::rename(&part, path)
    })();
    if result.is_err() {
        let _ = std::fs::remove_file(&part);
        return Err(io_error());
    }
    Ok(())
}

fn ensure_dir(path: &Path) -> Result<(), Failure> {
    if std::fs::symlink_metadata(path).is_ok_and(|m| !m.is_dir()) {
        return Err(io_error());
    }
    let mut builder = std::fs::DirBuilder::new();
    builder.recursive(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::DirBuilderExt;
        builder.mode(0o700);
    }
    builder.create(path).map_err(|_| io_error())
}

// A Store product id (BigId), as Launchme checks it.
pub fn valid_product(id: &str) -> bool {
    (9..=16).contains(&id.len()) && id.bytes().all(|b| b.is_ascii_alphanumeric())
}

fn valid_content_id(id: &str) -> bool {
    !id.is_empty() && id.len() <= 64 && id.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')
}

pub struct LicenseCache<'a> {
    root: PathBuf,
    secret: &'a [u8],
}

impl<'a> LicenseCache<'a> {
    pub fn new(root: &Path, secret: &'a [u8]) -> Self {
        Self { root: root.to_path_buf(), secret }
    }

    fn entry(&self, content_id: &str) -> Result<PathBuf, Failure> {
        if !valid_content_id(content_id) {
            return Err(Failure::new("internal", "content id not valid for the offline license cache"));
        }
        Ok(self.root.join(format!("{}.json", content_id.to_ascii_lowercase())))
    }

    fn owner(&self) -> Option<Holder> {
        let value: Value = serde_json::from_slice(&read_bounded(&self.root.join(OWNER_FILE), 4096).ok()?).ok()?;
        Some(Holder { puid: value["puid"].as_str()?.to_string(), device: value["device"].as_str()?.to_string() })
    }

    fn sealed(&self, mut value: Value) -> Result<Vec<u8>, Failure> {
        value.as_object_mut().ok_or_else(io_error)?.remove("integrity");
        let tag = seal(self.secret, value.to_string().as_bytes()).ok_or_else(io_error)?;
        value["integrity"] = json!(tag);
        Ok(value.to_string().into_bytes())
    }

    fn authentic(&self, value: &mut Value) -> bool {
        let Some(object) = value.as_object_mut() else { return false };
        let Some(Value::String(tag)) = object.remove("integrity") else { return false };
        verify(self.secret, value.to_string().as_bytes(), &tag)
    }

    // Claims the cache for this holder: anything another account or registration left is removed first.
    fn claim(&self, holder: &Holder) -> Result<(), Failure> {
        if self.owner().as_ref() == Some(holder) && !self.root.join(REVOKED_FILE).exists() {
            return Ok(());
        }
        revoke_all(&self.root)?;
        ensure_dir(&self.root)?;
        let owner = json!({ "schema": SCHEMA, "puid": holder.puid, "device": holder.device });
        write_atomic(&self.root.join(OWNER_FILE), owner.to_string().as_bytes())
    }

    // Records a license Microsoft just issued with the leases that came with it; only an offline
    // lease `check_lease` accepts is kept (the earliest-expiring one). None: nothing usable offline.
    pub fn store(&self, content_id: &str, holder: &Holder, key: &Document, leases: &[String], now: u64) -> Result<Option<u64>, Failure> {
        ensure_dir(&self.root)?;
        let path = self.entry(content_id)?;
        self.claim(holder)?;
        let mut refused = Vec::new();
        let lease = leases
            .iter()
            .filter_map(|document| match licensing::check_lease(document, key, now, now) {
                LeaseCheck::Valid(until) => Some((until, document)),
                LeaseCheck::Expired => {
                    refused.push("expired");
                    None
                }
                LeaseCheck::Invalid(reason) => {
                    refused.push(reason);
                    None
                }
            })
            .min_by_key(|(until, _)| *until);
        let Some((until, lease)) = lease else {
            if !refused.is_empty() {
                crate::output::log(&format!("offline: no usable lease ({})", refused.join(", ")));
            }
            let _ = std::fs::remove_file(&path);
            return Ok(None);
        };
        if key.splicense_block.is_empty() || key.splicense_block.len() > MAX_BLOCK_CHARS {
            let _ = std::fs::remove_file(&path);
            return Ok(None);
        }
        let entry = json!({
            "schema": SCHEMA,
            "contentId": content_id.to_ascii_lowercase(),
            "puid": holder.puid,
            "device": holder.device,
            "issuedAt": now,
            "lastUsedAt": now,
            "until": until,
            "key": {
                "type": key.binding_type,
                "deviceId": key.device_id,
                "leaseRequired": key.lease_required,
                "beginDate": key.begin_date,
                "expirationDate": key.expiration_date,
            },
            "lease": lease,
            "splicenseBlock": key.splicense_block,
        });
        write_atomic(&path, &self.sealed(entry)?)?;
        Ok(Some(until))
    }

    // The stored license when its lease still allows offline use now.
    pub fn load(&self, content_id: &str, holder: &Holder, now: u64) -> Result<Result<Cached, Unusable>, Failure> {
        let path = self.entry(content_id)?;
        if std::fs::symlink_metadata(&self.root).is_ok_and(|m| !m.is_dir()) {
            return Err(io_error());
        }
        if self.root.join(REVOKED_FILE).exists() {
            return Ok(Err(Unusable::Invalid));
        }
        if self.owner().is_some_and(|owner| owner != *holder) {
            revoke_all(&self.root)?;
            return Ok(Err(Unusable::Account));
        }
        let invalid = |path: &Path| {
            let _ = std::fs::remove_file(path);
            Ok(Err(Unusable::Invalid))
        };
        let bytes = match read_bounded(&path, MAX_FILE_BYTES) {
            Ok(bytes) => bytes,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(Err(Unusable::Missing)),
            Err(_) => return invalid(&path),
        };
        let Ok(mut value) = serde_json::from_slice::<Value>(&bytes) else { return invalid(&path) };
        if !self.authentic(&mut value) {
            return invalid(&path);
        }
        let text = |key: &str| value[key].as_str().map(str::to_string);
        let number = |key: &str| value[key].as_u64();
        let (Some(block), Some(lease), Some(issued_at), Some(last_used_at)) =
            (text("splicenseBlock"), text("lease"), number("issuedAt"), number("lastUsedAt"))
        else {
            return invalid(&path);
        };
        if number("schema") != Some(SCHEMA)
            || text("contentId").as_deref() != Some(content_id.to_ascii_lowercase().as_str())
            || block.is_empty()
            || block.len() > MAX_BLOCK_CHARS
        {
            return invalid(&path);
        }
        if text("puid").as_deref() != Some(holder.puid.as_str()) || text("device").as_deref() != Some(holder.device.as_str()) {
            revoke_all(&self.root)?;
            return Ok(Err(Unusable::Account));
        }
        // A clock set back must not stretch the lease: refused while behind the last use (kept).
        if now.saturating_add(CLOCK_SKEW_SECONDS) < issued_at.max(last_used_at) {
            return Ok(Err(Unusable::Clock));
        }
        let stored = &value["key"];
        let field = |key: &str| stored[key].as_str().map(str::to_string);
        let key = Document {
            license_type: "Full".into(),
            binding_type: field("type").unwrap_or_default(),
            device_id: field("deviceId"),
            lease_required: field("leaseRequired"),
            begin_date: field("beginDate"),
            expiration_date: field("expirationDate"),
            custom_policies: None,
            splicense_block: block.clone(),
        };
        let until = match licensing::check_lease(&lease, &key, issued_at, now) {
            LeaseCheck::Valid(until) => until,
            LeaseCheck::Expired => {
                let _ = std::fs::remove_file(&path);
                return Ok(Err(Unusable::Expired));
            }
            LeaseCheck::Invalid(reason) => {
                crate::output::log(&format!("offline: stored lease not valid ({reason})"));
                return invalid(&path);
            }
        };
        // The use is recorded so a later clock change cannot move behind it; one that cannot be
        // recorded is not granted.
        value["lastUsedAt"] = json!(last_used_at.max(now));
        write_atomic(&path, &self.sealed(value)?)?;
        Ok(Ok(Cached { splicense_block: block, until }))
    }

    // Microsoft refused the content while online: its entry goes.
    pub fn purge(&self, content_id: &str) -> bool {
        self.entry(content_id).is_ok_and(|path| std::fs::remove_file(path).is_ok())
    }
}

// Removes every stored license and the owner record. A marker written first keeps the cache closed
// if the process stops half way (the next claim or revoke finishes the job).
pub fn revoke_all(root: &Path) -> Result<usize, Failure> {
    match std::fs::symlink_metadata(root) {
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(0),
        Ok(m) if m.is_dir() => {}
        _ => return Err(io_error()),
    }
    let revoked = root.join(REVOKED_FILE);
    write_atomic(&revoked, b"revoked\n")?;
    let mut removed = 0;
    let mut failed = false;
    for entry in std::fs::read_dir(root).map_err(|_| io_error())?.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        let license = name.ends_with(".json");
        if !(license || name.ends_with(".tmp") || name == OWNER_FILE) {
            continue;
        }
        match std::fs::remove_file(entry.path()) {
            Ok(()) => removed += usize::from(license),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
            Err(_) => failed = true,
        }
    }
    if failed {
        return Err(io_error());
    }
    std::fs::remove_file(revoked).map_err(|_| io_error())?;
    Ok(removed)
}

// ---- offline.json: what Launchme shows ----

fn index_path(profile: &Path) -> PathBuf {
    profile.join(INDEX_FILE)
}

fn read_index(profile: &Path) -> Value {
    read_bounded(&index_path(profile), 1024 * 1024)
        .ok()
        .and_then(|bytes| serde_json::from_slice::<Value>(&bytes).ok())
        .filter(Value::is_object)
        .unwrap_or_else(|| json!({ "version": 1 }))
}

fn write_index(profile: &Path, mut value: Value) {
    value["version"] = json!(1);
    if write_atomic(&index_path(profile), value.to_string().as_bytes()).is_err() {
        crate::output::log("offline: offline.json not written");
    }
}

// The product's offline state: Some(until) when a lease is stored, None when it is not (or ended).
pub fn index_game(profile: &Path, product: &str, until: Option<u64>) {
    if !valid_product(product) {
        return;
    }
    let mut value = read_index(profile);
    let mut games: BTreeMap<String, Value> = value["games"]
        .as_object()
        .map(|m| m.iter().map(|(k, v)| (k.clone(), v.clone())).collect())
        .unwrap_or_default();
    match until {
        Some(until) => games.insert(product.to_string(), json!(until)),
        None => games.remove(product),
    };
    value["games"] = json!(games);
    write_index(profile, value);
}

fn index_clear_games(profile: &Path) {
    let mut value = read_index(profile);
    if value["games"].as_object().is_some_and(|m| m.is_empty()) {
        return;
    }
    value["games"] = json!({});
    write_index(profile, value);
}

// Every stored offline license ends (sign-out, account change, designation off, device refused).
pub fn revoke(profile: &Path) -> Result<usize, Failure> {
    let removed = revoke_all(&cache_root(profile));
    index_clear_games(profile);
    removed
}

// ---- offline-pc: read or change the designation ----
//
// The Xbox app reads and changes it through the PC Game App Front Door:
//   GET  https://beige.xboxservices.com/pcgafd/config/offlinepc
//   POST https://beige.xboxservices.com/pcgafd/config/offlinepc/{true|false}
// with an XSTS token for `http://mp.microsoft.com/` carrying device, title and user claims.

const OFFLINE_PC: &str = "https://beige.xboxservices.com/pcgafd/config/offlinepc";
const RELYING_PARTY: &str = "http://mp.microsoft.com/";
const CLIENT_ID: &str = "000000004424da1f";
const TITLE_ID: i64 = 704208617;
const MAX_BODY: usize = 64 * 1024;

// Microsoft's answer reduced to what Launchme shows. Dates before 1970 (its "no date" is
// 1601-01-01) become null.
pub fn summarize(body: &[u8]) -> Option<Value> {
    let value: Value = serde_json::from_slice(body).ok()?;
    let permissions = value.get("offlinePermissions")?;
    let enabled = permissions.get("enabled")?.as_bool()?;
    let request = permissions.get("requestData").cloned().unwrap_or(Value::Null);
    let count = |key: &str| request.get(key).and_then(Value::as_u64).filter(|n| *n <= 10_000);
    let next_change_at = request
        .get("nextTimeToAdd")
        .and_then(Value::as_str)
        .and_then(|text| chrono::DateTime::parse_from_rfc3339(text).ok())
        .map(|time| time.timestamp())
        .filter(|seconds| *seconds > 0);
    Some(json!({
        "enabled": enabled,
        "request_success": request.get("success").and_then(Value::as_bool).unwrap_or(false),
        "remaining": count("remainingTogglesForPeriod"),
        "max": count("maxTogglesPerPeriod"),
        "period_days": count("periodLengthDays"),
        "next_change_at": next_change_at,
    }))
}

// Whether the answer ends the stored licenses: not designated, or a change applied (either way the
// games need one online start again).
pub fn designation_revokes(set: Option<bool>, summary: &Value) -> bool {
    let applied = set.is_some() && summary["request_success"] == json!(true);
    applied || summary["enabled"] != json!(true)
}

pub async fn offline_pc(client: &reqwest::Client, tokens: &TokenManager, profile: &Path, set: Option<bool>, confirmed: bool) -> Outcome {
    if set.is_some() && !confirmed {
        return Err(Failure::new("usage", "changing the offline PC needs --confirm yes"));
    }
    crate::tokens::signed_in(client, tokens).await?;
    let (_auth, sisu, device) = xodus::auth::do_sisu(client, tokens, CLIENT_ID, TITLE_ID)
        .await
        .map_err(|e| Failure::network(format!("Xbox device sign-in: {e}")))?;
    let xsts = xodus::auth::get_xsts_token(Some(&device), Some(&sisu.title_token), Some(&sisu.user_token), RELYING_PARTY)
        .await
        .map_err(|e| Failure::network(format!("Xbox authorization: {e}")))?;
    let correlation = format!("{}.0", BASE64_STANDARD.encode(uuid::Uuid::new_v4().as_bytes()).trim_end_matches('='));
    let request = match set {
        None => client.get(OFFLINE_PC),
        Some(value) => client.post(format!("{OFFLINE_PC}/{value}")).header("Content-Length", "0"),
    };
    let response = request
        .header("Authorization", xsts.authorization_header_value())
        .header("Accept", "application/json")
        .header("Accept-Language", "en-US")
        .header("X-MS-API-Version", "1.0")
        .header("x-xbl-contract-version", "1")
        .header("MS-CV", correlation.as_str())
        .timeout(std::time::Duration::from_secs(30))
        .send()
        .await
        .map_err(Failure::network)?;
    let status = response.status().as_u16();
    let body = response.bytes().await.map_err(Failure::network)?;
    if body.len() > MAX_BODY {
        return Err(Failure::new("network", "offline-permission answer too large"));
    }
    if status == 401 || status == 403 {
        return Err(Failure::new("signed_out", format!("Microsoft refused the offline-permission request (HTTP {status})")));
    }
    if !(200..300).contains(&status) {
        return Err(Failure::new("network", format!("offline-permission request failed (HTTP {status})")));
    }
    let summary = summarize(&body).ok_or_else(|| Failure::new("internal", "unrecognized offline-permission answer"))?;
    if designation_revokes(set, &summary) {
        match revoke(profile) {
            Ok(removed) if removed > 0 => crate::output::log(&format!("offline: {removed} stored license(s) removed")),
            Ok(_) => {}
            Err(_) => crate::output::log("offline: stored licenses could not be removed"),
        }
    }
    let mut index = read_index(profile);
    for key in ["enabled", "remaining", "max", "period_days", "next_change_at"] {
        index[key] = summary[key].clone();
    }
    index["checked_at"] = json!(now());
    write_index(profile, index);
    if set.is_some() && summary["request_success"] != json!(true) {
        return Err(Failure::new(
            "declined",
            "Microsoft did not apply the change (no changes left in this period?)",
        ));
    }
    Ok(summary)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("launchme-xbox-offline-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn holder() -> Holder {
        Holder { puid: "1234".into(), device: "device:abc".into() }
    }

    #[test]
    fn integrity_needs_the_secret_and_the_exact_payload() {
        let tag = seal(b"secret", b"policy").unwrap();
        assert!(verify(b"secret", b"policy", &tag));
        assert!(!verify(b"other", b"policy", &tag));
        assert!(!verify(b"secret", b"changed", &tag));
        assert!(!verify(b"", b"policy", &tag));
        assert!(seal(b"", b"policy").is_none());
    }

    #[test]
    fn a_stored_offline_lease_is_used_until_it_ends_and_only_by_its_holder() {
        let root = temp("cache");
        let cache = LicenseCache::new(&root, b"device secret");
        let now = 1_900_000_000;
        let key = licensing::tests::machine_key();
        let lease = licensing::tests::lease(true, now + 10 * 24 * 3600);
        let online_only = licensing::tests::lease(false, now + 10 * 24 * 3600);
        assert_eq!(cache.store("content-1", &holder(), &key, &[online_only], now).unwrap(), None);
        assert_eq!(cache.load("content-1", &holder(), now).unwrap().err(), Some(Unusable::Missing));
        let until = cache.store("content-1", &holder(), &key, &[lease], now).unwrap().unwrap();
        assert_eq!(until, now + 10 * 24 * 3600);
        let loaded = cache.load("CONTENT-1", &holder(), now + 3600).unwrap().ok().unwrap();
        assert_eq!(loaded.splicense_block, key.splicense_block);
        assert_eq!(loaded.until, until);
        // A clock set back before the last use is refused, the entry stays.
        assert_eq!(cache.load("content-1", &holder(), now - 3600).unwrap().err(), Some(Unusable::Clock));
        // Another secret (a new device registration) cannot read it.
        let other = LicenseCache::new(&root, b"another secret");
        assert_eq!(other.load("content-1", &holder(), now + 7200).unwrap().err(), Some(Unusable::Invalid));
        // After the lease: expired, and removed.
        let until = cache.store("content-1", &holder(), &key, &[licensing::tests::lease(true, now + 60)], now).unwrap().unwrap();
        assert_eq!(cache.load("content-1", &holder(), until + 1).unwrap().err(), Some(Unusable::Expired));
        assert_eq!(cache.load("content-1", &holder(), until + 2).unwrap().err(), Some(Unusable::Missing));
        // Another account: everything goes.
        cache.store("content-1", &holder(), &key, &[licensing::tests::lease(true, now + 600)], now).unwrap();
        let stranger = Holder { puid: "999".into(), device: "device:abc".into() };
        assert_eq!(cache.load("content-1", &stranger, now).unwrap().err(), Some(Unusable::Account));
        assert_eq!(cache.load("content-1", &holder(), now).unwrap().err(), Some(Unusable::Missing));
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn a_tampered_entry_is_refused_and_removed() {
        let root = temp("tamper");
        let cache = LicenseCache::new(&root, b"device secret");
        let now = 1_900_000_000;
        cache.store("content-2", &holder(), &licensing::tests::machine_key(), &[licensing::tests::lease(true, now + 3600)], now).unwrap();
        let path = root.join("content-2.json");
        let text = std::fs::read_to_string(&path).unwrap().replace(&format!("\"issuedAt\":{now}"), &format!("\"issuedAt\":{}", now + 999));
        std::fs::write(&path, text).unwrap();
        assert_eq!(cache.load("content-2", &holder(), now).unwrap().err(), Some(Unusable::Invalid));
        assert!(!path.exists());
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn revoking_removes_licenses_and_the_index_games() {
        let profile = temp("revoke");
        let cache = LicenseCache::new(&cache_root(&profile), b"device secret");
        let now = 1_900_000_000;
        cache.store("content-3", &holder(), &licensing::tests::machine_key(), &[licensing::tests::lease(true, now + 3600)], now).unwrap();
        index_game(&profile, "9NBLGGH4R315", Some(now + 3600));
        assert_eq!(read_index(&profile)["games"]["9NBLGGH4R315"], json!(now + 3600));
        assert_eq!(revoke(&profile).unwrap(), 1);
        assert!(read_index(&profile)["games"].as_object().unwrap().is_empty());
        assert!(!cache_root(&profile).join(REVOKED_FILE).exists());
        index_game(&profile, "../escape", Some(1));
        assert!(read_index(&profile)["games"].as_object().unwrap().is_empty());
        let _ = std::fs::remove_dir_all(profile);
    }

    #[test]
    fn the_designation_answer_keeps_only_what_launchme_shows() {
        let body = br#"{"offlinePermissions":{"enabled":true,"requestData":{"success":true,"maxTogglesPerPeriod":3,"remainingTogglesForPeriod":1,"periodLengthDays":365,"nextTimeToAdd":"1601-01-01T00:00:00+00:00"},"extra":"x"}}"#;
        let summary = summarize(body).unwrap();
        assert_eq!(summary["enabled"], true);
        assert_eq!(summary["remaining"], 1);
        assert_eq!(summary["max"], 3);
        assert_eq!(summary["period_days"], 365);
        assert!(summary["next_change_at"].is_null());
        assert!(summary.get("extra").is_none());
        let later = br#"{"offlinePermissions":{"enabled":false,"requestData":{"success":false,"remainingTogglesForPeriod":0,"nextTimeToAdd":"2027-03-01T10:00:00+00:00"}}}"#;
        let summary = summarize(later).unwrap();
        assert_eq!(summary["next_change_at"], 1_803_895_200);
        for bad in [&b"not json"[..], br#"{"offlinePermissions":{}}"#, br#"{"offlinePermissions":{"enabled":"yes"}}"#] {
            assert!(summarize(bad).is_none());
        }
        let status = |enabled: bool, success: bool| json!({ "enabled": enabled, "request_success": success });
        assert!(!designation_revokes(None, &status(true, false)));
        assert!(designation_revokes(None, &status(false, false)));
        assert!(designation_revokes(Some(true), &status(true, true)));
        assert!(designation_revokes(Some(false), &status(false, true)));
        assert!(!designation_revokes(Some(false), &status(true, false)));
    }
}
