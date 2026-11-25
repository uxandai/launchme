// The account's games: Store collections (bought, Play Anywhere, claimed), the active Game Pass
// subscription and its PC catalog, then displaycatalog details. Endpoints as documented by Xodus
// (docs/xbox/gamepass.md, PR #199) and its owned-games work (PR #221). A bought bundle (a Premium
// edition, a collection) has no PC package of its own: the games its SKUs bundle are the account's
// too. With Game Pass, the public "recently added" PC collection names the newest catalog games in
// its own order (as the Nexus launcher reads it, https://github.com/domsvoboda/nexus-launcher).
// The account's add-ons (collections' Durable items) are listed under their game: displaycatalog
// names the game in MarketProperties[].RelatedProducts[] with RelationshipType "addOnParent"; an
// add-on with an x64 MSIXVC package for Windows.Desktop is content to download, one without is a
// licence the game checks itself (listing only: Launchme does not install add-ons yet).

use std::collections::{BTreeMap, HashMap};
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Deserialize;
use serde_json::{Value, json};
use xodus::tokens::TokenManager;
use xodus::xal::cvlib::CorrelationVector;

use crate::output::{Failure, Outcome};
use crate::tokens::{StoreTickets, store_tickets};

const COLLECTIONS_QUERY: &str = "https://collections.mp.microsoft.com/v7.0/collections/query";
const SUBSCRIPTIONS: &str = "https://beige.xboxservices.com/PCGAFD/entitlements/subscriptions";
const SIGLS: &str = "https://catalog.gamepass.com/sigls/v3/";
const SIGLS_V2: &str = "https://catalog.gamepass.com/sigls/v2";
const DISPLAY_CATALOG: &str = "https://displaycatalog.mp.microsoft.com/v7.0/products";
const ALL_PC_GAMES: &str = "fdd9e2a7-0fee-49f6-ad69-4354098401ff";
const ALL_PC_GAMES_EA: &str = "609d944c-d395-4c0a-9ea4-e9f39b52c1ad";
const RECENTLY_ADDED_PC: &str = "06323672-b8c8-43cc-b0de-32d5a9834749";
const RECENT_LIMIT: usize = 40;
const MAX_BUNDLE_CHILDREN: usize = 64;
const MAX_ADDONS: usize = 1000;
const MAX_GAME_ADDONS: usize = 200;
const PC_GAME_PASS: &str = "CFQ7TTC0KGQ8";
const ULTIMATE: &str = "CFQ7TTC0KHS0";
// Every Game Pass subscription id the Xbox app asks about.
const SUBSCRIPTION_IDS: [&str; 7] = [
    "CFQ7TTC0K6L8",
    "CFQ7TTC0K5DJ",
    "CFQ7TTC0KGQ8",
    "CFQ7TTC0P85B",
    "CFQ7TTC0KHS0",
    "CFQ7TTC10QFD",
    "CFQ7TTC0HXBN",
];
const BATCH: usize = 20;
const MAX_PAGES: usize = 100;

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum Acquisition {
    GamePass = 1,
    Other = 2,
    Purchased = 3,
}

