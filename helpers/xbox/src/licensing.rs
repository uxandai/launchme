// Content license → the package's full AES key. The request is xodus-cli's (licensing/content.rs,
// GPL-3.0, the Xodus authors); the answer is read here with its leases so the account's designated
// offline PC can keep them (offline.rs). Lease rules follow the Nexus backend's license cache
// (https://github.com/domsvoboda/nexus-backend, GPL-3.0). A license is asked for every install and
// every launch, so an ended Game Pass entitlement stops working as in the Xbox app; only when
// Microsoft cannot be reached does a stored offline license stand in, while its lease lasts.

use std::collections::HashMap;
use std::path::Path;

use base64::prelude::*;
use futures_util::FutureExt;
use serde_json::Value;
use xodus::licensing::splicense::SPLicense;
use xodus::models::licensing::{DeviceContext, LicenseContentRequest, LicenseUserIdentity};
use xodus::tokens::TokenManager;
use xodus::xal::cvlib::CorrelationVector;

use crate::offline::{self, LicenseCache};
use crate::output::Failure;
use crate::tokens::store_tickets;

const CONTENT: &str = "https://licensing.mp.microsoft.com/v7.0/licenses/content";
const AGENT: &str = "XboxLm-PC/Microsoft.GamingServices_32.107.4002.0_x64__8wekyb3d8bbwe";
const CHALLENGE: &str = "PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0idXRmLTgiID8+PENsaWVudENoYWxsZW5nZSB4bWxuczp4c2k9Imh0dHA6Ly93d3cudzMub3JnLzIwMDEvWE1MU2NoZW1hLWluc3RhbmNlIiB4bWxuczp4c2Q9Imh0dHA6Ly93d3cudzMub3JnLzIwMDEvWE1MU2NoZW1hIiB4bWxucz0iaHR0cDovL3NjaGVtYXMubWljcm9zb2Z0LmNvbS9vbmVzdG9yZS9zZWN1cml0eS9ta21zL0xpY1JlcS92MSIgVmVyc2lvbj0iMiI+PExpY2Vuc2VQcm90b2NvbFZlcnNpb24+NTwvTGljZW5zZVByb3RvY29sVmVyc2lvbj48U2lnbmluZ0tleVZlcnNpb24+MTwvU2lnbmluZ0tleVZlcnNpb24+PENsaWVudFZlcnNpb24+MjwvQ2xpZW50VmVyc2lvbj48L0NsaWVudENoYWxsZW5nZT4=";
const MAX_BODY: usize = 8 * 1024 * 1024;
// The whole online attempt (tickets and license); past it Microsoft counts as unreachable.
const ONLINE_LIMIT: std::time::Duration = std::time::Duration::from_secs(60);
// Microsoft's licensing reason code for the Store's device limit (10 devices per account).
const DEVICE_LIMIT: i64 = 501;

pub const DEVICE_LIMIT_MESSAGE: &str = "Microsoft Store device limit reached (10 devices). Unlink a device you no longer use at https://account.microsoft.com/devices/content and try again.";

// A license document (the key license or a lease): the fields Launchme acts on.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Document {
    pub license_type: String, // LicenseInfo@Type: Full, Lease, Device, User...
    pub begin_date: Option<String>,
    pub expiration_date: Option<String>,
    pub custom_policies: Option<String>,
    pub binding_type: String, // Binding@Binding_Type: Machine or User.
    pub device_id: Option<String>,
    pub lease_required: Option<String>,
    pub splicense_block: String,
}

// A document as Microsoft sends it: base64 of its XML.
pub fn parse_document(value: &str) -> Option<Document> {
    if value.is_empty() || value.len() > MAX_BODY {
        return None;
    }
    let xml = BASE64_STANDARD.decode(value.trim()).ok()?;
    parse_xml(std::str::from_utf8(&xml).ok()?)
}

