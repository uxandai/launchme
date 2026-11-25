// Read-only Xbox Live: the account's played titles (last played, achievement counts, minutes played)
// and one title's achievements. Endpoints and checks follow the Nexus backend's `game-activity` and
// `achievements` commands (https://github.com/domsvoboda/nexus-backend, GPL-3.0); nothing here
// writes presence, statistics or achievement progress.

use std::collections::{HashMap, HashSet};
use std::path::Path;

use serde_json::{Value, json};
use xodus::models::live::ExchangeUserTokenOutcome;
use xodus::models::secrets::Token;
use xodus::models::soap;
use xodus::tokens::TokenManager;

use crate::output::{Failure, Outcome};

const PROFILE: &str = "https://profile.xboxlive.com/users/me/profile/settings?settings=Gamertag";
const MAX_TITLES: usize = 3000;
const STATS_BATCH: usize = 50;

pub struct Session {
    authorization: String,
    pub xuid: String,
}

// RPS ticket → Xbox user token → XSTS for http://xboxlive.com (a bearer token; never written down).
pub async fn session(client: &reqwest::Client, tokens: &TokenManager) -> Result<Session, Failure> {
    let signed = crate::tokens::signed_in(client, tokens).await?;
    let outcome = xodus::api::live::exchange_user_token(
        client,
        signed.user,
        signed.username,
        signed.device,
        None,
        Some("Silent".to_string()),
        crate::tokens::HOSTING_APP.to_string(),
        &[("user.auth.xboxlive.com".to_string(), Some(soap::PolicyReference::mbi_ssl()))],
    )
    .await
    .map_err(Failure::network)?;
    let token: Token = match outcome {
        ExchangeUserTokenOutcome::Issued(soap::BodyContent::RequestSecurityTokenResponseCollection(mut c)) if !c.security_tokens.is_empty() => {
            c.security_tokens.remove(0).into()
        }
        ExchangeUserTokenOutcome::Issued(soap::BodyContent::RequestSecurityTokenResponse(t)) => (*t).into(),
        _ => return Err(Failure::new("signed_out", "the session expired, sign in again")),
    };
    let Token::Compact(ticket) = token else {
        return Err(Failure::new("internal", "expected a compact ticket"));
    };
    let user = xodus::api::xbox::authenticate_xbox_user(client, ticket).await.map_err(Failure::network)?;
    let xsts = xodus::api::xbox::request_xsts_token(client, user.token, "http://xboxlive.com").await.map_err(Failure::network)?;
    let hash = xsts.user_hash().filter(|h| !h.is_empty()).ok_or_else(|| Failure::new("internal", "XSTS without a user hash"))?;
    let authorization = format!("XBL3.0 x={hash};{}", xsts.token);
    let profile = get(client, &authorization, PROFILE, 128 * 1024).await?;
    let xuid = xuid(&profile).ok_or_else(|| Failure::new("internal", "profile without an Xbox user id"))?;
    Ok(Session { authorization, xuid })
}

fn decimal(value: &str) -> bool {
    !value.is_empty() && value.len() <= 20 && value.bytes().all(|b| b.is_ascii_digit()) && value.parse::<u64>().is_ok_and(|n| n > 0)
}

pub fn xuid(profile: &Value) -> Option<String> {
    let users = profile["profileUsers"].as_array().filter(|u| u.len() == 1)?;
    users[0]["id"].as_str().filter(|id| decimal(id)).map(str::to_owned)
}

async fn read(response: reqwest::Response, limit: usize) -> Result<Value, Failure> {
    let status = response.status();
    if status.as_u16() == 401 || status.as_u16() == 403 {
        return Err(Failure::new("signed_out", format!("Xbox Live refused the request (HTTP {})", status.as_u16())));
    }
    if !status.is_success() {
        return Err(Failure::new("network", format!("Xbox Live answered HTTP {}", status.as_u16())));
    }
    if response.content_length().is_some_and(|size| size > limit as u64) {
        return Err(Failure::new("internal", "Xbox Live answer too large"));
    }
    let mut response = response;
    let mut bytes = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(Failure::network)? {
        if bytes.len() + chunk.len() > limit {
            return Err(Failure::new("internal", "Xbox Live answer too large"));
        }
        bytes.extend_from_slice(&chunk);
    }
    serde_json::from_slice(&bytes).map_err(|_| Failure::new("internal", "Xbox Live answer is not JSON"))
}

