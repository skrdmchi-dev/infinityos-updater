// Thin C++20 GTK4 + Libadwaita GUI shell (non-Steel C++20 adapter rewritten by Gemini).
// Build with -DUSE_GTK `pkg-config --cflags --libs gtk4 libadwaita-1` on Debian/Linux.
// All UI state transitions run in Steel (ui_state.steel).
#define main __steel_unused_main
#include "../cpp-core/generated.cpp"
#undef main

#ifdef USE_GTK
#include <adwaita.h>

struct UiCtx {
    std::string ui_json;
    std::string state_json;
    std::string cat_json;
    GtkWidget* label = nullptr;
};

static void dispatch_ui_event(UiCtx* ctx, const char* ev_json) {
    steel::Val next_json = steel_ui_reduce(
        steel::Val(ctx->ui_json),
        steel::Val(ev_json),
        steel::Val(ctx->state_json),
        steel::Val(ctx->cat_json)
    );
    ctx->ui_json = next_json.to_string();
    steel::Val parsed = steel::json_parse_val(next_json);
    std::string txt = steel::idx_get(parsed, steel::Val("banner_text")).to_string();
    gtk_label_set_text(GTK_LABEL(ctx->label), txt.c_str());
}

static void on_select_all(GtkButton*, gpointer data) { dispatch_ui_event(static_cast<UiCtx*>(data), "{\"type\":\"select_all_updates\"}"); }
static void on_preview(GtkButton*, gpointer data) { dispatch_ui_event(static_cast<UiCtx*>(data), "{\"type\":\"preview_plan\"}"); }

static void on_activate(GtkApplication* app, gpointer data) {
    auto* ctx = static_cast<UiCtx*>(data);
    ctx->label = gtk_label_new("Ready");
    GtkWidget* b1 = gtk_button_new_with_label("Select All Updates");
    GtkWidget* b2 = gtk_button_new_with_label("Preview Plan");
    g_signal_connect(b1, "clicked", G_CALLBACK(on_select_all), ctx);
    g_signal_connect(b2, "clicked", G_CALLBACK(on_preview), ctx);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(box), ctx->label);
    gtk_box_append(GTK_BOX(box), b1);
    gtk_box_append(GTK_BOX(box), b2);

    GtkWidget* win = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "InfinityOS Updater");
    gtk_window_set_default_size(GTK_WINDOW(win), 640, 420);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), box);
    gtk_window_present(GTK_WINDOW(win));
}
#endif

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    const char* sp = std::getenv("INFINITY_STATE_PATH");
    const char* cp = std::getenv("INFINITY_CATALOGUE_PATH");
    std::string st = sp ? steel::read_file_str(steel::Val(sp)).to_string() : "";
    std::string ct = cp ? steel::read_file_str(steel::Val(cp)).to_string() : "{}";

#ifdef USE_GTK
    UiCtx ctx{"", st, ct, nullptr};
    AdwApplication* app = adw_application_new("co.uk.infinityos.Updater", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &ctx);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
#else
    steel::Val out = steel_ui_reduce(steel::Val(""), steel::Val("{\"type\":\"select_all_updates\"}"), steel::Val(st), steel::Val(ct));
    std::cout << "InfinityOS Updater C++20 GUI (built without -DUSE_GTK; Steel UI state active)\n"
              << out.to_string() << "\n";
    return 0;
#endif
}
