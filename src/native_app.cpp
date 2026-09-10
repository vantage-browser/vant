#include "native_app.h"

#include "navigation.h"

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <memory>
#include <string>

namespace {

struct WindowState {
    GtkApplication *application{};
    GtkWidget *window{};
    GtkWidget *address{};
    GtkWidget *reload_stop{};
    WebKitWebView *view{};
    vantage::NavigationPolicy policy;
    bool smoke{};
};

void load_decision(WindowState *state, const vantage::NavigationDecision &decision) {
    if (decision.kind == vantage::NavigationKind::web) {
        webkit_web_view_load_uri(state->view, decision.uri.c_str());
    } else if (decision.kind == vantage::NavigationKind::internal) {
        webkit_web_view_load_html(state->view,
            "<!doctype html><meta charset=utf-8><title>Vantage Browser</title><style>html{color-scheme:dark}body{margin:0;background:#11100f;color:#f1ede3;font:18px system-ui;display:grid;place-items:center;height:100vh}main{text-align:center}b{color:#ff725e;font-size:42px}</style><main><b>Vantage</b><p>A clearer point of view on the web.</p></main>",
            nullptr);
        gtk_editable_set_text(GTK_EDITABLE(state->address), decision.uri.c_str());
    }
}

void submit_address(GtkEntry *, WindowState *state) {
    const char *text = gtk_editable_get_text(GTK_EDITABLE(state->address));
    load_decision(state, state->policy.resolve(text ? text : ""));
}

void go_back(GtkButton *, WindowState *state) { if (webkit_web_view_can_go_back(state->view)) webkit_web_view_go_back(state->view); }
void go_forward(GtkButton *, WindowState *state) { if (webkit_web_view_can_go_forward(state->view)) webkit_web_view_go_forward(state->view); }
void reload_or_stop(GtkButton *, WindowState *state) {
    if (webkit_web_view_is_loading(state->view)) webkit_web_view_stop_loading(state->view);
    else webkit_web_view_reload(state->view);
}

void loading_changed(WebKitWebView *view, GParamSpec *, WindowState *state) {
    const bool loading = webkit_web_view_is_loading(view);
    gtk_button_set_icon_name(GTK_BUTTON(state->reload_stop),
        loading ? "process-stop-symbolic" : "view-refresh-symbolic");
    gtk_widget_set_tooltip_text(state->reload_stop, loading ? "Stop loading" : "Reload");
}

void uri_changed(WebKitWebView *view, GParamSpec *, WindowState *state) {
    const char *uri = webkit_web_view_get_uri(view);
    if (uri) gtk_editable_set_text(GTK_EDITABLE(state->address), uri);
}

void title_changed(WebKitWebView *view, GParamSpec *, WindowState *state) {
    const char *title = webkit_web_view_get_title(view);
    gtk_window_set_title(GTK_WINDOW(state->window), title && *title ? title : "Vantage Browser");
}

gboolean decide_policy(WebKitWebView *, WebKitPolicyDecision *decision,
                       WebKitPolicyDecisionType type, WindowState *state) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) return FALSE;
    auto *navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    auto *action = webkit_navigation_policy_decision_get_navigation_action(navigation);
    auto *request = webkit_navigation_action_get_request(action);
    const char *uri = webkit_uri_request_get_uri(request);
    const auto resolved = state->policy.resolve(uri ? uri : "");
    if (resolved.kind == vantage::NavigationKind::web)
        return FALSE;
    webkit_policy_decision_ignore(decision);
    if (resolved.kind == vantage::NavigationKind::internal)
        load_decision(state, resolved);
    return TRUE;
}

gboolean tls_failed(WebKitWebView *, const char *, GTlsCertificate *, GTlsCertificateFlags, WindowState *) {
    return FALSE;
}

gboolean finish_smoke(void *data) {
    auto *state = static_cast<WindowState *>(data);
    g_application_quit(G_APPLICATION(state->application));
    return G_SOURCE_REMOVE;
}