async fn get(client: &reqwest::Client, authorization: &str, url: &str, limit: usize) -> Result<Value, Failure> {
    let response = client
        .get(url)
        .header("Authorization", authorization)
        .header("x-xbl-contract-version", "2")
        .header("Accept-Language", "en-US")
        .timeout(std::time::Duration::from_secs(30))
        .send()
        .await
        .map_err(Failure::network)?;
    read(response, limit).await
}

fn text(value: &Value, limit: usize) -> Option<String> {
    let s = value.as_str()?;
    if s.chars().any(|c| c.is_control() && c != '\n' && c != '\r' && c != '\t') {
        return None;
    }
    Some(s.chars().take(limit).collect())
}

fn date(value: &Value) -> Option<i64> {
    let s = value.as_str().filter(|s| (10..=40).contains(&s.len()) && s.is_ascii())?;
    crate::licensing::time(s).filter(|t| *t > 0)
}

// One title of the account's history: titlehub's titlehistory with the achievement decoration.
pub fn history_title(title: &Value) -> Option<(String, Value)> {
    let id = title["titleId"].as_str().filter(|id| decimal(id))?.to_string();
    if title["titleHistory"]["visible"] == json!(false) {
        return None;
    }
    let achievement = &title["achievement"];
    let count = |key: &str| achievement[key].as_u64().filter(|n| *n <= 100_000);
    let achievements = match (count("currentAchievements"), count("totalAchievements")) {
        (Some(unlocked), Some(total)) if total > 0 && unlocked <= total => json!({
            "unlocked": unlocked,
            "total": total,
            "gamerscore": count("currentGamerscore"),
            "total_gamerscore": count("totalGamerscore"),
        }),
        _ => Value::Null,
    };
    Some((
        id.clone(),
        json!({
            "title_id": id,
            "name": text(&title["name"], 256),
            "pfn": text(&title["pfn"], 128),
            "last_played": date(&title["titleHistory"]["lastTimePlayed"]),
            "achievements": achievements,
        }),
    ))
}

// userstats batch: MinutesPlayed per title id for this user (missing ≠ zero).
pub fn minutes(value: &Value, xuid: &str) -> HashMap<String, u64> {
    let mut result = HashMap::new();
    let mut lists: Vec<&Value> = value["statlistscollection"].as_array().into_iter().flatten().collect();
    for group in value["groups"].as_array().into_iter().flatten() {
        lists.extend(group["statlistscollection"].as_array().into_iter().flatten());
    }
    for list in lists {
        if list["arrangebyfieldid"].as_str().is_some_and(|id| id != xuid) {
            continue;
        }
        for stat in list["stats"].as_array().into_iter().flatten() {
            if stat["name"].as_str() != Some("MinutesPlayed") || stat["xuid"].as_str().is_some_and(|id| id != xuid) {
                continue;
            }
            let title = stat["titleid"].as_str().or_else(|| stat["titleId"].as_str()).filter(|t| decimal(t));
            let raw = stat["value"].as_str().filter(|v| !v.is_empty() && v.len() <= 32 && v.bytes().all(|c| c.is_ascii_digit() || c == b'.'));
            if let (Some(title), Some(raw)) = (title, raw)
                && let Ok(n) = raw.parse::<f64>()
                && n.is_finite()
                && (0.0..=1_000_000_000.0).contains(&n)
            {
                result.insert(title.to_string(), n as u64);
            }
        }
    }
    result
}