impl Acquisition {
    fn name(self) -> &'static str {
        match self {
            Acquisition::Purchased => "purchased",
            Acquisition::GamePass => "gamepass",
            Acquisition::Other => "other",
        }
    }
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct CollectionsPage {
    #[serde(default)]
    items: Vec<CollectionItem>,
    continuation_token: Option<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct CollectionItem {
    product_id: String,
    #[serde(default)]
    product_kind: String,
    #[serde(default)]
    status: String,
    order_id: Option<String>,
    recurrence_id: Option<String>,
}

// The account's games (with how they came) and its add-ons.
async fn owned(
    client: &reqwest::Client,
    tickets: &StoreTickets,
    market: &str,
) -> Result<(BTreeMap<String, Acquisition>, std::collections::BTreeSet<String>), Failure> {
    let mut result = BTreeMap::new();
    let mut addons = std::collections::BTreeSet::new();
    let mut continuation: Option<String> = None;
    for _ in 0..MAX_PAGES {
        let mut body = json!({
            "beneficiaries": [{ "identityType": "Msa", "identityValue": tickets.user, "localTicketReference": tickets.puid }],
            "market": market,
            "maxPageSize": 100,
            "validityType": "Valid",
            "productKinds": ["Game", "Durable"],
        });
        if let Some(token) = &continuation {
            body["continuationToken"] = Value::String(token.clone());
        }
        let page: CollectionsPage = client
            .post(COLLECTIONS_QUERY)
            .header("Authorization", &tickets.device)
            .json(&body)
            .send()
            .await
            .and_then(|r| r.error_for_status())
            .map_err(Failure::network)?
            .json()
            .await
            .map_err(Failure::network)?;
        for item in page.items {
            if item.status != "Active" {
                continue;
            }
            if item.product_kind == "Durable" {
                if addons.len() < MAX_ADDONS && valid_id(&item.product_id) {
                    addons.insert(item.product_id.to_ascii_uppercase());
                }
                continue;
            }
            if item.product_kind != "Game" {
                continue;
            }
            let acquisition = if item.order_id.as_deref().is_some_and(|s| !s.is_empty()) {
                Acquisition::Purchased
            } else if item.recurrence_id.as_deref().is_some_and(|s| !s.is_empty()) {
                Acquisition::GamePass
            } else {
                Acquisition::Other
            };
            let slot = result.entry(item.product_id).or_insert(acquisition);
            *slot = (*slot).max(acquisition);
        }
        match page.continuation_token {
            Some(token) if !token.is_empty() => continuation = Some(token),
            _ => break,
        }
    }
    Ok((result, addons))
}

fn valid_id(id: &str) -> bool {
    id.len() == 12 && id.bytes().all(|b| b.is_ascii_alphanumeric())
}

// An add-on's game ("addOnParent": exactly one, not itself) and what Launchme shows of it.
pub fn addon(product: &Value) -> Option<(String, Value)> {
    let id = product["ProductId"].as_str().filter(|id| valid_id(id))?.to_ascii_uppercase();
    if product["ProductType"].as_str() != Some("Durable") || !bundle_children(product).is_empty() {
        return None;
    }
    let mut parents: Vec<String> = Vec::new();
    for market in product["MarketProperties"].as_array().into_iter().flatten() {
        for related in market["RelatedProducts"].as_array().into_iter().flatten() {
            if related["RelationshipType"].as_str() != Some("addOnParent") {
                continue;
            }
            if let Some(parent) = related["RelatedProductId"].as_str().filter(|p| valid_id(p)) {
                let parent = parent.to_ascii_uppercase();
                if !parents.contains(&parent) {
                    parents.push(parent);
                }
            }
        }
    }
    let [parent] = parents.as_slice() else { return None };
    if *parent == id {
        return None;
    }
    let local = &product["LocalizedProperties"][0];
    let title = local["ProductTitle"].as_str().map(str::trim).filter(|t| !t.is_empty() && t.len() <= 256)?;
    // Content (a package to download) or only a licence.
    let content = product["DisplaySkuAvailabilities"].as_array().into_iter().flatten().any(|dsa| {
        let properties = &dsa["Sku"]["Properties"];
        properties["IsTrial"] != json!(true)
            && properties["Packages"].as_array().into_iter().flatten().any(|pkg| {
                pkg["PackageFormat"].as_str() == Some("MSIXVC")
                    && pkg["Architectures"].as_array().into_iter().flatten().any(|a| a.as_str() == Some("x64"))
                    && pkg["PlatformDependencies"]
                        .as_array()
                        .into_iter()
                        .flatten()
                        .any(|d| d["PlatformName"].as_str() == Some("Windows.Desktop"))
            })
    });
    let image = image(&local["Images"], &["BoxArt", "Poster", "Tile", "SuperHeroArt"]).filter(|u| u.starts_with("https://") && u.len() < 2048);
    Some((parent.clone(), json!({ "product_id": id, "title": title, "content": content, "image": image })))
}

// The add-ons' games, best effort: a failure only leaves the lists out.
async fn addons(client: &reqwest::Client, ids: &[String], market: &str, language: &str) -> HashMap<String, Vec<Value>> {
    let mut result: HashMap<String, Vec<Value>> = HashMap::new();
    let languages = format!("{language},neutral");
    for chunk in ids.chunks(BATCH) {
        let response = client
            .get(with(DISPLAY_CATALOG, &[("bigIds", chunk.join(",").as_str()), ("market", market), ("languages", languages.as_str())]))
            .send()
            .await
            .and_then(|r| r.error_for_status());
        let Ok(value) = (match response {
            Ok(response) => response.json::<Value>().await,
            Err(e) => Err(e),
        }) else {
            crate::output::log("add-ons: request failed");
            return result;
        };
        for product in value["Products"].as_array().into_iter().flatten() {
            if let Some((parent, entry)) = addon(product) {
                let list = result.entry(parent).or_default();
                if list.len() < MAX_GAME_ADDONS {
                    list.push(entry);
                }
            }
        }
    }
    result
}

// Active subscription ids; an error here only means "no Game Pass list", never a failed sync.
async fn subscriptions(client: &reqwest::Client, tickets: &StoreTickets, market: &str, language: &str) -> Vec<String> {
    let response = client
        .post(with(SUBSCRIPTIONS, &[("market", market), ("language", language)]))
        .header("x-ms-api-version", "1.1")
        .header("authorization", &tickets.user)
        .header("ms-cv", CorrelationVector::new().to_string())
        .json(&json!({ "productIds": SUBSCRIPTION_IDS }))
        .send()
        .await
        .and_then(|r| r.error_for_status());
    let Ok(response) = response else {
        crate::output::log("subscriptions: request failed");
        return Vec::new();
    };
    let Ok(value) = response.json::<Value>().await else {
        return Vec::new();
    };
    let mut active: Vec<String> = value["entitlements"]
        .as_object()
        .into_iter()
        .flatten()
        .filter(|(_, e)| e["status"].as_str() == Some("Active"))
        .map(|(id, _)| id.clone())
        .collect();
    active.sort();
    active
}

async fn gamepass_catalog(client: &reqwest::Client, subscription: &str, market: &str, language: &str) -> Vec<String> {
    let ids = |value: Value| -> Vec<String> {
        value
            .as_array()
            .into_iter()
            .flatten()
            .filter_map(|e| e["id"].as_str().map(str::to_owned))
            .collect()
    };
    let v3 = client
        .get(with(SIGLS, &[
            ("language", language),
            ("market", market),
            ("id", ALL_PC_GAMES),
            ("platformContext", "pc"),
            ("subscriptionContext", subscription),
        ]))
        .send()
        .await
        .and_then(|r| r.error_for_status());
    if let Ok(response) = v3
        && let Ok(value) = response.json::<Value>().await
    {
        let list = ids(value);
        if !list.is_empty() {
            return list;
        }
    }
    let v2 = client
        .get(with(SIGLS_V2, &[("id", ALL_PC_GAMES_EA), ("language", language), ("market", market)]))
        .send()
        .await
        .and_then(|r| r.error_for_status());
    match v2 {
        Ok(response) => response.json::<Value>().await.map(ids).unwrap_or_default(),
        Err(_) => Vec::new(),
    }
}

// The collection's ids after its metadata row ({"siglId": ...}), in its order.
pub fn recent_ids(value: &Value) -> Vec<String> {
    let Some(rows) = value.as_array().filter(|rows| rows.len() <= 1001) else { return Vec::new() };
    if rows.first().and_then(|m| m["siglId"].as_str()) != Some(RECENTLY_ADDED_PC) {
        return Vec::new();
    }
    let mut ids: Vec<String> = Vec::new();
    for row in &rows[1..] {
        if let Some(id) = row["id"].as_str().filter(|id| id.len() == 12 && id.bytes().all(|b| b.is_ascii_alphanumeric()))
            && !ids.iter().any(|known| known == id)
        {
            ids.push(id.to_string());
        }
    }
    ids
}

async fn recently_added(client: &reqwest::Client, subscription: &str, market: &str, language: &str) -> Vec<String> {
    let context = subscription.to_ascii_lowercase();
    let response = client
        .get(with(SIGLS, &[
            ("id", RECENTLY_ADDED_PC),
            ("language", language),
            ("market", market),
            ("platformContext", "pc"),
            ("subscriptionContext", context.as_str()),
        ]))
        .timeout(std::time::Duration::from_secs(20))
        .send()
        .await
        .and_then(|r| r.error_for_status());
    match response {
        Ok(response) => response.json::<Value>().await.map(|v| recent_ids(&v)).unwrap_or_default(),
        Err(_) => {
            crate::output::log("recently added: request failed");
            Vec::new()
        }
    }
}

// The games a full (not trial, not pre-order) SKU bundles: BundledSkus' BigIds.
pub fn bundle_children(product: &Value) -> Vec<String> {
    let mut children: Vec<String> = Vec::new();
    for availability in product["DisplaySkuAvailabilities"].as_array().into_iter().flatten() {
        let sku = &availability["Sku"];
        let properties = &sku["Properties"];
        if properties["IsTrial"] != json!(false) || properties["IsPreOrder"] == json!(true) {
            continue;
        }
        if matches!(sku["SkuType"].as_str().map(str::to_ascii_lowercase).as_deref(), Some("trial") | Some("demo")) {
            continue;
        }
        for entry in properties["BundledSkus"].as_array().into_iter().flatten() {
            if let Some(id) = entry["BigId"].as_str().filter(|id| id.len() == 12 && id.bytes().all(|b| b.is_ascii_alphanumeric())) {
                let id = id.to_ascii_uppercase();
                if !children.contains(&id) && children.len() < MAX_BUNDLE_CHILDREN {
                    children.push(id);
                }
            }
        }
    }
    children
}

// reqwest 0.13 builds query strings only with its query feature; url does it here.
fn with(base: &str, params: &[(&str, &str)]) -> String {
    url::Url::parse_with_params(base, params)
        .map(String::from)
        .unwrap_or_else(|_| base.to_string())
}

fn https(uri: &str) -> String {
    if let Some(rest) = uri.strip_prefix("//") {
        format!("https://{rest}")
    } else {
        uri.to_string()
    }
}

fn image(images: &Value, purposes: &[&str]) -> Option<String> {
    let images = images.as_array()?;
    purposes.iter().find_map(|purpose| {
        images
            .iter()
            .filter(|i| i["ImagePurpose"].as_str() == Some(*purpose))
            .max_by_key(|i| i["Width"].as_u64().unwrap_or(0))
            .and_then(|i| i["Uri"].as_str())
            .map(https)
    })
}

// The Windows.Desktop package, if the product runs on PC (GDK MSIXVC, MSIXVC2 or an older UWP one).
fn pc_package(product: &Value) -> Option<(String, Option<String>, u64)> {
    product["DisplaySkuAvailabilities"]
        .as_array()?
        .iter()
        .flat_map(|dsa| dsa["Sku"]["Properties"]["Packages"].as_array().into_iter().flatten())
        .filter(|pkg| {
            pkg["PlatformDependencies"]
                .as_array()
                .into_iter()
                .flatten()
                .any(|d| d["PlatformName"].as_str() == Some("Windows.Desktop"))
        })
        .map(|pkg| {
            (
                pkg["PackageFormat"].as_str().unwrap_or("").to_string(),
                pkg["ContentId"].as_str().map(str::to_owned),
                pkg["MaxDownloadSizeInBytes"].as_u64().unwrap_or(0),
            )
        })
        .max_by_key(|(_, _, size)| *size)
}

// Only games: an add-on (Durable) or a consumable has a Windows.Desktop package too, and a
// bundle lists them beside its game (Forza Horizon 4 Ultimate: the game and six packs).
fn not_a_game(product: &Value) -> bool {
    matches!(product["ProductType"].as_str(), Some("Durable" | "Consumable" | "UnmanagedConsumable" | "Pass"))
}

fn describe(product: &Value) -> Option<(String, Value)> {
    if not_a_game(product) {
        return None;
    }
    let id = product["ProductId"].as_str()?.to_string();
    let local = &product["LocalizedProperties"][0];
    let (format, content_id, size) = pc_package(product)?;
    let images = &local["Images"];
    let title_id = product["AlternateIds"]
        .as_array()
        .into_iter()
        .flatten()
        .find(|a| a["IdType"].as_str() == Some("XboxTitleId"))
        .and_then(|a| a["Value"].as_str())
        .map(str::to_owned);
    let own_install = match format.as_str() {
        "MSIXVC" => "supported",
        "MSIXVC2" => "unsupported",
        _ => "not_gdk",
    };
    Some((
        id.clone(),
        json!({
            "product_id": id,
            "title": local["ProductTitle"].as_str().unwrap_or(""),
            "publisher": local["PublisherName"].as_str(),
            "developer": local["DeveloperName"].as_str(),
            "description": local["ShortDescription"].as_str(),
            "release_date": product["MarketProperties"][0]["OriginalReleaseDate"].as_str(),
            "pfn": product["Properties"]["PackageFamilyName"].as_str(),
            "xbox_title_id": title_id,
            "content_id": content_id,
            "package_format": format,
            "download_size": size,
            "own_install": own_install,
            "images": {
                "poster": image(images, &["Poster"]),
                "box": image(images, &["BoxArt", "Tile"]),
                "hero": image(images, &["SuperHeroArt", "TitledHeroArt"]),
                "logo": image(images, &["Logo"]),
            },
        }),
    ))
}

// The PC games among `ids` and, for the others, the games their SKUs bundle.
async fn details(
    client: &reqwest::Client,
    ids: &[String],
    market: &str,
    language: &str,
) -> Result<(HashMap<String, Value>, HashMap<String, Vec<String>>), Failure> {
    let mut result = HashMap::new();
    let mut bundles = HashMap::new();
    let languages = format!("{language},neutral");
    for (index, chunk) in ids.chunks(BATCH).enumerate() {
        crate::output::progress("details", (index * BATCH) as u64, ids.len() as u64);
        let value: Value = client
            .get(with(DISPLAY_CATALOG, &[("bigIds", chunk.join(",").as_str()), ("market", market), ("languages", languages.as_str())]))
            .send()
            .await
            .and_then(|r| r.error_for_status())
            .map_err(Failure::network)?
            .json()
            .await
            .map_err(Failure::network)?;
        for product in value["Products"].as_array().into_iter().flatten() {
            if let Some((id, game)) = describe(product) {
                result.insert(id, game);
            } else if let Some(id) = product["ProductId"].as_str() {
                let children = bundle_children(product);
                if !children.is_empty() {
                    bundles.insert(id.to_string(), children);
                }
            }
        }
    }
    Ok((result, bundles))
}

pub fn write_atomic(path: &Path, value: &Value) -> Result<(), Failure> {
    let storage = |e: std::io::Error| Failure::new("storage", format!("{}: {e}", path.display()));
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).map_err(storage)?;
    }
    let part = path.with_extension("part");
    std::fs::write(&part, serde_json::to_vec_pretty(value).expect("json")).map_err(storage)?;
    std::fs::rename(&part, path).map_err(storage)
}

