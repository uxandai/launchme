// A session of a game installed by `install`. Xodus on Linux decrypts the package's encrypted
// files into memory for Wine (WINE_DLL_FILE_MAP); Windows has no such loader hook, so
// `launch-prepare` asks for a fresh license and decrypts them next to the encrypted copies
// (<file>.xodus-enc) for the session, and `launch-cleanup` puts the encrypted ones back.
//
// Linux `run` wraps the Wine/Proton command Launchme built for the game (after `--`): it decrypts
// the same way by default, which works with any Wine or Proton, or, with `--file-map yes`, into
// memfds handed to the runner through WINE_DLL_FILE_MAP, which only Xodus' Wine build reads (the
// encrypted files then never leave the disk decrypted). An open xgameruntime.dll, when one is
// available, is placed next to the game's executable for the session (linux::runtime).

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use msixvc::xvd::{SegmentFile, XvdFile};
use serde_json::{Value, json};
use tokio::fs::{File, OpenOptions};
use xodus::tokens::TokenManager;

use crate::install::{HEADER, MARKER};
use crate::output::{Failure, Outcome};

const SESSION: &str = ".launchme-xbox-session.json";
const ENCRYPTED: &str = "xodus-enc";

fn storage(path: &Path, e: impl std::fmt::Display) -> Failure {
    Failure::new("storage", format!("{}: {e}", path.display()))
}

fn encrypted_copy(path: &Path) -> PathBuf {
    let mut name = path.as_os_str().to_owned();
    name.push(".");
    name.push(ENCRYPTED);
    PathBuf::from(name)
}

fn relative(name: &str) -> Option<PathBuf> {
    let path = crate::config::package_relative(name);
    path.components()
        .all(|c| matches!(c, std::path::Component::Normal(_)))
        .then_some(path)
}

async fn encrypted_files(header: &Path) -> Result<(XvdFile, HashMap<String, SegmentFile>), Failure> {
    let internal = |e: Box<dyn std::error::Error>| Failure::new("internal", e.to_string());
    let mut file = OpenOptions::new()
        .read(true)
        .open(header)
        .await
        .map_err(|_| Failure::new("not_found", "not installed by Launchme (no package header)"))?;
    let xvd = XvdFile::parse(&mut file).await.map_err(internal)?;
    let mut files = HashMap::new();
    for (name, entry) in &xvd.parse_user_package_files(&mut file).await.map_err(internal)? {
        if name == "SegmentMetadata.bin" {
            files = xvd.parse_segment_metadata(&mut file, entry).await.map_err(internal)?;
        }
    }
    if files.is_empty() {
        files = xvd.parse_ntfs_segment_metadata(&mut file, false).await.map_err(internal)?;
    }
    files.retain(|_, f| f.keep_encrypted);
    Ok((xvd, files))
}

fn marker(dir: &Path) -> Option<Value> {
    serde_json::from_slice(&std::fs::read(dir.join(MARKER)).ok()?).ok()
}

// The Store product the install marker names (offline.json keys the game by it).
fn product(dir: &Path) -> Option<String> {
    marker(dir)?["product_id"].as_str().filter(|p| crate::offline::valid_product(p)).map(str::to_owned)
}

fn executable(dir: &Path) -> Option<String> {
    let marker = marker(dir)?;
    marker["executable"]
        .as_str()
        .map(str::to_owned)
        .or_else(|| {
            crate::config::read(dir)
                .and_then(|c| c.executables.first().cloned())
                .map(|p| p.to_string_lossy().into_owned())
        })
}