// `activity --out <file>`: every title of the history with its last play, achievement counts and
// minutes played. Written only after a complete read.
pub async fn activity(client: &reqwest::Client, tokens: &TokenManager, out: &Path) -> Outcome {
    let session = session(client, tokens).await?;
    crate::output::progress("history", 0, 0);
    let history = get(
        client,
        &session.authorization,
        &format!("https://titlehub.xboxlive.com/users/xuid({})/titles/titlehistory/decoration/achievement", session.xuid),
        8 * 1024 * 1024,
    )
    .await?;
    if history["xuid"].as_str().is_some_and(|id| id != session.xuid) {
        return Err(Failure::new("internal", "title history of another user"));
    }
    let mut titles: Vec<(String, Value)> = history["titles"].as_array().into_iter().flatten().filter_map(history_title).collect();
    titles.truncate(MAX_TITLES);
    let ids: Vec<String> = titles.iter().map(|(id, _)| id.clone()).collect();
    let mut played = HashMap::new();
    for (index, chunk) in ids.chunks(STATS_BATCH).enumerate() {
        crate::output::progress("stats", (index * STATS_BATCH) as u64, ids.len() as u64);
        let body = json!({
            "arrangebyfield": "xuid",
            "xuids": [session.xuid],
            "stats": chunk.iter().map(|id| json!({ "name": "MinutesPlayed", "titleId": id })).collect::<Vec<_>>(),
        });
        let response = client
            .post("https://userstats.xboxlive.com/batch")
            .header("Authorization", &session.authorization)
            .header("x-xbl-contract-version", "2")
            .json(&body)
            .timeout(std::time::Duration::from_secs(30))
            .send()
            .await;
        // One unavailable batch leaves those titles without minutes, never with zero.
        match response {
            Ok(response) => match read(response, 1024 * 1024).await {
                Ok(value) => played.extend(minutes(&value, &session.xuid)),
                Err(failure) => crate::output::log(&format!("stats: {}", failure.message)),
            },
            Err(e) => crate::output::log(&format!("stats: {e}")),
        }
    }
    let titles: Vec<Value> = titles
        .into_iter()
        .map(|(id, mut title)| {
            title["minutes"] = played.get(&id).map(|m| json!(m)).unwrap_or(Value::Null);
            title
        })
        .collect();
    let count = titles.len();
    crate::library::write_atomic(out, &json!({ "version": 1, "synced_at": crate::offline::now(), "titles": titles }))?;
    Ok(json!({ "titles": count }))
}

fn scid(value: &str) -> bool {
    value.len() == 36
        && value.bytes().enumerate().all(|(i, b)| if [8, 13, 18, 23].contains(&i) { b == b'-' } else { b.is_ascii_hexdigit() })
}

fn title_matches(value: &Value, title: u32) -> bool {
    value.as_u64() == Some(u64::from(title)) || value.as_str() == Some(title.to_string().as_str())
}

fn safe_icon(value: &Value) -> Option<String> {
    let raw = value.as_str().filter(|r| r.len() <= 2048)?;
    let url = url::Url::parse(raw).ok()?;
    if url.scheme() != "https" || !url.username().is_empty() || url.password().is_some() || url.port().is_some() {
        return None;
    }
    if !["images-eds-ssl.xboxlive.com", "images-eds.xboxlive.com", "store-images.s-microsoft.com"].contains(&url.host_str()?) {
        return None;
    }
    Some(url.to_string())
}