GtkWidget *icon_button(const char *icon, const char *tooltip) {
    auto *button = gtk_button_new_from_icon_name(icon);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_tooltip_text(button, tooltip);
    return button;
}

void activate(GtkApplication *application, void *user_data) {
    auto *state = static_cast<WindowState *>(user_data);
    state->application = application;
    state->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(state->window), "Vantage Browser");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 1100, 760);

    auto *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    auto *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(toolbar, 8);
    gtk_widget_set_margin_end(toolbar, 8);
    gtk_widget_set_margin_top(toolbar, 8);
    gtk_widget_set_margin_bottom(toolbar, 8);
    auto *back = icon_button("go-previous-symbolic", "Back");
    auto *forward = icon_button("go-next-symbolic", "Forward");
    state->reload_stop = icon_button("view-refresh-symbolic", "Reload");
    state->address = gtk_entry_new();
    gtk_widget_set_hexpand(state->address, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->address), "Search or enter address");
    gtk_box_append(GTK_BOX(toolbar), back);
    gtk_box_append(GTK_BOX(toolbar), forward);
    gtk_box_append(GTK_BOX(toolbar), state->reload_stop);
    gtk_box_append(GTK_BOX(toolbar), state->address);

    state->view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_widget_set_vexpand(GTK_WIDGET(state->view), TRUE);
    gtk_box_append(GTK_BOX(layout), toolbar);
    gtk_box_append(GTK_BOX(layout), GTK_WIDGET(state->view));
    gtk_window_set_child(GTK_WINDOW(state->window), layout);

    g_signal_connect(back, "clicked", G_CALLBACK(go_back), state);
    g_signal_connect(forward, "clicked", G_CALLBACK(go_forward), state);
    g_signal_connect(state->reload_stop, "clicked", G_CALLBACK(reload_or_stop), state);
    g_signal_connect(state->address, "activate", G_CALLBACK(submit_address), state);
    g_signal_connect(state->view, "notify::uri", G_CALLBACK(uri_changed), state);
    g_signal_connect(state->view, "notify::title", G_CALLBACK(title_changed), state);
    g_signal_connect(state->view, "notify::is-loading", G_CALLBACK(loading_changed), state);
    g_signal_connect(state->view, "decide-policy", G_CALLBACK(decide_policy), state);
    g_signal_connect(state->view, "load-failed-with-tls-errors", G_CALLBACK(tls_failed), state);

    gtk_window_present(GTK_WINDOW(state->window));
    if (state->smoke) {
        webkit_web_view_load_html(state->view, "<!doctype html><title>Vant smoke</title><p>ok</p>", "https://smoke.invalid/");
        g_timeout_add(900, finish_smoke, state);
    } else {
        const auto *uri = static_cast<const char *>(g_object_get_data(G_OBJECT(application), "initial-uri"));
        load_decision(state, state->policy.resolve(uri ? uri : "vantage:new"));
    }
}

} // namespace

namespace vantage {

std::string native_versions() {
    return "GTK " + std::to_string(gtk_get_major_version()) + "." +
        std::to_string(gtk_get_minor_version()) + "." + std::to_string(gtk_get_micro_version()) +
        "; WebKitGTK " + std::to_string(webkit_get_major_version()) + "." +
        std::to_string(webkit_get_minor_version()) + "." + std::to_string(webkit_get_micro_version());
}

int run_native(bool smoke, const std::string &initial_uri) {
    auto application = std::unique_ptr<GtkApplication, decltype(&g_object_unref)>(
        gtk_application_new("cv.vantage_browser.Vantage", G_APPLICATION_DEFAULT_FLAGS), &g_object_unref);
    WindowState state;
    state.smoke = smoke;
    g_object_set_data_full(G_OBJECT(application.get()), "initial-uri", g_strdup(initial_uri.c_str()), g_free);
    g_signal_connect(application.get(), "activate", G_CALLBACK(activate), &state);
    return g_application_run(G_APPLICATION(application.get()), 0, nullptr);
}

} // namespace vantage