pub async fn prepare(client: &reqwest::Client, tokens: &TokenManager, dir: &Path, market: &str, profile: &Path) -> Outcome {
    if dir.join(SESSION).exists() {
        cleanup(dir).await?;
    }
    let (xvd, files) = encrypted_files(&dir.join(HEADER)).await?;
    let licensed = crate::licensing::content_key(client, tokens, profile, &xvd.content_id().to_string(), market, product(dir).as_deref()).await?;
    let key = licensed.key;
    let mut decrypted: Vec<String> = Vec::new();
    let total: u64 = files.values().map(|f| f.length).sum();
    let mut done = 0u64;
    let session = dir.join(SESSION);
    for (name, entry) in &files {
        let Some(rel) = relative(name) else { continue };
        let plain = dir.join(&rel);
        let sealed = encrypted_copy(&plain);
        if !sealed.exists() {
            if !plain.exists() {
                continue; // Not part of this install (skipped NTFS metadata, optional file).
            }
            std::fs::rename(&plain, &sealed).map_err(|e| storage(&plain, e))?;
        }
        // Recorded before writing so a crash still lets cleanup restore it.
        decrypted.push(rel.to_string_lossy().into_owned());
        std::fs::write(&session, serde_json::to_vec(&json!({ "files": decrypted })).expect("json"))
            .map_err(|e| storage(&session, e))?;
        let mut input = File::open(&sealed).await.map_err(|e| storage(&sealed, e))?;
        let mut output = OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&plain)
            .await
            .map_err(|e| storage(&plain, e))?;
        xvd.mount_mem_fd(&mut input, &mut output, entry, key, |_, _| {})
            .await
            .map_err(|e| Failure::new("internal", format!("{name}: {e}")))?;
        done += entry.length;
        crate::output::progress("decrypt", done, total);
    }
    Ok(json!({
        "executable": executable(dir),
        "decrypted": decrypted,
        "offline": licensed.from_cache,
        "offline_until": licensed.offline_until,
    }))
}

// Launchme's Graj for a game it installed: prepare, start the game from the package root, wait for
// it (Launchme's job holds both processes, so its session and "Zamknij grę" see the game), then put
// the encrypted files back. A failure is shown to the user: nothing else would tell them.
#[cfg(windows)]
pub async fn run(client: &reqwest::Client, tokens: &TokenManager, dir: &Path, market: &str, profile: &Path) -> Outcome {
    let result: Outcome = async {
        let prepared = prepare(client, tokens, dir, market, profile).await?;
        let relative = prepared["executable"]
            .as_str()
            .and_then(relative)
            .ok_or_else(|| Failure::new("not_found", "MicrosoftGame.config names no PC executable"))?;
        let exe = dir.join(relative);
        let status = tokio::process::Command::new(&exe)
            .current_dir(dir)
            .status()
            .await
            .map_err(|e| Failure::new("internal", format!("{}: {e}", exe.display())));
        let cleaned = cleanup(dir).await;
        let status = status?;
        cleaned?;
        Ok(json!({ "exit_code": status.code() }))
    }
    .await;
    if let Err(failure) = &result {
        crate::output::alert(&failure.message);
    }
    result
}

pub async fn cleanup(dir: &Path) -> Outcome {
    let session = dir.join(SESSION);
    let files: Vec<String> = match std::fs::read(&session) {
        Ok(bytes) => serde_json::from_slice::<Value>(&bytes)
            .ok()
            .and_then(|v| v["files"].as_array().cloned())
            .unwrap_or_default()
            .into_iter()
            .filter_map(|v| v.as_str().map(str::to_owned))
            .collect(),
        Err(_) => return Ok(json!({ "restored": 0 })),
    };
    let mut restored = 0;
    for name in &files {
        let Some(rel) = relative(name) else { continue };
        let plain = dir.join(rel);
        let sealed = encrypted_copy(&plain);
        if !sealed.exists() {
            continue;
        }
        if plain.exists() {
            std::fs::remove_file(&plain).map_err(|e| storage(&plain, e))?;
        }
        std::fs::rename(&sealed, &plain).map_err(|e| storage(&sealed, e))?;
        restored += 1;
    }
    std::fs::remove_file(&session).map_err(|e| storage(&session, e))?;
    Ok(json!({ "restored": restored }))
}

