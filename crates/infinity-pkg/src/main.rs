//! Thin packager/publisher CLI: Steel (`runner.steel` + `pkg.steel`) owns catalogue
//! validation and publishing; Rust only packs `.tar.xz` and signs with Ed25519.

use std::env;
use std::fs;
use std::path::Path;
use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = env::args().skip(1).collect();
    match args.first().map(String::as_str) {
        Some("pack") if args.len() >= 3 => {
            let out = Path::new(&args[2]);
            let files = match infinity_runtime::pack_tar_xz(Path::new(&args[1]), out) {
                Ok(f) => f,
                Err(e) => { eprintln!("pack failed: {e}"); return ExitCode::from(1); }
            };
            let bytes = match fs::read(out) {
                Ok(b) => b,
                Err(e) => { eprintln!("read archive failed: {e}"); return ExitCode::from(1); }
            };
            let digest = infinity_runtime::sha256_hex(&bytes);
            let sig = match args.get(3) {
                Some(k) => match infinity_runtime::sign_ed25519(k, &bytes) {
                    Ok(s) => s,
                    Err(e) => { eprintln!("signing failed: {e}"); return ExitCode::from(1); }
                },
                None => String::new(),
            };
            println!("{}", serde_json::json!({ "archive": args[2], "file_count": files.len(), "sha256": digest, "signature": sig }));
            ExitCode::SUCCESS
        }
        Some("publish") if args.len() >= 4 => match infinity_steel_core::runner_publish(&args[1], &args[2], &args[3]) {
            Ok(v) => {
                if let Some(o) = v.get("output").and_then(|x| x.as_str()) { println!("{o}"); }
                let c = v.get("exit_code").and_then(|x| x.as_i64()).unwrap_or(1);
                ExitCode::from(u8::try_from(c.clamp(0, 255)).unwrap_or(1))
            }
            Err(e) => { eprintln!("publish error: {e}"); ExitCode::from(1) }
        },
        _ => {
            eprintln!("Usage:\n  infinity-pkg pack <src_dir> <out.tar.xz> [signing_key_hex]\n  infinity-pkg publish <catalogue.json> <id> <entry.json>");
            ExitCode::from(2)
        }
    }
}