// One achievement of the Achievements V2 list, or why it was left out.
pub fn achievement(value: &Value, title: u32) -> Result<Value, &'static str> {
    let id = text(&value["id"], 128).filter(|s| !s.is_empty()).ok_or("id")?;
    let service = value["serviceConfigId"].as_str().filter(|s| scid(s)).ok_or("service")?;
    if !value["titleAssociations"].as_array().ok_or("titles")?.iter().any(|v| title_matches(&v["id"], title)) {
        return Err("title");
    }
    if value["achievementType"].as_str() != Some("Persistent") {
        return Err("type");
    }
    let revoked = value["isRevoked"].as_bool().ok_or("revoked")?;
    let secret = value["isSecret"].as_bool().ok_or("secret")?;
    let unlocked = match value["progressState"].as_str() {
        Some("Achieved") => !revoked,
        Some("NotStarted") | Some("InProgress") => false,
        _ => return Err("state"),
    };
    let name = text(&value["name"], 256).filter(|s| !s.is_empty()).ok_or("name")?;
    let description = text(&value[if unlocked { "description" } else { "lockedDescription" }], 1500)
        .or_else(|| text(&value["description"], 1500))
        .unwrap_or_default();
    let score = value["rewards"]
        .as_array()
        .into_iter()
        .flatten()
        .filter(|r| r["type"].as_str() == Some("Gamerscore"))
        .find_map(|r| r["value"].as_str().and_then(|s| s.parse::<u32>().ok()).filter(|n| *n <= 10_000));
    let unlocked_at = if unlocked { date(&value["progression"]["timeUnlocked"]) } else { None };
    let icon = value["mediaAssets"]
        .as_array()
        .into_iter()
        .flatten()
        .take(20)
        .filter(|a| a["type"].as_str() == Some("Icon"))
        .find_map(|a| safe_icon(&a["url"]));
    let rarity = value["rarity"]["currentPercentage"].as_f64().filter(|p| p.is_finite() && (0.0..=100.0).contains(p));
    Ok(json!({
        "id": format!("{}:{id}", service.to_ascii_lowercase()),
        "name": name,
        "description": description,
        "unlocked": unlocked,
        "secret": secret,
        "gamerscore": score,
        "unlocked_at": unlocked_at,
        "icon": icon,
        "rarity": rarity,
    }))
}

