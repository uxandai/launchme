// launchme-xbox: Launchme's Xbox account helper, built on Xodus
// (https://github.com/xodus-gaming/xodus, GPL-3.0). Copyright (C) the Launchme authors.
// Licensed under the GNU General Public License v3.0 or later; parts of login.rs, install.rs and
// licensing.rs follow xodus-cli's commands (GPL-3.0, the Xodus authors); offline.rs and xboxlive.rs
// follow the Nexus backend's (https://github.com/domsvoboda/nexus-backend, GPL-3.0).
//
// Contract with Launchme (src/store/xbox_account.*):
//   launchme-xbox.exe --profile <dir> <command> [options]
// * stdout: exactly one JSON object when the command ends: {"ok":true,...} or
//   {"ok":false,"error":"<code>","message":"..."}. Codes: usage, signed_out, cancelled, network,
//   not_entitled, device_limit, declined, unsupported_package, not_found, storage, internal.
//
// Commands: version, status, login, logout, library --out <file> [--market --language],
// install --product <id> --dest <dir>, launch-prepare/run/launch-cleanup --dir <dir>,
// offline-pc [--set true|false --confirm yes] (the designated offline PC; <profile>/offline.json),
// activity --out <file> (played titles: last play, achievement counts, minutes),
// achievements --title <id>, updates (newer packages of the games `install` put down).
// * stderr: progress as JSON lines {"progress":{"stage":"...","done":N,"total":M}}; anything else
//   on stderr is a log line.
// * The profile folder holds everything (DPAPI-protected tokens, the WebView2 data of the sign-in
//   window, the synced library); nothing is written next to the EXE or into the user's keychain.
//
// Linux (launchme-xbox, shipped next to Launchme): the same commands and answers. The tokens are
// 0600 files in the 0700 profile, the sign-in window is GTK 4 + WebKitGTK 6.0, and `run` takes the
// Wine/Proton command Launchme built for the game after `--`:
//   launchme-xbox --profile <dir> run --dir <game> [--market <m>] [--file-map yes] -- <runner...> <Game.exe>

// No console window: Launchme starts it hidden with pipes, and `run` stands in for the game.
#![windows_subsystem = "windows"]

mod config;
mod install;
mod launch;
mod library;
mod licensing;
#[cfg(windows)]
mod login;
#[cfg(target_os = "linux")]
#[path = "login_linux.rs"]
mod login;
mod offline;
mod output;
mod package;
mod store;
mod tokens;
mod updates;
mod xboxlive;
use std::path::PathBuf;
use std::sync::Arc;

use output::{Failure, Outcome};

pub const USER_AGENT: &str = concat!("launchme-xbox/", env!("CARGO_PKG_VERSION"));

struct Arguments {
    profile: PathBuf,
    command: String,
    options: Vec<(String, String)>,
    // Everything after "--" (Linux `run`: the Wine/Proton command line for the game).
    #[cfg_attr(windows, allow(dead_code))]
    trailing: Vec<String>,
}

impl Arguments {
    fn option(&self, name: &str) -> Option<&str> {
        self.options.iter().find(|(k, _)| k == name).map(|(_, v)| v.as_str())
    }

    fn required(&self, name: &str) -> Result<&str, Failure> {
        self.option(name)
            .filter(|v| !v.is_empty())
            .ok_or_else(|| Failure::new("usage", format!("missing --{name}")))
    }
}

// --profile <dir> <command> [--key value]... [-- trailing...] Values never start with "--".
fn parse(args: Vec<String>) -> Result<Arguments, Failure> {
    let mut profile = None;
    let mut command = None;
    let mut options = Vec::new();
    let mut trailing = Vec::new();
    let mut it = args.into_iter().skip(1);
    while let Some(arg) = it.next() {
        if arg == "--" {
            trailing.extend(it.by_ref());
            break;
        }
        if let Some(name) = arg.strip_prefix("--") {
            let value = it
                .next()
                .ok_or_else(|| Failure::new("usage", format!("--{name} needs a value")))?;
            if name == "profile" {
                profile = Some(PathBuf::from(value));
            } else {
                options.push((name.to_string(), value));
            }
        } else if command.is_none() {
            command = Some(arg);
        } else {
            return Err(Failure::new("usage", format!("unexpected argument {arg}")));
        }
    }
    let profile = profile.ok_or_else(|| Failure::new("usage", "missing --profile"))?;
    if !profile.is_absolute() {
        return Err(Failure::new("usage", "--profile must be absolute"));
    }
    let command = command.ok_or_else(|| Failure::new("usage", "missing command"))?;
    Ok(Arguments {
        profile,
        command,
        options,
        trailing,
    })
}