fn parse_xml(xml: &str) -> Option<Document> {
    fn child<'a, 'input>(node: roxmltree::Node<'a, 'input>, name: &str) -> Option<roxmltree::Node<'a, 'input>> {
        node.children().find(|n| n.is_element() && n.tag_name().name() == name)
    }
    fn text(node: Option<roxmltree::Node<'_, '_>>, name: &str) -> Option<String> {
        node.and_then(|n| child(n, name))
            .and_then(|n| n.text())
            .map(|t| t.trim().to_string())
            .filter(|t| !t.is_empty())
    }
    let doc = roxmltree::Document::parse(xml.trim_start_matches('\u{feff}')).ok()?;
    let root = doc.root_element();
    let info = child(root, "LicenseInfo");
    let binding = child(root, "Binding");
    Some(Document {
        license_type: info.and_then(|i| i.attribute("Type")).unwrap_or("").to_string(),
        begin_date: text(info, "BeginDate"),
        expiration_date: text(info, "ExpirationDate"),
        custom_policies: text(Some(root), "CustomPolicies"),
        binding_type: binding.and_then(|b| b.attribute("Binding_Type")).unwrap_or("").to_string(),
        device_id: text(binding, "DeviceId"),
        lease_required: text(binding, "LeaseRequired"),
        splicense_block: text(Some(root), "SPLicenseBlock").unwrap_or_default(),
    })
}

// The policy Microsoft attaches to a lease (CustomPolicies, base64 JSON); unknown fields ignored.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LeasePolicy {
    pub is_offline: bool,
    pub expires_at: Option<i64>,
}

pub fn lease_policy(lease: &Document) -> Option<LeasePolicy> {
    let encoded = lease.custom_policies.as_deref()?;
    if encoded.len() > 64 * 1024 {
        return None;
    }
    let value: Value = serde_json::from_slice(&BASE64_STANDARD.decode(encoded).ok()?).ok()?;
    Some(LeasePolicy {
        is_offline: value["isOffline"].as_bool() == Some(true),
        expires_at: value["expiresAt"].as_str().and_then(time),
    })
}

// Microsoft writes RFC 3339 in the policy and a UTC date-time without an offset in LicenseInfo.
pub fn time(text: &str) -> Option<i64> {
    let text = text.trim();
    if let Ok(time) = chrono::DateTime::parse_from_rfc3339(text) {
        return Some(time.timestamp());
    }
    ["%Y-%m-%dT%H:%M:%S%.f", "%Y-%m-%d %H:%M:%S%.f", "%m/%d/%Y %H:%M:%S", "%m/%d/%Y %I:%M:%S %p"]
        .iter()
        .find_map(|format| chrono::NaiveDateTime::parse_from_str(text, format).ok())
        .map(|time| time.and_utc().timestamp())
}

