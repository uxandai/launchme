// Install (and update) a GDK game: stream the MSIXVC from the Xbox CDN and extract its files
// straight into the game folder, as xodus-cli's `streaming` command (GPL-3.0, the Xodus authors).
// Files the package keeps encrypted (the executables) stay encrypted on disk; `launch-prepare`
// decrypts them with a fresh license for each session. <dest>\.xodus-streaming.msixvc keeps the
// package header so the next update downloads only changed files.

use std::collections::HashMap;
use std::path::Path;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use futures_util::{StreamExt, stream};
use msixvc::streaming::{HttpRead, PrefixCacheFile};
use msixvc::xvd::{SegmentFile, XvdFile};
use serde_json::json;
use tokio::fs::OpenOptions;
use xodus::tokens::TokenManager;

use crate::output::{Failure, Outcome};

pub const MARKER: &str = ".launchme-xbox.json";
pub const HEADER: &str = ".xodus-streaming.msixvc";
const HEADER_PART: &str = ".xodus-streaming-tmp.msixvc";

fn storage(path: &Path, e: impl std::fmt::Display) -> Failure {
    Failure::new("storage", format!("{}: {e}", path.display()))
}

fn internal(e: Box<dyn std::error::Error>) -> Failure {
    Failure::new("internal", e.to_string())
}

fn changed(name: &str, remote: &SegmentFile, local: &HashMap<String, SegmentFile>) -> bool {
    match local.get(name) {
        Some(previous) => remote.data_hashs != previous.data_hashs || remote.data_hashs.is_empty(),
        None => true,
    }
}

async fn local_files(header: &Path) -> HashMap<String, SegmentFile> {
    let mut files = HashMap::new();
    let Ok(mut file) = OpenOptions::new().read(true).open(header).await else {
        return files;
    };
    let Ok(xvd) = XvdFile::parse(&mut file).await else {
        return files;
    };
    if let Ok(user_files) = xvd.parse_user_package_files(&mut file).await {
        for (name, entry) in &user_files {
            if name == "SegmentMetadata.bin"
                && let Ok(segments) = xvd.parse_segment_metadata(&mut file, entry).await
            {
                files = segments;
            }
        }
    }
    if let Ok(ntfs) = xvd.parse_ntfs_segment_metadata(&mut file, !files.is_empty()).await {
        files.extend(ntfs);
    }
    files
}