fn client() -> reqwest::Client {
    reqwest::ClientBuilder::new()
        .user_agent(USER_AGENT)
        .build()
        .expect("HTTP client")
}

async fn run(args: Arguments) -> Outcome {
    std::fs::create_dir_all(&args.profile)
        .map_err(|e| Failure::new("storage", format!("profile folder: {e}")))?;
    // Linux: the profile holds the account's tokens; it is the user's alone even when the helper
    // is started without Launchme (which creates it so already).
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&args.profile, std::fs::Permissions::from_mode(0o700))
            .map_err(|e| Failure::new("storage", format!("profile folder: {e}")))?;
    }
    let tokens = Arc::new(store::token_manager(&args.profile));
    let client = client();
    match args.command.as_str() {
        "version" => Ok(serde_json::json!({ "version": env!("CARGO_PKG_VERSION"), "xodus": "514dd56f", "offline": true })),
        "status" => tokens::status(&tokens),
        "login" => login::run(&client, &tokens, &args.profile).await,
        "logout" => {
            // The stored offline licenses belong to this account: they go with its session.
            if offline::revoke(&args.profile).is_err() {
                output::log("offline: stored licenses could not be removed");
            }
            tokens::logout(&tokens)
        }
        "library" => {
            library::run(
                &client,
                &tokens,
                args.option("market").unwrap_or("US"),
                args.option("language").unwrap_or("en-US"),
                &PathBuf::from(args.required("out")?),
            )
            .await
        }
        "install" => {
            install::run(
                &client,
                &tokens,
                args.required("product")?,
                &PathBuf::from(args.required("dest")?),
                args.option("market").unwrap_or("neutral"),
                args.option("parallel").and_then(|p| p.parse().ok()).unwrap_or(4),
                &args.profile,
            )
            .await
        }
        "launch-prepare" => {
            launch::prepare(
                &client,
                &tokens,
                &PathBuf::from(args.required("dir")?),
                args.option("market").unwrap_or("neutral"),
                &args.profile,
            )
            .await
        }
        #[cfg(windows)]
        "run" => {
            launch::run(
                &client,
                &tokens,
                &PathBuf::from(args.required("dir")?),
                args.option("market").unwrap_or("neutral"),
                &args.profile,
            )
            .await
        }
        #[cfg(not(windows))]
        "run" => {
            launch::run(
                &client,
                &tokens,
                &PathBuf::from(args.required("dir")?),
                args.option("market").unwrap_or("neutral"),
                &args.profile,
                &args.trailing,
                args.option("file-map") == Some("yes"),
            )
            .await
        }
        "launch-cleanup" => launch::cleanup(&PathBuf::from(args.required("dir")?)).await,
        "offline-pc" => {
            let set = match args.option("set") {
                None => None,
                Some("true") => Some(true),
                Some("false") => Some(false),
                Some(_) => return Err(Failure::new("usage", "--set takes true or false")),
            };
            offline::offline_pc(&client, &tokens, &args.profile, set, args.option("confirm") == Some("yes")).await
        }
        "activity" => xboxlive::activity(&client, &tokens, &PathBuf::from(args.required("out")?)).await,
        "achievements" => xboxlive::achievements(&client, &tokens, args.required("title")?).await,
        "updates" => updates::run(&client, &tokens, &args.profile).await,
        other => Err(Failure::new("usage", format!("unknown command {other}"))),
    }
}

fn main() {
    output::install_panic_hook();
    let outcome = match parse(std::env::args().collect()) {
        Err(failure) => Err(failure),
        Ok(args) => {
            let runtime = tokio::runtime::Builder::new_multi_thread()
                .enable_all()
                .build()
                .expect("tokio runtime");
            match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| runtime.block_on(run(args)))) {
                Ok(outcome) => outcome,
                Err(_) => Err(Failure::new("internal", output::last_panic())),
            }
        }
    };
    let code = output::finish(outcome);
    std::process::exit(code);
}