#[derive(Debug, PartialEq, Eq)]
pub enum LeaseCheck {
    Valid(u64), // Usable offline until this Unix time.
    Expired,
    Invalid(&'static str),
}

// Microsoft's restrictions on a lease as issued: a lease whose policy allows offline use, before its
// own expiry and expiration date, bound to the same device as a Machine-bound, lease-required key
// license, never past the offline window after `issued_at`.
pub fn check_lease(document: &str, key: &Document, issued_at: u64, now: u64) -> LeaseCheck {
    if document.is_empty() || document.len() > offline::MAX_LEASE_CHARS {
        return LeaseCheck::Invalid("size");
    }
    let Some(lease) = parse_document(document) else {
        return LeaseCheck::Invalid("unreadable");
    };
    if !lease.license_type.eq_ignore_ascii_case("Lease") {
        return LeaseCheck::Invalid("not-a-lease");
    }
    if !key.binding_type.eq_ignore_ascii_case("Machine") || !key.lease_required.as_deref().is_some_and(|v| v.eq_ignore_ascii_case("true")) {
        return LeaseCheck::Invalid("key-not-device-bound");
    }
    if let (Some(lease_device), Some(key_device)) = (&lease.device_id, &key.device_id)
        && lease_device != key_device
    {
        return LeaseCheck::Invalid("other-device");
    }
    let Some(policy) = lease_policy(&lease) else {
        return LeaseCheck::Invalid("no-policy");
    };
    if !policy.is_offline {
        return LeaseCheck::Invalid("not-offline");
    }
    let Some(expires_at) = policy.expires_at.filter(|s| *s > 0) else {
        return LeaseCheck::Invalid("no-expiry");
    };
    let mut until = expires_at as u64;
    for (begin, expiration) in [(&lease.begin_date, &lease.expiration_date), (&key.begin_date, &key.expiration_date)] {
        if let Some(begin) = begin.as_deref() {
            let Some(begin) = time(begin) else { return LeaseCheck::Invalid("begin-date") };
            if begin > now as i64 {
                return LeaseCheck::Invalid("not-yet-valid");
            }
        }
        if let Some(expiration) = expiration.as_deref() {
            let Some(expiration) = time(expiration) else { return LeaseCheck::Invalid("expiration-date") };
            until = until.min(expiration.max(0) as u64);
        }
    }
    until = until.min(issued_at.saturating_add(offline::MAX_OFFLINE_SECONDS));
    if until <= now {
        return LeaseCheck::Expired;
    }
    LeaseCheck::Valid(until)
}

// Why the online request gave no license.
#[derive(Debug)]
pub enum OnlineFailure {
    Unreachable(String), // No connection, a timeout or a passing service error: a stored license may stand in.
    Refused(Failure),    // This account has no right to the content: its stored license goes.
    DeviceRefused,       // The Store's device limit: every stored license goes.
    Other(Failure),      // Sign-in or answer problems offline play must not hide.
}

pub struct Issued {
    pub key: Document,
    pub leases: Vec<String>,
}

// Microsoft's answer to a content-license request, from its status and body.
pub fn read_answer(status: u16, body: &[u8]) -> Result<Issued, OnlineFailure> {
    match status {
        200..=299 => {}
        401 => return Err(OnlineFailure::Other(Failure::new("signed_out", "Microsoft licensing refused the sign-in (HTTP 401): sign in again"))),
        403 => return Err(OnlineFailure::Refused(Failure::new("not_entitled", "Microsoft denied the license (HTTP 403)"))),
        408 | 429 | 500..=599 => return Err(OnlineFailure::Unreachable(format!("Microsoft licensing answered HTTP {status}"))),
        _ => return Err(OnlineFailure::Other(Failure::new("network", format!("Microsoft licensing answered HTTP {status}")))),
    }
    if body.len() > MAX_BODY {
        return Err(OnlineFailure::Other(Failure::new("internal", "content-license answer too large")));
    }
    let schema = || OnlineFailure::Other(Failure::new("internal", "unrecognized content-license answer"));
    let value: Value = serde_json::from_slice(body).map_err(|_| schema())?;
    let license = value.get("license").filter(|v| !v.is_null());
    let failure = value.get("satisfactionFailure").filter(|v| !v.is_null());
    match (license, failure) {
        (Some(_), Some(_)) | (None, None) => Err(schema()),
        (None, Some(failure)) => {
            if failure["code"].as_i64() == Some(DEVICE_LIMIT) {
                Err(OnlineFailure::DeviceRefused)
            } else {
                Err(OnlineFailure::Refused(Failure::new(
                    "not_entitled",
                    "Microsoft did not grant a license for this account: is the game owned, or is the Game Pass subscription active?",
                )))
            }
        }
        (Some(license), None) => {
            let key = license["keys"][0]["value"].as_str().and_then(parse_document).ok_or_else(schema)?;
            if key.splicense_block.is_empty() {
                return Err(schema());
            }
            let leases = license["leases"]
                .as_array()
                .into_iter()
                .flatten()
                .filter_map(|lease| lease["value"].as_str())
                .filter(|lease| lease.len() <= offline::MAX_LEASE_CHARS)
                .take(16)
                .map(str::to_owned)
                .collect();
            Ok(Issued { key, leases })
        }
    }
}

async fn request(client: &reqwest::Client, tokens: &TokenManager, content_id: &str, market: &str) -> Result<Issued, OnlineFailure> {
    let tickets = store_tickets(client, tokens).await.map_err(|failure| match failure.code {
        "network" => OnlineFailure::Unreachable(failure.message),
        _ => OnlineFailure::Other(failure),
    })?;
    let body = LicenseContentRequest {
        content_id: content_id.to_string(),
        market: market.to_string(),
        client_challenge: CHALLENGE.into(),
        concurrency_mode: "Rude".into(),
        license_version: 4,
        need_key: true,
        key_only: true,
        device_context: DeviceContext::default(),
        users: HashMap::from([(
            xodus::licensing::utils::generate_suid(),
            vec![LicenseUserIdentity {
                identity_type: "Msa".to_string(),
                identity_value: tickets.user,
                local_ticket_reference: tickets.puid,
            }],
        )]),
    };
    let mut response = client
        .post(CONTENT)
        .header("from", "XboxLicenseManager")
        .header("Authorization", tickets.device)
        .header("user-agent", AGENT)
        .header("MS-CV", CorrelationVector::new().to_string())
        .json(&body)
        .timeout(std::time::Duration::from_secs(30))
        .send()
        .await
        .map_err(|e| OnlineFailure::Unreachable(e.to_string()))?;
    let status = response.status().as_u16();
    let mut bytes = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(|e| OnlineFailure::Unreachable(e.to_string()))? {
        if bytes.len() + chunk.len() > MAX_BODY {
            return Err(OnlineFailure::Other(Failure::new("internal", "content-license answer too large")));
        }
        bytes.extend_from_slice(&chunk);
    }
    read_answer(status, &bytes)
}

// The content key a license block opens with this device's key.
pub fn unpack(block: &str, tokens: &TokenManager) -> Result<[u8; 32], Failure> {
    let game = SPLicense::parse_base64(block).map_err(|_| Failure::new("internal", "unreadable game license"))?;
    let device = tokens.get_device_license().map_err(|e| Failure::new("storage", e.to_string()))?;
    let device = SPLicense::parse_base64(&device.splicense).map_err(|_| Failure::new("internal", "unreadable device license"))?;
    let key = device
        .encrypted_device_key
        .ok_or_else(|| Failure::new("internal", "device license without a key"))?
        .derive_device_key();
    if game.content_keys.len() != 1 {
        return Err(Failure::new("unsupported_package", format!("{} content keys", game.content_keys.len())));
    }
    let (_, content_key) = game.content_keys.into_iter().next().expect("one key");
    let full = content_key.unpack(&key).map_err(|_| Failure::new("internal", "content key did not unpack"))?;
    Ok(*full)
}

pub struct Key {
    pub key: [u8; 32],
    pub offline_until: Option<u64>, // A stored offline lease covers the game until then.
    pub from_cache: bool,           // Microsoft was unreachable: the stored license opened it.
}

// The key for `content_id`. `product` (the install marker's) names the game in offline.json.
pub async fn content_key(
    client: &reqwest::Client,
    tokens: &TokenManager,
    profile: &Path,
    content_id: &str,
    market: &str,
    product: Option<&str>,
) -> Result<Key, Failure> {
    let holder = offline::holder(tokens);
    // Xodus still panics on some unexpected answers (and without a connection while renewing the
    // device's sign-in): such an attempt counts as Microsoft being unreachable.
    let attempt = std::panic::AssertUnwindSafe(request(client, tokens, content_id, market)).catch_unwind();
    let online = match tokio::time::timeout(ONLINE_LIMIT, attempt).await {
        Ok(Ok(result)) => result,
        Ok(Err(_)) => Err(OnlineFailure::Unreachable(crate::output::last_panic())),
        Err(_) => Err(OnlineFailure::Unreachable("Microsoft licensing timed out".into())),
    };
    let root = offline::cache_root(profile);
    let index = |until: Option<u64>| {
        if let Some(product) = product {
            offline::index_game(profile, product, until);
        }
    };
    match online {
        Ok(issued) => {
            let key = unpack(&issued.key.splicense_block, tokens)?;
            let mut offline_until = None;
            if !issued.key.binding_type.eq_ignore_ascii_case("Machine") {
                // A user-bound license: this PC is not (or no longer) the account's offline PC.
                if offline::revoke(profile).is_ok_and(|removed| removed > 0) {
                    crate::output::log("offline: this PC is no longer the offline PC; stored licenses removed");
                }
            } else if let Some((holder, secret)) = &holder {
                match LicenseCache::new(&root, secret).store(content_id, holder, &issued.key, &issued.leases, offline::now()) {
                    Ok(until) => offline_until = until,
                    Err(_) => crate::output::log("offline: license not stored"),
                }
                index(offline_until);
            }
            Ok(Key { key, offline_until, from_cache: false })
        }
        Err(OnlineFailure::Refused(failure)) => {
            if let Some((_, secret)) = &holder {
                LicenseCache::new(&root, secret).purge(content_id);
            }
            index(None);
            Err(failure)
        }
        Err(OnlineFailure::DeviceRefused) => {
            let _ = offline::revoke(profile);
            Err(Failure::new("device_limit", DEVICE_LIMIT_MESSAGE))
        }
        Err(OnlineFailure::Other(failure)) => Err(failure),
        Err(OnlineFailure::Unreachable(reason)) => {
            crate::output::log(&format!("licensing unreachable: {reason}"));
            let Some((holder, secret)) = &holder else {
                return Err(Failure::network(format!("{reason}; no offline license can be used without a stored sign-in")));
            };
            match LicenseCache::new(&root, secret).load(content_id, holder, offline::now())? {
                Ok(cached) => Ok(Key { key: unpack(&cached.splicense_block, tokens)?, offline_until: Some(cached.until), from_cache: true }),
                Err(unusable) => {
                    if unusable != offline::Unusable::Clock {
                        index(None);
                    }
                    Err(Failure::network(format!("Microsoft cannot be reached and {}", unusable.message())))
                }
            }
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    fn encode(xml: &str) -> String {
        BASE64_STANDARD.encode(xml)
    }

    fn iso(seconds: u64) -> String {
        chrono::DateTime::from_timestamp(seconds as i64, 0).unwrap().to_rfc3339()
    }

    pub fn machine_key() -> Document {
        parse_document(&encode(
            r#"<?xml version="1.0"?><License xmlns="urn:x"><SPLicenseBlock>QUJD</SPLicenseBlock><LicenseInfo Type="Full"><BeginDate>2020-01-01T00:00:00.0000000</BeginDate></LicenseInfo><Binding Binding_Type="Machine"><DeviceId>dev-1</DeviceId><LeaseRequired>true</LeaseRequired></Binding></License>"#,
        ))
        .unwrap()
    }

    pub fn lease(offline: bool, expires: u64) -> String {
        let policy = BASE64_STANDARD.encode(format!(r#"{{"isOffline":{offline},"expiresAt":"{}","pollAt":"{}"}}"#, iso(expires), iso(expires)));
        encode(&format!(
            r#"<License><LicenseInfo Type="Lease"/><CustomPolicies>{policy}</CustomPolicies><Binding Binding_Type="Machine"><DeviceId>dev-1</DeviceId></Binding></License>"#
        ))
    }

    #[test]
    fn documents_read_their_type_binding_and_dates() {
        let key = machine_key();
        assert_eq!(key.license_type, "Full");
        assert_eq!(key.binding_type, "Machine");
        assert_eq!(key.device_id.as_deref(), Some("dev-1"));
        assert_eq!(key.lease_required.as_deref(), Some("true"));
        assert_eq!(key.splicense_block, "QUJD");
        assert_eq!(time(key.begin_date.as_deref().unwrap()), Some(1_577_836_800));
        assert!(parse_document("not base64!").is_none());
        assert!(parse_document(&encode("<not xml")).is_none());
    }

    #[test]
    fn only_an_offline_lease_for_this_device_counts_and_never_past_the_window() {
        let now = 1_900_000_000;
        let key = machine_key();
        assert_eq!(check_lease(&lease(true, now + 3600), &key, now, now), LeaseCheck::Valid(now + 3600));
        assert_eq!(check_lease(&lease(false, now + 3600), &key, now, now), LeaseCheck::Invalid("not-offline"));
        assert_eq!(check_lease(&lease(true, now - 1), &key, now, now), LeaseCheck::Expired);
        // A lease longer than the window ends with it.
        assert_eq!(
            check_lease(&lease(true, now + 90 * 24 * 3600), &key, now, now),
            LeaseCheck::Valid(now + offline::MAX_OFFLINE_SECONDS)
        );
        let mut user = key.clone();
        user.binding_type = "User".into();
        assert_eq!(check_lease(&lease(true, now + 3600), &user, now, now), LeaseCheck::Invalid("key-not-device-bound"));
        let mut other = key.clone();
        other.device_id = Some("dev-2".into());
        assert_eq!(check_lease(&lease(true, now + 3600), &other, now, now), LeaseCheck::Invalid("other-device"));
        let mut trial = key.clone();
        trial.expiration_date = Some(iso(now + 60));
        assert_eq!(check_lease(&lease(true, now + 3600), &trial, now, now), LeaseCheck::Valid(now + 60));
        assert_eq!(check_lease(&machine_key().splicense_block, &key, now, now), LeaseCheck::Invalid("unreadable"));
    }

    #[test]
    fn answers_become_licenses_refusals_or_unreachable() {
        let key = encode(r#"<License><SPLicenseBlock>QUJD</SPLicenseBlock><LicenseInfo Type="Full"/><Binding Binding_Type="User"/></License>"#);
        let body = format!(r#"{{"license":{{"keys":[{{"value":"{key}"}}],"leases":[{{"value":"{}"}}]}}}}"#, lease(true, 1));
        let issued = read_answer(200, body.as_bytes()).ok().unwrap();
        assert_eq!(issued.key.binding_type, "User");
        assert_eq!(issued.leases.len(), 1);
        assert!(matches!(read_answer(200, br#"{"satisfactionFailure":{"code":501,"description":"x"}}"#), Err(OnlineFailure::DeviceRefused)));
        assert!(matches!(read_answer(200, br#"{"satisfactionFailure":{"code":2,"description":"x"}}"#), Err(OnlineFailure::Refused(f)) if f.code == "not_entitled"));
        assert!(matches!(read_answer(503, b""), Err(OnlineFailure::Unreachable(_))));
        assert!(matches!(read_answer(429, b""), Err(OnlineFailure::Unreachable(_))));
        assert!(matches!(read_answer(401, b""), Err(OnlineFailure::Other(f)) if f.code == "signed_out"));
        assert!(matches!(read_answer(403, b""), Err(OnlineFailure::Refused(_))));
        // Never a license from an answer that also refuses.
        let both = format!(r#"{{"license":{{"keys":[{{"value":"{key}"}}]}},"satisfactionFailure":{{"code":1}}}}"#);
        assert!(matches!(read_answer(200, both.as_bytes()), Err(OnlineFailure::Other(_))));
        assert!(matches!(read_answer(200, b"{}"), Err(OnlineFailure::Other(_))));
    }
}