// Linux: Launchme's Graj for a game it installed. `command` is the Wine/Proton command line Launchme
// built for the game's executable (empty: $WINE or wine from PATH runs it, from the package root).
// The helper waits for it, passes Launchme's signals on, then seals the files again.
#[cfg(not(windows))]
pub async fn run(
    client: &reqwest::Client,
    tokens: &TokenManager,
    dir: &Path,
    market: &str,
    profile: &Path,
    command: &[String],
    file_map: bool,
) -> Outcome {
    let result: Outcome = async {
        linux::remove_runtime(dir); // Left by a session that was killed.
        let (prepared, mapped) = if file_map {
            let (prepared, mapped) = linux::prepare_mapped(client, tokens, dir, market, profile).await?;
            (prepared, Some(mapped))
        } else {
            (prepare(client, tokens, dir, market, profile).await?, None)
        };
        let relative = prepared["executable"]
            .as_str()
            .and_then(relative)
            .ok_or_else(|| Failure::new("not_found", "MicrosoftGame.config names no PC executable"))?;
        let exe = dir.join(relative);
        let runtime = linux::provide_runtime(dir, &exe, profile);
        let status = linux::start(command, &exe, dir, runtime.is_some(), mapped.as_ref()).await;
        drop(mapped);
        linux::remove_runtime(dir);
        let cleaned = if file_map { Ok(json!({})) } else { cleanup(dir).await };
        let status = status?;
        cleaned?;
        Ok(json!({
            "exit_code": status.code(),
            "xgameruntime": runtime.is_some(),
            "file_map": file_map,
            "offline": prepared["offline"],
        }))
    }
    .await;
    if let Err(failure) = &result {
        crate::output::alert(&failure.message);
    }
    result
}

#[cfg(not(windows))]
mod linux {
    use std::ffi::OsString;
    use std::os::fd::{FromRawFd, OwnedFd};
    use std::path::{Path, PathBuf};
    use std::process::ExitStatus;

    use serde_json::{Value, json};
    use tokio::fs::File;
    use tokio::signal::unix::{SignalKind, signal};
    use xodus::tokens::TokenManager;

    use super::{HEADER, SESSION, cleanup, encrypted_files, executable, product, relative, storage};
    use crate::output::Failure;

    const RUNTIME_RECORD: &str = ".launchme-xgameruntime.json";
    const RUNTIME_DLL: &str = "xgameruntime.dll";

    // The decrypted files, kept open (and inherited by the runner) for the whole session.
    pub struct Mapped {
        _files: Vec<std::fs::File>,
        map: String,
        entry: Option<String>,
    }

    // Xodus' `run`: every encrypted file decrypted into a memfd the runner inherits, named in
    // WINE_DLL_FILE_MAP as "<fd>:\??\Z:\<unix path with backslashes>", '|' between files.
    pub async fn prepare_mapped(
        client: &reqwest::Client,
        tokens: &TokenManager,
        dir: &Path,
        market: &str,
        profile: &Path,
    ) -> Result<(Value, Mapped), Failure> {
        if dir.join(SESSION).exists() {
            cleanup(dir).await?; // A decrypted-in-place session must not be what gets mapped.
        }
        let (xvd, files) = encrypted_files(&dir.join(HEADER)).await?;
        let licensed =
            crate::licensing::content_key(client, tokens, profile, &xvd.content_id().to_string(), market, product(dir).as_deref()).await?;
        let key = licensed.key;
        let executable = executable(dir);
        let wanted = executable.as_deref().and_then(relative);
        let canonical = std::fs::canonicalize(dir).map_err(|e| storage(dir, e))?;
        let prefix = canonical.to_string_lossy().replace('/', "\\");
        let prefix = prefix.trim_end_matches('\\');
        let total: u64 = files.values().map(|f| f.length).sum();
        let mut done = 0u64;
        let mut mapped = Mapped { _files: Vec::new(), map: String::new(), entry: None };
        for (name, entry) in &files {
            let Some(rel) = relative(name) else { continue };
            let source = dir.join(&rel);
            if !source.exists() {
                continue;
            }
            // Without close-on-exec: the runner (and Wine in it) inherits the descriptor.
            // SAFETY: memfd_create with a NUL-terminated name; the result is checked before use.
            let fd = unsafe { libc::memfd_create(c"launchme-xbox".as_ptr(), 0) };
            if fd < 0 {
                return Err(Failure::new("storage", format!("memfd: {}", std::io::Error::last_os_error())));
            }
            // SAFETY: a fresh descriptor this process owns alone.
            let owned = unsafe { OwnedFd::from_raw_fd(fd) };
            let mut output = File::from_std(std::fs::File::from(owned));
            let mut input = File::open(&source).await.map_err(|e| storage(&source, e))?;
            xvd.mount_mem_fd(&mut input, &mut output, entry, key, |_, _| {})
                .await
                .map_err(|e| Failure::new("internal", format!("{name}: {e}")))?;
            let file = output.into_std().await;
            let nt = format!("\\??\\Z:{prefix}\\{}", rel.to_string_lossy().replace('/', "\\"));
            if wanted.as_ref() == Some(&rel) {
                mapped.entry = Some(nt.clone());
            }
            if !mapped.map.is_empty() {
                mapped.map.push('|');
            }
            mapped.map.push_str(&format!("{fd}:{nt}"));
            mapped._files.push(file);
            done += entry.length;
            crate::output::progress("decrypt", done, total);
        }
        Ok((
            json!({
                "executable": executable,
                "mapped": mapped._files.len(),
                "offline": licensed.from_cache,
                "offline_until": licensed.offline_until,
            }),
            mapped,
        ))
    }

