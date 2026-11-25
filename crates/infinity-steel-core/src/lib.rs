//! Minimal Rust FFI bridge to the Steel-compiled InfinityOS Updater core (`steel-core/*.steel`).

#[allow(
    clippy::all,
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    warnings
)]
mod generated;

use generated::SVal;
use serde::{Deserialize, Serialize};
use serde_json::Value;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PolicyCheck {
    pub ok: bool,
    pub reason: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ReconcileAction {
    pub id: String,
    pub action: String,
    pub from: String,
    pub to: String,
    #[serde(default)]
    pub scope: String,
    pub install_path: String,
    #[serde(default)]
    pub url: String,
    #[serde(default)]
    pub sha256: String,
}

pub fn validate_archive_entries(entries: &[String]) -> Result<PolicyCheck, serde_json::Error> {
    let raw = generated::steel_validate_archive_json(SVal::str_owned(serde_json::to_string(entries)?));
    serde_json::from_str(&raw.to_string())
}

pub fn ui_reduce(ui_json: &str, event_json: &str, state_json: &str, catalogue_json: &str) -> Result<Value, serde_json::Error> {
    let raw = generated::steel_ui_reduce(
        SVal::str_owned(ui_json.to_string()),
        SVal::str_owned(event_json.to_string()),
        SVal::str_owned(state_json.to_string()),
        SVal::str_owned(catalogue_json.to_string()),
    );
    serde_json::from_str(&raw.to_string())
}

pub fn helper_run(req_json: &str) -> Result<Value, serde_json::Error> {
    serde_json::from_str(&generated::steel_helper_run_json(SVal::str_owned(req_json.to_string())).to_string())
}

pub fn runner_cli() -> Result<Value, serde_json::Error> {
    serde_json::from_str(&generated::steel_runner_cli_json().to_string())
}

pub fn runner_publish(cat_path: &str, id: &str, entry_path: &str) -> Result<Value, serde_json::Error> {
    let raw = generated::steel_runner_publish_json(
        SVal::str_owned(cat_path.to_string()),
        SVal::str_owned(id.to_string()),
        SVal::str_owned(entry_path.to_string()),
    );
    serde_json::from_str(&raw.to_string())
}

#[cfg(test)]
mod tests {
    #[test]
    fn steel_self_tests_pass() {
        assert!(super::generated::steel_run_self_tests().is_truthy());
    }
}
