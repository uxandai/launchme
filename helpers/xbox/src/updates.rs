// `updates`: whether the games the helper installed have a newer package. Each install marker
// (written by `install`) names the content id and the package version it installed; the Xbox
// package service's GetBasePackage names the current one. An update installs like the first time
// (`install` into the same folder downloads only the files that changed).

use std::path::{Path, PathBuf};

use serde_json::{Value, json};
use xodus::tokens::TokenManager;

use crate::install::MARKER;
use crate::output::Outcome;

// "1.2.3.4" (four parts, each up to 65535; Microsoft's package versions are 64-bit quads).
pub fn version(text: &str) -> Option<[u64; 4]> {
    let parts: Vec<u64> = text.split('.').map(|p| p.parse::<u64>().ok().filter(|n| *n <= 65535)).collect::<Option<_>>()?;
    parts.try_into().ok()
}

// A plain number (the service's packed form: four 16-bit parts) or a dotted quad.
pub fn comparable(text: &str) -> Option<[u64; 4]> {
    if let Some(quad) = version(text) {
        return Some(quad);
    }
    let packed: u64 = text.parse().ok()?;
    Some([packed >> 48, (packed >> 32) & 0xffff, (packed >> 16) & 0xffff, packed & 0xffff])
}

pub fn newer(installed: &str, available: &str) -> Option<bool> {
    Some(comparable(available)? > comparable(installed)?)
}

// The folders Launchme recorded (<profile>/installs.json, its own file).
fn installs(profile: &Path) -> Vec<PathBuf> {
    let Ok(bytes) = std::fs::read(profile.join("installs.json")) else { return Vec::new() };
    let Ok(Value::Array(entries)) = serde_json::from_slice::<Value>(&bytes) else { return Vec::new() };
    entries
        .iter()
        .filter_map(|e| e["directory"].as_str().map(PathBuf::from))
        .filter(|d| d.is_absolute())
        .take(4096)
        .collect()
}

pub async fn run(client: &reqwest::Client, tokens: &TokenManager, profile: &Path) -> Outcome {
    crate::tokens::signed_in(client, tokens).await?;
    let folders = installs(profile);
    let mut result = Vec::new();
    for (index, folder) in folders.iter().enumerate() {
        crate::output::progress("updates", index as u64, folders.len() as u64);
        let Ok(bytes) = std::fs::read(folder.join(MARKER)) else { continue };
        let Ok(marker) = serde_json::from_slice::<Value>(&bytes) else { continue };
        let (Some(product), Some(content_id), Some(installed)) =
            (marker["product_id"].as_str(), marker["content_id"].as_str(), marker["package_version"].as_str())
        else {
            continue;
        };
        if !crate::offline::valid_product(product) {
            continue;
        }
        match crate::package::base_package(client, tokens, content_id).await {
            Ok(package) => result.push(json!({
                "product_id": product,
                "installed": installed,
                "available": package.version,
                "update": newer(installed, &package.version).unwrap_or(installed != package.version),
            })),
            Err(failure) if failure.code == "network" => return Err(failure),
            Err(failure) => crate::output::log(&format!("updates: {product}: {}", failure.message)),
        }
    }
    Ok(json!({ "updates": result }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions_compare_as_quads_in_either_form() {
        assert_eq!(newer("1.0.2.0", "1.0.10.0"), Some(true));
        assert_eq!(newer("1.0.10.0", "1.0.2.0"), Some(false));
        assert_eq!(newer("1.0.0.0", "1.0.0.0"), Some(false));
        // 1.2.3.4 packed: 0x0001_0002_0003_0004.
        assert_eq!(comparable("281483566841860"), Some([1, 2, 3, 4]));
        assert_eq!(newer("281483566841860", "1.2.3.5"), Some(true));
        assert_eq!(newer("x", "1.0.0.0"), None);
        assert!(version("1.2.3").is_none() && version("1.2.3.70000").is_none());
    }
}
