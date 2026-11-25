//! Minimal Rust ecosystem adapter for crates Steel does not embed:
//! Ed25519 (`ed25519-dalek`), `.tar.xz` (`tar` + `xz2`), resumable HTTPS (`reqwest`), and SHA-256 bytes.

use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use sha2::{Digest, Sha256};
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::path::Path;
use thiserror::Error;
use xz2::read::XzDecoder;
use xz2::write::XzEncoder;

#[derive(Debug, Error)]
pub enum RuntimeError {
    #[error("I/O error: {0}")]
    Io(#[from] io::Error),
    #[error("HTTP error: {0}")]
    Http(#[from] reqwest::Error),
    #[error("policy rejected: {0}")]
    Policy(String),
    #[error("signature error: {0}")]
    Signature(String),
    #[error("serialization error: {0}")]
    Json(String),
}

pub fn sha256_hex(bytes: &[u8]) -> String {
    let mut h = Sha256::new();
    h.update(bytes);
    h.finalize().iter().map(|b| format!("{b:02x}")).collect()
}

fn decode_hex(s: &str) -> Result<Vec<u8>, RuntimeError> {
    if !s.len().is_multiple_of(2) {
        return Err(RuntimeError::Signature("odd hex length".to_string()));
    }
    s.as_bytes()
        .chunks(2)
        .map(|c| {
            let pair = std::str::from_utf8(c).map_err(|_| RuntimeError::Signature("bad utf8".to_string()))?;
            u8::from_str_radix(pair, 16).map_err(|_| RuntimeError::Signature("bad hex".to_string()))
        })
        .collect()
}

pub fn sign_ed25519(secret_hex: &str, payload: &[u8]) -> Result<String, RuntimeError> {
    let arr: [u8; 32] = decode_hex(secret_hex)?
        .try_into()
        .map_err(|_| RuntimeError::Signature("signing key must be 32 bytes".to_string()))?;
    Ok(SigningKey::from_bytes(&arr)
        .sign(payload)
        .to_bytes()
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect())
}

pub fn verify_ed25519(pubkey_hex: &str, sig_hex: &str, payload: &[u8]) -> Result<(), RuntimeError> {
    let pk: [u8; 32] = decode_hex(pubkey_hex)?
        .try_into()
        .map_err(|_| RuntimeError::Signature("public key must be 32 bytes".to_string()))?;
    let sg: [u8; 64] = decode_hex(sig_hex)?
        .try_into()
        .map_err(|_| RuntimeError::Signature("signature must be 64 bytes".to_string()))?;
    let vk = VerifyingKey::from_bytes(&pk).map_err(|e| RuntimeError::Signature(e.to_string()))?;
    vk.verify(payload, &Signature::from_bytes(&sg))
        .map_err(|e| RuntimeError::Signature(e.to_string()))
}

pub fn download_resumable(url: &str, dest: &Path) -> Result<(), RuntimeError> {
    let part = dest.with_extension("part");
    if let Some(parent) = dest.parent() {
        fs::create_dir_all(parent)?;
    }
    let existing = fs::metadata(&part).map(|m| m.len()).unwrap_or(0);
    let client = reqwest::blocking::Client::builder().user_agent("infinityos-updater/0.2").build()?;
    let mut req = client.get(url);
    if existing > 0 {
        req = req.header(reqwest::header::RANGE, format!("bytes={existing}-"));
    }
    let mut resp = req.send()?.error_for_status()?;
    let append = existing > 0 && resp.status() == reqwest::StatusCode::PARTIAL_CONTENT;
    let mut out = OpenOptions::new().create(true).write(true).append(append).truncate(!append).open(&part)?;
    io::copy(&mut resp, &mut out)?;
    out.sync_all()?;
    fs::rename(&part, dest)?;
    Ok(())
}

pub fn pack_tar_xz(src_dir: &Path, archive_dest: &Path) -> Result<Vec<String>, RuntimeError> {
    let mut builder = tar::Builder::new(XzEncoder::new(File::create(archive_dest)?, 6));
    let mut entries = Vec::new();
    collect_and_pack(&mut builder, src_dir, src_dir, &mut entries)?;
    builder.into_inner()?.finish()?;
    Ok(entries)
}

fn collect_and_pack<W: Write>(
    builder: &mut tar::Builder<W>,
    root: &Path,
    dir: &Path,
    entries: &mut Vec<String>,
) -> Result<(), RuntimeError> {
    for entry in fs::read_dir(dir)? {
        let path = entry?.path();
        let rel = path.strip_prefix(root).map_err(|e| RuntimeError::Policy(e.to_string()))?;
        let rel_str = rel.to_string_lossy().replace('\\', "/");
        if path.is_dir() {
            collect_and_pack(builder, root, &path, entries)?;
        } else {
            entries.push(rel_str.clone());
            builder.append_path_with_name(&path, &rel_str)?;
        }
    }
    Ok(())
}

pub fn extract_tar_xz_safe(archive: &Path, dest_dir: &Path) -> Result<(), RuntimeError> {
    let mut names = Vec::new();
    for entry in tar::Archive::new(XzDecoder::new(File::open(archive)?)).entries()? {
        names.push(entry?.path()?.to_string_lossy().replace('\\', "/"));
    }
    let chk = infinity_steel_core::validate_archive_entries(&names)
        .map_err(|e| RuntimeError::Json(e.to_string()))?;
    if !chk.ok {
        return Err(RuntimeError::Policy(chk.reason));
    }
    fs::create_dir_all(dest_dir)?;
    tar::Archive::new(XzDecoder::new(File::open(archive)?)).unpack(dest_dir)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ed25519_and_tar_xz_roundtrip() {
        let sk_hex = "11".repeat(32);
        let pk_hex: String = SigningKey::from_bytes(&[0x11; 32]).verifying_key().to_bytes().iter().map(|b| format!("{b:02x}")).collect();
        let sig = sign_ed25519(&sk_hex, b"infinityos").ok();
        assert!(sig.is_some());
        if let Some(s) = sig {
            assert!(verify_ed25519(&pk_hex, &s, b"infinityos").is_ok());
            assert!(verify_ed25519(&pk_hex, &s, b"tampered").is_err());
        }
        if let Ok(d) = tempfile::tempdir() {
            let src = d.path().join("src");
            let out = d.path().join("out");
            let arc = d.path().join("pkg.tar.xz");
            let _ = fs::create_dir_all(&src);
            let _ = fs::write(src.join("hello.txt"), b"ok");
            assert_eq!(pack_tar_xz(&src, &arc).ok(), Some(vec!["hello.txt".to_string()]));
            assert!(extract_tar_xz_safe(&arc, &out).is_ok());
            assert_eq!(fs::read_to_string(out.join("hello.txt")).ok().as_deref(), Some("ok"));
        }
    }
}