    // xgameruntime.dll from the open xgameruntime project, when there is one: $LAUNCHME_XGAMERUNTIME,
    // xgameruntime/ next to this helper (Launchme's package), or <profile>/xgameruntime/.
    fn runtime_source(profile: &Path) -> Option<PathBuf> {
        let mut candidates = Vec::new();
        if let Some(chosen) = std::env::var_os("LAUNCHME_XGAMERUNTIME").filter(|v| !v.is_empty()) {
            candidates.push(PathBuf::from(chosen));
        }
        if let Some(folder) = std::env::current_exe().ok().and_then(|e| e.parent().map(Path::to_path_buf)) {
            candidates.push(folder.join("xgameruntime").join(RUNTIME_DLL));
        }
        candidates.push(profile.join("xgameruntime").join(RUNTIME_DLL));
        candidates.into_iter().find(|c| c.is_absolute() && c.is_file())
    }

    // Next to the game's executable for the session (the loader looks there first), recorded so the
    // session's end (or the next one) removes it; a DLL of that name the game ships stays untouched.
    pub fn provide_runtime(dir: &Path, exe: &Path, profile: &Path) -> Option<PathBuf> {
        let source = runtime_source(profile)?;
        let target = exe.parent()?.join(RUNTIME_DLL);
        if target.exists() {
            crate::output::log(&format!("{} exists: the game's own xgameruntime is used", target.display()));
            return None;
        }
        std::fs::write(dir.join(RUNTIME_RECORD), json!({ "file": target }).to_string()).ok()?;
        match std::fs::copy(&source, &target) {
            Ok(_) => Some(target),
            Err(e) => {
                crate::output::log(&format!("xgameruntime: {e}"));
                remove_runtime(dir);
                None
            }
        }
    }

    pub fn remove_runtime(dir: &Path) {
        let record = dir.join(RUNTIME_RECORD);
        let Ok(bytes) = std::fs::read(&record) else { return };
        if let Some(file) = serde_json::from_slice::<Value>(&bytes).ok().and_then(|v| v["file"].as_str().map(PathBuf::from))
            && file.starts_with(dir)
            && file.file_name().is_some_and(|n| n == RUNTIME_DLL)
        {
            let _ = std::fs::remove_file(file);
        }
        let _ = std::fs::remove_file(record);
    }

    // WINEDLLOVERRIDES with the native xgameruntime first, after whatever the runner set.
    fn overrides(existing: Option<String>) -> String {
        match existing.filter(|v| !v.trim().is_empty()) {
            Some(value) if value.to_ascii_lowercase().contains("xgameruntime") => value,
            Some(value) => format!("{value};xgameruntime=n,b"),
            None => "xgameruntime=n,b".to_string(),
        }
    }

    fn same_file(argument: &OsString, exe: &Path) -> bool {
        let path = Path::new(argument);
        path == exe || std::fs::canonicalize(path).ok().zip(std::fs::canonicalize(exe).ok()).is_some_and(|(a, b)| a == b)
    }