// `achievements --title <id>`: the title's achievements for this account (at most 500).
pub async fn achievements(client: &reqwest::Client, tokens: &TokenManager, title: &str) -> Outcome {
    let title: u32 = title.parse().ok().filter(|t| *t > 0).ok_or_else(|| Failure::new("usage", "--title must be an Xbox title id"))?;
    let session = session(client, tokens).await?;
    let mut items = Vec::new();
    let mut ids = HashSet::new();
    let mut seen_tokens = HashSet::new();
    let mut continuation: Option<String> = None;
    let mut partial = false;
    for page in 0..10 {
        let mut url = url::Url::parse(&format!("https://achievements.xboxlive.com/users/xuid({})/achievements", session.xuid))
            .map_err(|_| Failure::new("internal", "achievements URL"))?;
        url.query_pairs_mut()
            .append_pair("titleId", &title.to_string())
            .append_pair("types", "Persistent")
            .append_pair("maxItems", "100");
        if let Some(token) = &continuation {
            url.query_pairs_mut().append_pair("continuationToken", token);
        }
        let value = get(client, &session.authorization, url.as_str(), 2 * 1024 * 1024).await?;
        let rows = value["achievements"].as_array().filter(|r| r.len() <= 500).ok_or_else(|| Failure::new("internal", "achievements answer"))?;
        for row in rows {
            match achievement(row, title) {
                Ok(item) => {
                    if ids.insert(item["id"].as_str().unwrap_or_default().to_string()) && items.len() < 500 {
                        items.push(item);
                    }
                }
                Err(_) => partial = true,
            }
        }
        let next = value["pagingInfo"]["continuationToken"]
            .as_str()
            .filter(|t| !t.is_empty() && t.len() <= 4096 && !t.chars().any(char::is_control))
            .map(str::to_owned);
        match next {
            Some(next) if items.len() < 500 && page < 9 && seen_tokens.insert(next.clone()) => continuation = Some(next),
            Some(_) => {
                partial = true;
                break;
            }
            None => break,
        }
    }
    let unlocked = items.iter().filter(|i| i["unlocked"] == json!(true)).count();
    Ok(json!({ "title_id": title.to_string(), "unlocked": unlocked, "total": items.len(), "partial": partial, "items": items }))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn row() -> Value {
        json!({"id":"1","serviceConfigId":"11111111-1111-4111-8111-111111111111","titleAssociations":[{"id":42}],
            "achievementType":"Persistent","name":"First step","description":"Unlocked","lockedDescription":"Do something",
            "progressState":"Achieved","isSecret":false,"isRevoked":false,"rewards":[{"type":"Gamerscore","value":"10"}],
            "progression":{"timeUnlocked":"2026-01-01T12:00:00Z"},"rarity":{"currentPercentage":12.5},
            "mediaAssets":[{"type":"Icon","url":"https://images-eds-ssl.xboxlive.com/image"}]})
    }

    #[test]
    fn achievements_keep_state_dates_and_safe_icons_only() {
        let v = achievement(&row(), 42).unwrap();
        assert_eq!(v["unlocked"], true);
        assert_eq!(v["gamerscore"], 10);
        assert_eq!(v["unlocked_at"], 1_767_268_800);
        assert_eq!(v["rarity"], 12.5);
        assert_eq!(v["icon"], "https://images-eds-ssl.xboxlive.com/image");
        let mut r = row();
        r["isRevoked"] = json!(true);
        let v = achievement(&r, 42).unwrap();
        assert_eq!(v["unlocked"], false);
        assert!(v["unlocked_at"].is_null());
        assert_eq!(v["description"], "Do something");
        assert!(achievement(&row(), 43).is_err());
        for url in ["file:///etc/passwd", "https://evil.test/icon", "https://user:x@images-eds-ssl.xboxlive.com/a", "http://images-eds-ssl.xboxlive.com/a"] {
            assert!(safe_icon(&json!(url)).is_none());
        }
        r["progressState"] = json!("Unknown");
        assert!(achievement(&r, 42).is_err());
    }

    #[test]
    fn history_titles_carry_counts_dates_and_skip_hidden_ones() {
        let title = json!({"titleId":"42","name":"Game","pfn":"Studio.Game_8wekyb3d8bbwe",
            "titleHistory":{"lastTimePlayed":"2026-02-01T10:00:00.1234567Z","visible":true},
            "achievement":{"currentAchievements":3,"totalAchievements":10,"currentGamerscore":40,"totalGamerscore":1000}});
        let (id, v) = history_title(&title).unwrap();
        assert_eq!(id, "42");
        assert_eq!(v["last_played"], 1_769_940_000);
        assert_eq!(v["achievements"]["unlocked"], 3);
        assert_eq!(v["achievements"]["total"], 10);
        let mut hidden = title.clone();
        hidden["titleHistory"]["visible"] = json!(false);
        assert!(history_title(&hidden).is_none());
        let mut none = title.clone();
        none["achievement"] = json!({"currentAchievements":0,"totalAchievements":0});
        assert!(history_title(&none).unwrap().1["achievements"].is_null());
        assert!(history_title(&json!({"titleId":"abc"})).is_none());
    }

    #[test]
    fn minutes_come_per_title_and_only_for_this_user() {
        let stat = |xuid: &str, title: &str, value: &str| json!({"xuid":xuid,"titleid":title,"name":"MinutesPlayed","value":value});
        let value = json!({"statlistscollection":[{"arrangebyfield":"xuid","arrangebyfieldid":"123",
            "stats":[stat("123","42","2052"), stat("123","43","0"), stat("999","44","5"), stat("123","45","-1"), stat("123","46","1e5")]}]});
        let played = minutes(&value, "123");
        assert_eq!(played.get("42"), Some(&2052));
        assert_eq!(played.get("43"), Some(&0));
        assert!(!played.contains_key("44") && !played.contains_key("45") && !played.contains_key("46"));
        assert_eq!(xuid(&json!({"profileUsers":[{"id":"2535"}]})).as_deref(), Some("2535"));
        assert!(xuid(&json!({"profileUsers":[{"id":"x"}]})).is_none());
    }
}
