/* Thin C11 GTK4 + Libadwaita GUI shell (Gemini AI-assisted C11 adapter).
 * Build with -DUSE_GTK `pkg-config --cflags --libs gtk4 libadwaita-1` on Debian/Linux.
 * All UI state transitions run in Steel (ui_state.steel).
 */
#define main __steel_unused_main
#include "../c-core/generated.c"
#undef main

#ifdef USE_GTK
#include <adwaita.h>

typedef struct {
    char* ui_json;
    char* state_json;
    char* cat_json;
    GtkWidget* label;
} UiCtx;

static void dispatch_ui_event(UiCtx* ctx, const char* ev_json) {
    SVal next_json = steel_ui_reduce(sv_str(ctx->ui_json), sv_str(ev_json), sv_str(ctx->state_json), sv_str(ctx->cat_json));
    if (next_json.type == ST_STR && next_json.str) {
        free(ctx->ui_json);
        ctx->ui_json = strdup(next_json.str);
        SVal parsed = sv_json_parse(next_json);
        SVal txt = sv_map_get(parsed, "banner_text");
        if (txt.type == ST_STR && txt.str) gtk_label_set_text(GTK_LABEL(ctx->label), txt.str);
    }
}

static void on_select_all(GtkButton* btn, gpointer data) { (void)btn; dispatch_ui_event((UiCtx*)data, "{\"type\":\"select_all_updates\"}"); }
static void on_preview(GtkButton* btn, gpointer data) { (void)btn; dispatch_ui_event((UiCtx*)data, "{\"type\":\"preview_plan\"}"); }

static void on_activate(GtkApplication* app, gpointer data) {
    UiCtx* ctx = (UiCtx*)data;
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
    const char* sp = getenv("INFINITY_STATE_PATH");
    const char* cp = getenv("INFINITY_CATALOGUE_PATH");
    SVal st = sp ? sv_read_file(sv_str(sp)) : sv_str("");
    SVal ct = cp ? sv_read_file(sv_str(cp)) : sv_str("{}");

#ifdef USE_GTK
    UiCtx ctx = { strdup(""), strdup(st.type == ST_STR ? st.str : ""), strdup(ct.type == ST_STR ? ct.str : "{}"), NULL };
    AdwApplication* app = adw_application_new("co.uk.infinityos.Updater", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &ctx);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    free(ctx.ui_json); free(ctx.state_json); free(ctx.cat_json);
    sv_cleanup();
    return status;
#else
    SVal out = steel_ui_reduce(sv_str(""), sv_str("{\"type\":\"select_all_updates\"}"), st, ct);
    printf("InfinityOS Updater C11 GUI (built without -DUSE_GTK; Steel UI state active)\n%s\n", out.type == ST_STR ? out.str : "{}");
    sv_cleanup();
    return 0;
#endif
}