    pub async fn start(
        command: &[String],
        exe: &Path,
        dir: &Path,
        runtime: bool,
        mapped: Option<&Mapped>,
    ) -> Result<ExitStatus, Failure> {
        let mut argv: Vec<OsString> = if command.is_empty() {
            let wine = std::env::var_os("WINE").filter(|w| !w.is_empty()).unwrap_or_else(|| "wine".into());
            vec![wine, exe.as_os_str().to_owned()]
        } else {
            command.iter().map(OsString::from).collect()
        };
        if let Some(entry) = mapped.and_then(|m| m.entry.as_ref()) {
            // Xodus' Wine finds the decrypted image by its NT name.
            if let Some(argument) = argv.iter_mut().skip(1).find(|a| same_file(a, exe)) {
                *argument = OsString::from(entry);
            }
        }
        let mut process = tokio::process::Command::new(&argv[0]);
        process.args(&argv[1..]);
        if command.is_empty() {
            process.current_dir(dir); // Launchme's runner command keeps the folder Launchme gave it.
        }
        if runtime {
            process.env("WINEDLLOVERRIDES", overrides(std::env::var("WINEDLLOVERRIDES").ok()));
        }
        if let Some(mapped) = mapped {
            process.env("WINE_DLL_FILE_MAP", &mapped.map);
        }
        let mut child = process
            .spawn()
            .map_err(|e| Failure::new("internal", format!("{}: {e}", argv[0].to_string_lossy())))?;
        let pid = child.id().map(|p| p as libc::pid_t);
        let forward = |signal: libc::c_int| {
            if let Some(pid) = pid {
                // SAFETY: a signal to the runner this helper started and still waits for.
                unsafe { libc::kill(pid, signal) };
            }
        };
        let signal_error = |e: std::io::Error| Failure::new("internal", format!("signals: {e}"));
        let mut terminate = signal(SignalKind::terminate()).map_err(signal_error)?;
        let mut interrupt = signal(SignalKind::interrupt()).map_err(signal_error)?;
        let mut hangup = signal(SignalKind::hangup()).map_err(signal_error)?;
        loop {
            tokio::select! {
                status = child.wait() => return status.map_err(|e| Failure::new("internal", e.to_string())),
                _ = terminate.recv() => forward(libc::SIGTERM),
                _ = interrupt.recv() => forward(libc::SIGINT),
                _ = hangup.recv() => forward(libc::SIGHUP),
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn overrides_keep_the_runners_and_add_xgameruntime_once() {
            assert_eq!(overrides(None), "xgameruntime=n,b");
            assert_eq!(overrides(Some("d3d11=n,b".into())), "d3d11=n,b;xgameruntime=n,b");
            assert_eq!(overrides(Some("xgameruntime=b".into())), "xgameruntime=b");
        }

        #[test]
        fn the_runtime_goes_next_to_the_executable_and_away_again() {
            let root = std::env::temp_dir().join(format!("launchme-xbox-runtime-{}", std::process::id()));
            let game = root.join("game");
            let profile = root.join("profile");
            std::fs::create_dir_all(game.join("Bin")).unwrap();
            std::fs::create_dir_all(profile.join("xgameruntime")).unwrap();
            std::fs::write(profile.join("xgameruntime").join(RUNTIME_DLL), b"MZ runtime").unwrap();
            let exe = game.join("Bin").join("Game.exe");
            let placed = provide_runtime(&game, &exe, &profile).unwrap();
            assert_eq!(placed, game.join("Bin").join(RUNTIME_DLL));
            assert_eq!(std::fs::read(&placed).unwrap(), b"MZ runtime");
            remove_runtime(&game);
            assert!(!placed.exists() && !game.join(RUNTIME_RECORD).exists());
            // The game's own DLL of that name is never replaced or removed.
            std::fs::write(&placed, b"the game's").unwrap();
            assert!(provide_runtime(&game, &exe, &profile).is_none());
            remove_runtime(&game);
            assert_eq!(std::fs::read(&placed).unwrap(), b"the game's");
            let _ = std::fs::remove_dir_all(root);
        }

        #[tokio::test]
        async fn the_runner_command_runs_with_the_overrides() {
            let root = std::env::temp_dir().join(format!("launchme-xbox-start-{}", std::process::id()));
            std::fs::create_dir_all(&root).unwrap();
            let out = root.join("out");
            let command = vec![
                "/bin/sh".to_string(),
                "-c".to_string(),
                format!("printf %s \"$WINEDLLOVERRIDES\" > {}; exit 7", out.display()),
            ];
            let status = start(&command, &root.join("Game.exe"), &root, true, None).await.unwrap();
            assert_eq!(status.code(), Some(7));
            assert_eq!(std::fs::read_to_string(&out).unwrap(), overrides(std::env::var("WINEDLLOVERRIDES").ok()));
            let _ = std::fs::remove_dir_all(root);
        }
    }
}
