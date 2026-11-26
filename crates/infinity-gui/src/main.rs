//! Thin GTK4 + Libadwaita shell driven by Steel's `ui_state.steel` reducer.
//! Enable the `gtk` feature on Linux (`cargo build -p infinity-gui --features gtk`).

use std::env;
use std::fs;
use std::process::ExitCode;

#[cfg(feature = "gtk")]
fn run_gtk(state_json: String, catalogue_json: String) -> ExitCode {
    use gtk4::prelude::*;
    use libadwaita::prelude::*;
    use std::cell::RefCell;
    use std::rc::Rc;

    let app = libadwaita::Application::builder()
        .application_id("co.uk.infinityos.Updater")
        .build();

    app.connect_activate(move |app| {
        let ui_state = Rc::new(RefCell::new(String::new()));
        let status_label = gtk4::Label::new(Some("Ready"));
        let check_btn = gtk4::Button::with_label("Select All Updates");
        let plan_btn = gtk4::Button::with_label("Preview Plan");

        let st_c = state_json.clone();
        let cat_c = catalogue_json.clone();
        let ui_c = Rc::clone(&ui_state);
        let lbl_c = status_label.clone();
        check_btn.connect_clicked(move |_| {
            if let Ok(next) = infinity_steel_core::ui_reduce(
                &ui_c.borrow(),
                r#"{"type":"select_all_updates"}"#,
                &st_c,
                &cat_c,
            ) {
                if let Some(txt) = next.get("banner_text").and_then(|v| v.as_str()) {
                    lbl_c.set_text(txt);
                }
                if let Ok(s) = serde_json::to_string(&next) {
                    *ui_c.borrow_mut() = s;
                }
            }
        });

        let st_p = state_json.clone();
        let cat_p = catalogue_json.clone();
        let ui_p = Rc::clone(&ui_state);
        let lbl_p = status_label.clone();
        plan_btn.connect_clicked(move |_| {
            if let Ok(next) = infinity_steel_core::ui_reduce(
                &ui_p.borrow(),
                r#"{"type":"preview_plan"}"#,
                &st_p,
                &cat_p,
            ) {
                if let Some(txt) = next.get("banner_text").and_then(|v| v.as_str()) {
                    lbl_p.set_text(txt);
                }
                if let Ok(s) = serde_json::to_string(&next) {
                    *ui_p.borrow_mut() = s;
                }
            }
        });

        let content = gtk4::Box::new(gtk4::Orientation::Vertical, 12);
        content.append(&status_label);
        content.append(&check_btn);
        content.append(&plan_btn);

        let window = libadwaita::ApplicationWindow::builder()
            .application(app)
            .title("InfinityOS Updater")
            .default_width(640)
            .default_height(420)
            .content(&content)
            .build();
        window.present();
    });

    app.run();
    ExitCode::SUCCESS
}

#[cfg(not(feature = "gtk"))]
fn run_headless(state_json: &str, catalogue_json: &str) -> ExitCode {
    match infinity_steel_core::ui_reduce("", r#"{"type":"select_all_updates"}"#, state_json, catalogue_json) {
        Ok(state) => {
            println!("InfinityOS Updater GUI (built without --features gtk; Steel UI state active)");
            println!("{}", serde_json::to_string_pretty(&state).unwrap_or_default());
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("GUI reducer error: {e}");
            ExitCode::from(1)
        }
    }
}

fn main() -> ExitCode {
    let state_path = env::var("INFINITY_STATE_PATH").unwrap_or_else(|_| "infinity-state.json".to_string());
    let cat_path = env::var("INFINITY_CATALOGUE_PATH").unwrap_or_else(|_| "catalogue.json".to_string());
    let state_json = fs::read_to_string(&state_path).unwrap_or_default();
    let catalogue_json = fs::read_to_string(&cat_path).unwrap_or_else(|_| "{}".to_string());

    #[cfg(feature = "gtk")]
    {
        run_gtk(state_json, catalogue_json)
    }
    #[cfg(not(feature = "gtk"))]
    {
        run_headless(&state_json, &catalogue_json)
    }
}
