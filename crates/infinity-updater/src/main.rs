//! Thin CLI shell: Steel (`runner.steel`) handles args, env, catalogue, state,
//! planning, and atomic saves; Rust only executes live HTTPS downloads and extract.

use std::env;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode, Stdio};

fn apply_live_action(act: &infinity_steel_core::ReconcileAction, cache: &Path) -> Result<(), String> {
    if act.action == "remove" {
        if act.scope == "system" {
            return invoke_pkexec(act, "");
        }
        return Ok(());
    }
    if act.url.is_empty() {
        return Ok(());
    }
    let arc = cache.join(format!("{}-{}.tar.xz", act.id, act.to));
    if !arc.exists() {
        infinity_runtime::download_resumable(&act.url, &arc).map_err(|e| e.to_string())?;
    }
    if act.scope == "system" {
        invoke_pkexec(act, &arc.to_string_lossy())
    } else {
        infinity_runtime::extract_tar_xz_safe(&arc, Path::new(&act.install_path))
            .map_err(|e| e.to_string())
    }
}

fn invoke_pkexec(act: &infinity_steel_core::ReconcileAction, archive_path: &str) -> Result<(), String> {
    let payload = serde_json::json!({
        "op": act.action, "component_id": act.id, "scope": act.scope,
        "install_path": act.install_path, "archive_path": archive_path, "sha256": act.sha256
    })
    .to_string();
    let mut child = Command::new("pkexec")
        .arg("infinity-helper")
        .stdin(Stdio::piped())
        .spawn()
        .map_err(|e| format!("pkexec launch failed: {e}"))?;
    if let Some(mut stdin) = child.stdin.take() {
        stdin.write_all(payload.as_bytes()).map_err(|e| e.to_string())?;
    }
    let status = child.wait().map_err(|e| e.to_string())?;
    if status.success() { Ok(()) } else { Err(format!("helper exited {status}")) }
}

fn main() -> ExitCode {
    let res = match infinity_steel_core::runner_cli() {
        Ok(v) => v,
        Err(e) => {
            eprintln!("Fatal: Steel runner decode failed: {e}");
            return ExitCode::from(1);
        }
    };
    if env::var("INFINITY_APPLY").ok().as_deref() == Some("1")
        && let Some(raw_acts) = res.get("actions")
        && let Ok(acts) = serde_json::from_value::<Vec<infinity_steel_core::ReconcileAction>>(raw_acts.clone())
    {
        let cache = env::var("INFINITY_CACHE_DIR").map(PathBuf::from).unwrap_or_else(|_| PathBuf::from(".infinity-cache"));
        for act in &acts {
            if let Err(e) = apply_live_action(act, &cache) {
                eprintln!("Action failed for {}: {e}", act.id);
                return ExitCode::from(1);
            }
        }
    }
    let code = res.get("exit_code").and_then(|v| v.as_i64()).unwrap_or(1);
    ExitCode::from(u8::try_from(code.clamp(0, 255)).unwrap_or(1))
}