pub async fn run(
    client: &reqwest::Client,
    tokens: &TokenManager,
    product: &str,
    dest: &Path,
    market: &str,
    parallel: usize,
    profile: &Path,
) -> Outcome {
    if !dest.is_absolute() {
        return Err(Failure::new("usage", "--dest must be absolute"));
    }
    crate::tokens::signed_in(client, tokens).await?;
    crate::output::progress("resolving", 0, 0);
    let content_id = crate::package::content_id(client, product, market, 0).await?;
    let package = crate::package::base_package(client, tokens, &content_id).await?;
    let Some(file) = package.package_files.iter().find(|p| p.file_name.ends_with(".msixvc")) else {
        return Err(Failure::new(
            "unsupported_package",
            "no MSIXVC file (MSIXVC2 and UWP packages are not supported yet)",
        ));
    };
    let url = file
        .cdn_root_paths
        .first()
        .map(|root| format!("{root}{}", file.relative_url))
        .ok_or_else(|| Failure::new("network", "no CDN"))?;

    std::fs::create_dir_all(dest).map_err(|e| storage(dest, e))?;
    let header_part = dest.join(HEADER_PART);
    let header = dest.join(HEADER);

    crate::output::progress("metadata", 0, 0);
    let reader = HttpRead::open(client.clone(), url.clone(), None::<fn(u64, u64)>)
        .await
        .map_err(Failure::network)?;
    let length = reader.len();
    let mut remote = PrefixCacheFile::new(reader, length, &header_part)
        .await
        .map_err(|e| storage(&header_part, e))?;
    let xvd = XvdFile::parse(&mut remote).await.map_err(internal)?;
    let mut remote_files: HashMap<String, SegmentFile> = HashMap::new();
    for (name, entry) in &xvd.parse_user_package_files(&mut remote).await.map_err(internal)? {
        if name == "SegmentMetadata.bin" {
            remote_files = xvd.parse_segment_metadata(&mut remote, entry).await.map_err(internal)?;
        }
    }
    let ntfs = xvd
        .parse_ntfs_segment_metadata(&mut remote, !remote_files.is_empty())
        .await
        .map_err(internal)?;
    remote_files.extend(ntfs);
    let local = local_files(&header).await;

    let key = crate::licensing::content_key(client, tokens, profile, &xvd.content_id().to_string(), market, Some(product))
        .await?
        .key;

    let jobs: Vec<(String, SegmentFile)> = remote_files
        .iter()
        .filter(|(name, entry)| changed(name, entry, &local))
        .map(|(name, entry)| {
            (
                name.clone(),
                SegmentFile {
                    offset: entry.offset,
                    length: entry.length,
                    data_hashs: vec![],
                    keep_encrypted: entry.keep_encrypted,
                },
            )
        })
        .collect();
    let total: u64 = jobs.iter().map(|(_, f)| f.length).sum();
    let free = fs2::available_space(dest).map_err(|e| storage(dest, e))?;
    if free < total {
        return Err(Failure::new(
            "storage",
            format!("not enough free space: need {total} bytes, {free} available"),
        ));
    }

    let done = Arc::new(AtomicU64::new(0));
    let finished = Arc::new(AtomicBool::new(false));
    let ticker = {
        let done = done.clone();
        let finished = finished.clone();
        tokio::spawn(async move {
            while !finished.load(Ordering::Relaxed) {
                crate::output::progress("download", done.load(Ordering::Relaxed), total);
                tokio::time::sleep(std::time::Duration::from_millis(500)).await;
            }
        })
    };

    let first_error: std::sync::Mutex<Option<Failure>> = std::sync::Mutex::new(None);
    let xvd_ref = &xvd;
    let url_ref = url.as_str();
    stream::iter(jobs)
        .for_each_concurrent(parallel.clamp(1, 16), |(name, entry)| {
            let done = done.clone();
            let first_error = &first_error;
            async move {
                if first_error.lock().map(|e| e.is_some()).unwrap_or(true) {
                    return;
                }
                let relative = crate::config::package_relative(&name);
                if relative
                    .components()
                    .any(|c| !matches!(c, std::path::Component::Normal(_)))
                {
                    *first_error.lock().unwrap() = Some(Failure::new("internal", format!("unsafe path {name}")));
                    return;
                }
                let target = dest.join(&relative);
                let result: Result<(), Failure> = async {
                    if let Some(folder) = target.parent() {
                        std::fs::create_dir_all(folder).map_err(|e| storage(folder, e))?;
                    }
                    let mut out = OpenOptions::new()
                        .create(true)
                        .write(true)
                        .truncate(true)
                        .open(&target)
                        .await
                        .map_err(|e| storage(&target, e))?;
                    let mut last = 0u64;
                    xvd_ref
                        .download_file_http(client, url_ref, &mut out, &entry, key, |position, _| {
                            done.fetch_add(position.saturating_sub(last), Ordering::Relaxed);
                            last = position;
                        })
                        .await
                        .map_err(|e| Failure::network(format!("{name}: {e}")))
                }
                .await;
                if let Err(failure) = result
                    && let Ok(mut slot) = first_error.lock()
                    && slot.is_none()
                {
                    *slot = Some(failure);
                }
            }
        })
        .await;
    finished.store(true, Ordering::Relaxed);
    let _ = ticker.await;
    if let Some(failure) = first_error.into_inner().ok().flatten() {
        return Err(failure);
    }
    crate::output::progress("download", total, total);

    drop(remote);
    let _ = std::fs::remove_file(&header);
    std::fs::rename(&header_part, &header).map_err(|e| storage(&header, e))?;

    let config = crate::config::read(dest);
    let executable = config
        .as_ref()
        .and_then(|c| c.executables.first().cloned())
        .map(|p| p.to_string_lossy().into_owned());
    let installed_at = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let marker = json!({
        "version": 1,
        "product_id": product,
        "content_id": content_id,
        "package_version": package.version,
        "identity": config.as_ref().and_then(|c| c.name.clone()),
        "identity_version": config.as_ref().and_then(|c| c.version.clone()),
        "display_name": config.as_ref().and_then(|c| c.display_name.clone()),
        "executable": executable,
        "installed_at": installed_at,
    });
    let marker_path = dest.join(MARKER);
    std::fs::write(&marker_path, serde_json::to_vec_pretty(&marker).expect("json"))
        .map_err(|e| storage(&marker_path, e))?;
    Ok(marker)
}