pub async fn run(client: &reqwest::Client, tokens: &TokenManager, market: &str, language: &str, out: &Path) -> Outcome {
    let tickets = store_tickets(client, tokens).await?;
    crate::output::progress("collections", 0, 0);
    let (owned, mut owned_addons) = owned(client, &tickets, market).await?;
    let subscriptions = subscriptions(client, &tickets, market, language).await;
    let pass = [PC_GAME_PASS, ULTIMATE]
        .into_iter()
        .find(|id| subscriptions.iter().any(|s| s == id));
    let catalog = match pass {
        Some(subscription) => gamepass_catalog(client, subscription, market, language).await,
        None => Vec::new(),
    };

    let mut acquisitions: BTreeMap<String, Acquisition> = owned;
    for id in &catalog {
        acquisitions.entry(id.clone()).or_insert(Acquisition::GamePass);
    }
    let ids: Vec<String> = acquisitions.keys().cloned().collect();
    let (mut details, bundles) = details(client, &ids, market, language).await?;

    // A bought (or claimed) bundle gives its games; a subscription's never makes them bought.
    let mut bundled: BTreeMap<String, (Acquisition, String)> = BTreeMap::new();
    for (root, children) in &bundles {
        let Some(&acquisition) = acquisitions.get(root) else { continue };
        if acquisition == Acquisition::GamePass {
            continue;
        }
        for child in children {
            let better = acquisitions.get(child).is_none_or(|known| *known < acquisition);
            if better && bundled.get(child).is_none_or(|(known, _)| *known < acquisition) {
                bundled.insert(child.clone(), (acquisition, root.clone()));
            }
        }
    }
    let missing: Vec<String> = bundled.keys().filter(|id| !details.contains_key(*id)).cloned().collect();
    if !missing.is_empty() {
        let (more, _) = self::details(client, &missing, market, language).await?;
        details.extend(more);
    }
    // What a bought bundle gives besides games (its add-ons) is the account's: listed under their game.
    for id in bundled.keys().filter(|id| !details.contains_key(*id)) {
        if owned_addons.len() < MAX_ADDONS {
            owned_addons.insert(id.clone());
        }
    }
    let mut bundle_of: HashMap<String, String> = HashMap::new();
    for (child, (acquisition, root)) in bundled {
        acquisitions.insert(child.clone(), acquisition);
        bundle_of.insert(child, root);
    }

    let owned_addons: Vec<String> = owned_addons.into_iter().collect();
    crate::output::progress("add-ons", 0, owned_addons.len() as u64);
    let mut addons = addons(client, &owned_addons, market, language).await;
    let recent = match pass {
        Some(subscription) => recently_added(client, subscription, market, language).await,
        None => Vec::new(),
    };
    let games: Vec<Value> = acquisitions
        .iter()
        .filter_map(|(id, acquisition)| {
            let mut game = details.remove(id)?;
            game["acquisition"] = json!(acquisition.name());
            game["in_game_pass"] = json!(catalog.iter().any(|c| c == id));
            if let Some(root) = bundle_of.get(id) {
                game["bundle"] = json!(root);
            }
            if let Some(mut list) = addons.remove(id) {
                list.sort_by(|a, b| a["title"].as_str().cmp(&b["title"].as_str()));
                game["addons"] = json!(list);
            }
            Some(game)
        })
        .collect();
    let recent: Vec<&String> = recent
        .iter()
        .filter(|id| games.iter().any(|g| g["product_id"].as_str() == Some(id.as_str())))
        .take(RECENT_LIMIT)
        .collect();
    let synced_at = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let username = tokens.get_user().map(|u| u.username).unwrap_or_default();
    let count = games.len();
    write_atomic(
        out,
        &json!({
            "version": 1,
            "synced_at": synced_at,
            "username": username,
            "market": market,
            "subscriptions": subscriptions,
            "game_pass": pass,
            "gamepass_recent": recent,
            "games": games,
        }),
    )?;
    Ok(json!({ "games": count, "subscriptions": subscriptions, "game_pass": pass }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn recently_added_keeps_the_collection_order_and_valid_ids() {
        let value = json!([{"siglId": RECENTLY_ADDED_PC, "requiresShuffling": "False"}, {"id": "9NBLGGH4R315"}, {"id": "bad"},
            {"id": "9NBLGGH4R315"}, {"id": "9P4D0K92BM7V"}]);
        assert_eq!(recent_ids(&value), vec!["9NBLGGH4R315".to_string(), "9P4D0K92BM7V".to_string()]);
        assert!(recent_ids(&json!([{"siglId": "other"}, {"id": "9NBLGGH4R315"}])).is_empty());
        assert!(recent_ids(&json!({})).is_empty());
    }

    #[test]
    fn bundles_give_their_full_skus_games_only() {
        let product = json!({"DisplaySkuAvailabilities": [
            {"Sku": {"SkuType": "full", "Properties": {"IsTrial": false, "BundledSkus": [{"BigId": "9nblggh4r315"}, {"BigId": "x"}]}}},
            {"Sku": {"SkuType": "trial", "Properties": {"IsTrial": false, "BundledSkus": [{"BigId": "9P4D0K92BM7V"}]}}},
            {"Sku": {"Properties": {"IsTrial": false, "IsPreOrder": true, "BundledSkus": [{"BigId": "9WZDNCRFJ3TJ"}]}}},
            {"Sku": {"Properties": {"IsTrial": false, "BundledSkus": [{"BigId": "9NBLGGH4R315"}, {"BigId": "9MV0B5HZVK9Z"}]}}}
        ]});
        assert_eq!(bundle_children(&product), vec!["9NBLGGH4R315".to_string(), "9MV0B5HZVK9Z".to_string()]);
        assert!(bundle_children(&json!({})).is_empty());
    }

    #[test]
    fn an_addon_with_a_pc_package_is_no_game() {
        let product = |kind: &str| json!({
            "ProductId": "9NTV1K54GVTT", "ProductType": kind,
            "LocalizedProperties": [{"ProductTitle": "Forza Horizon 4: Fortune Island", "Images": []}],
            "MarketProperties": [{}],
            "DisplaySkuAvailabilities": [{"Sku": {"Properties": {"Packages": [{
                "PackageFormat": "MSIXVC", "ContentId": "00000000-0000-0000-0000-000000000001", "MaxDownloadSizeInBytes": 10,
                "PlatformDependencies": [{"PlatformName": "Windows.Desktop"}]}]}}}]
        });
        assert!(describe(&product("Durable")).is_none());
        assert!(describe(&product("Consumable")).is_none());
        assert!(describe(&product("Game")).is_some());
    }

    #[test]
    fn addons_name_their_one_game_and_whether_they_download() {
        let package = json!({"PackageFormat": "MSIXVC", "Architectures": ["x64"], "PlatformDependencies": [{"PlatformName": "Windows.Desktop"}]});
        let product = |kind: &str, parents: Value, packages: Value| json!({
            "ProductId": "9nexpansion1", "ProductType": kind,
            "LocalizedProperties": [{"ProductTitle": " Moonlit: Deep Archive ", "Images": [{"ImagePurpose": "BoxArt", "Width": 10, "Uri": "//store-images.s-microsoft.com/a.png"}]}],
            "MarketProperties": [{"RelatedProducts": parents}],
            "DisplaySkuAvailabilities": [{"Sku": {"Properties": {"IsTrial": false, "Packages": packages}}}]
        });
        let parent = json!([{"RelationshipType": "addOnParent", "RelatedProductId": "9nblggh4r315"}, {"RelationshipType": "Bundle", "RelatedProductId": "9P4D0K92BM7V"}]);
        let (game, entry) = addon(&product("Durable", parent.clone(), json!([package]))).unwrap();
        assert_eq!(game, "9NBLGGH4R315");
        assert_eq!(entry["product_id"], "9NEXPANSION1");
        assert_eq!(entry["title"], "Moonlit: Deep Archive");
        assert_eq!(entry["content"], true);
        assert_eq!(entry["image"], "https://store-images.s-microsoft.com/a.png");
        // Licence only: no PC package.
        let (_, licence) = addon(&product("Durable", parent.clone(), json!([]))).unwrap();
        assert_eq!(licence["content"], false);
        // Not an add-on, two parents, none, or itself.
        assert!(addon(&product("Game", parent, json!([]))).is_none());
        assert!(addon(&product("Durable", json!([{"RelationshipType": "addOnParent", "RelatedProductId": "9NBLGGH4R315"},
            {"RelationshipType": "addOnParent", "RelatedProductId": "9P4D0K92BM7V"}]), json!([]))).is_none());
        assert!(addon(&product("Durable", json!([]), json!([]))).is_none());
        assert!(addon(&product("Durable", json!([{"RelationshipType": "addOnParent", "RelatedProductId": "9NEXPANSION1"}]), json!([]))).is_none());
    }
}
