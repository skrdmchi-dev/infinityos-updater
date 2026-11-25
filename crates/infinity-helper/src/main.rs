//! Thin privileged `pkexec` helper shell. Policy, dpkg ownership checks, SHA-256,
//! and directory ops all run in Steel (`helper.steel`); Rust only unpacks `.tar.xz`.

use std::io::{self, Read};
use std::path::Path;
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut raw = String::new();
    if let Err(e) = io::stdin().read_to_string(&mut raw) {
        eprintln!("helper: stdin read failed: {e}");
        return ExitCode::from(1);
    }
    let res = match infinity_steel_core::helper_run(&raw) {
        Ok(v) => v,
        Err(e) => {
            eprintln!("helper: decode error: {e}");
            return ExitCode::from(2);
        }
    };
    let code = res.get("exit_code").and_then(|v| v.as_i64()).unwrap_or(1);
    if code != 0 {
        if let Some(err) = res.get("error").and_then(|v| v.as_str()) {
            eprintln!("helper: {err}");
        }
        return ExitCode::from(u8::try_from(code.clamp(0, 255)).unwrap_or(1));
    }
    if let Some(ext) = res.get("extract")
        && let (Some(arc), Some(dest)) = (
            ext.get("archive").and_then(|v| v.as_str()),
            ext.get("dest").and_then(|v| v.as_str()),
        )
        && let Err(e) = infinity_runtime::extract_tar_xz_safe(Path::new(arc), Path::new(dest))
    {
        if let Some(bp) = res.get("backup_path").and_then(|v| v.as_str())
            && !bp.is_empty()
        {
            let rb = serde_json::json!({ "op": "restore_backup", "install_path": dest, "backup_path": bp }).to_string();
            let _ = infinity_steel_core::helper_run(&rb);
        }
        eprintln!("helper: extract failed (backup restored): {e}");
        return ExitCode::from(1);
    }
    ExitCode::SUCCESS
}
