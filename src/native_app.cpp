#include "native_app.h"

#include "navigation.h"

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct WindowState;

struct TabState {
    WindowState *window{};
    GtkWidget *page{};
    WebKitWebView *view{};
    GtkWidget *tab{};
    GtkWidget *label{};
    std::string internal_uri;
};

struct WindowState {
    GtkApplication *application{};
    GtkWidget *window{};
    GtkWidget *address{};
    GtkWidget *reload_stop{};
    GtkWidget *spinner{};
    GtkWidget *progress{};
    GtkWidget *tab_box{};
    GtkWidget *stack{};
    WebKitWebView *view{};
    std::vector<std::unique_ptr<TabState>> tabs;
    vantage::NavigationPolicy policy;
    bool smoke{};
};

void sync_active_chrome(WindowState *state);
TabState *new_tab(WindowState *state, const std::string &uri);

TabState *find_tab(WindowState *state, WebKitWebView *view) {
    const auto found = std::find_if(state->tabs.begin(), state->tabs.end(),
        [view](const auto &tab) { return tab->view == view; });
    return found == state->tabs.end() ? nullptr : found->get();
}

void load_decision(TabState *tab, const vantage::NavigationDecision &decision) {
    if (decision.kind == vantage::NavigationKind::web) {
        tab->internal_uri.clear();
        webkit_web_view_load_uri(tab->view, decision.uri.c_str());
    } else if (decision.kind == vantage::NavigationKind::internal) {
        tab->internal_uri = decision.uri;
        webkit_web_view_load_html(tab->view,
            "<!doctype html><meta charset=utf-8><title>Vantage Browser</title><style>html{color-scheme:dark}body{margin:0;background:#11100f;color:#f1ede3;font:18px system-ui;display:grid;place-items:center;height:100vh}main{text-align:center}b{color:#ff725e;font-size:42px}</style><main><b>Vantage</b><p>A clearer point of view on the web.</p></main>",
            nullptr);
        if (tab->window->view == tab->view)
            gtk_editable_set_text(GTK_EDITABLE(tab->window->address), decision.uri.c_str());
    }
}

void submit_address(GtkEntry *, WindowState *state) {
    const char *text = gtk_editable_get_text(GTK_EDITABLE(state->address));
    if (auto *tab = find_tab(state, state->view))
        load_decision(tab, state->policy.resolve(text ? text : ""));
}

void go_back(GtkButton *, WindowState *state) {
    if (state->view && webkit_web_view_can_go_back(state->view)) webkit_web_view_go_back(state->view);
}

void go_forward(GtkButton *, WindowState *state) {
    if (state->view && webkit_web_view_can_go_forward(state->view)) webkit_web_view_go_forward(state->view);
}

void reload_or_stop(GtkButton *, WindowState *state) {
    if (!state->view) return;
    if (webkit_web_view_is_loading(state->view)) webkit_web_view_stop_loading(state->view);
    else webkit_web_view_reload(state->view);
}

void sync_active_chrome(WindowState *state) {
    if (!state->view) return;
    const bool loading = webkit_web_view_is_loading(state->view);
    gtk_button_set_icon_name(GTK_BUTTON(state->reload_stop),
        loading ? "process-stop-symbolic" : "view-refresh-symbolic");
    gtk_widget_set_tooltip_text(state->reload_stop, loading ? "Stop loading" : "Reload");
    gtk_widget_set_visible(state->spinner, loading);
    gtk_widget_set_visible(state->progress, loading);
    if (loading) gtk_spinner_start(GTK_SPINNER(state->spinner));
    else gtk_spinner_stop(GTK_SPINNER(state->spinner));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress),
        webkit_web_view_get_estimated_load_progress(state->view));

    if (auto *tab = find_tab(state, state->view)) {
        const char *uri = webkit_web_view_get_uri(state->view);
        const std::string shown = !tab->internal_uri.empty() ? tab->internal_uri : (uri ? uri : "");
        gtk_editable_set_text(GTK_EDITABLE(state->address), shown.c_str());
        const char *title = webkit_web_view_get_title(state->view);
        gtk_window_set_title(GTK_WINDOW(state->window), title && *title ? title : "Vantage Browser");
    }
}

void loading_changed(WebKitWebView *view, GParamSpec *, TabState *tab) {
    if (tab->window->view == view) sync_active_chrome(tab->window);
}

void progress_changed(WebKitWebView *view, GParamSpec *, TabState *tab) {
    if (tab->window->view == view)
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(tab->window->progress),
            webkit_web_view_get_estimated_load_progress(view));
}

void uri_changed(WebKitWebView *view, GParamSpec *, TabState *tab) {
    if (tab->window->view != view) return;
    const char *uri = webkit_web_view_get_uri(view);
    if (!tab->internal_uri.empty() && (!uri || std::string_view(uri) == "about:blank")) return;
    if (uri) {
        tab->internal_uri.clear();
        gtk_editable_set_text(GTK_EDITABLE(tab->window->address), uri);
    }
}

void title_changed(WebKitWebView *view, GParamSpec *, TabState *tab) {
    const char *title = webkit_web_view_get_title(view);
    gtk_label_set_text(GTK_LABEL(tab->label), title && *title ? title : "New tab");
    if (tab->window->view == view)
        gtk_window_set_title(GTK_WINDOW(tab->window->window), title && *title ? title : "Vantage Browser");
}

gboolean decide_policy(WebKitWebView *, WebKitPolicyDecision *decision,
                       WebKitPolicyDecisionType type, TabState *tab) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) return FALSE;
    auto *navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    auto *action = webkit_navigation_policy_decision_get_navigation_action(navigation);
    auto *request = webkit_navigation_action_get_request(action);
    const char *uri = webkit_uri_request_get_uri(request);
    const auto resolved = tab->window->policy.resolve(uri ? uri : "");
    if (resolved.kind == vantage::NavigationKind::web) return FALSE;
    webkit_policy_decision_ignore(decision);
    if (resolved.kind == vantage::NavigationKind::internal) load_decision(tab, resolved);
    return TRUE;
}

gboolean tls_failed(WebKitWebView *, const char *, GTlsCertificate *, GTlsCertificateFlags, TabState *) {
    return FALSE;
}

void select_tab(TabState *tab) {
    auto *state = tab->window;
    state->view = tab->view;
    gtk_stack_set_visible_child(GTK_STACK(state->stack), tab->page);
    for (const auto &candidate : state->tabs) {
        if (candidate.get() == tab) gtk_widget_add_css_class(candidate->tab, "active");
        else gtk_widget_remove_css_class(candidate->tab, "active");
    }
    sync_active_chrome(state);
}

void tab_selected(GtkButton *, TabState *tab) { select_tab(tab); }

void close_tab(TabState *tab) {
    auto *state = tab->window;
    const auto found = std::find_if(state->tabs.begin(), state->tabs.end(),
        [tab](const auto &candidate) { return candidate.get() == tab; });
    if (found == state->tabs.end()) return;
    const auto index = static_cast<std::size_t>(std::distance(state->tabs.begin(), found));
    const bool was_active = state->view == tab->view;
    gtk_box_remove(GTK_BOX(state->tab_box), tab->tab);
    gtk_stack_remove(GTK_STACK(state->stack), tab->page);
    state->tabs.erase(found);
    if (state->tabs.empty()) {
        new_tab(state, "vantage:new");
    } else if (was_active) {
        select_tab(state->tabs[std::min(index, state->tabs.size() - 1)].get());
    }
}

void tab_closed(GtkButton *, TabState *tab) { close_tab(tab); }
void add_tab(GtkButton *, WindowState *state) { new_tab(state, "vantage:new"); }

GtkWidget *icon_button(const char *icon, const char *tooltip) {
    auto *button = gtk_button_new_from_icon_name(icon);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_tooltip_text(button, tooltip);
    return button;
}

TabState *new_tab(WindowState *state, const std::string &uri) {
    auto owned = std::make_unique<TabState>();
    auto *tab = owned.get();
    tab->window = state;
    tab->view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    tab->page = GTK_WIDGET(tab->view);
    gtk_widget_set_vexpand(tab->page, TRUE);
    gtk_stack_add_child(GTK_STACK(state->stack), tab->page);

    tab->tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(tab->tab, "browser-tab");
    auto *select = gtk_button_new();
    gtk_widget_add_css_class(select, "tab-select");
    tab->label = gtk_label_new("New tab");
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 24);
    gtk_button_set_child(GTK_BUTTON(select), tab->label);
    auto *close = icon_button("window-close-symbolic", "Close tab");
    gtk_widget_add_css_class(close, "tab-close");
    gtk_box_append(GTK_BOX(tab->tab), select);
    gtk_box_append(GTK_BOX(tab->tab), close);
    gtk_box_append(GTK_BOX(state->tab_box), tab->tab);

    g_signal_connect(select, "clicked", G_CALLBACK(tab_selected), tab);
    g_signal_connect(close, "clicked", G_CALLBACK(tab_closed), tab);
    g_signal_connect(tab->view, "notify::uri", G_CALLBACK(uri_changed), tab);
    g_signal_connect(tab->view, "notify::title", G_CALLBACK(title_changed), tab);
    g_signal_connect(tab->view, "notify::is-loading", G_CALLBACK(loading_changed), tab);
    g_signal_connect(tab->view, "notify::estimated-load-progress", G_CALLBACK(progress_changed), tab);
    g_signal_connect(tab->view, "decide-policy", G_CALLBACK(decide_policy), tab);
    g_signal_connect(tab->view, "load-failed-with-tls-errors", G_CALLBACK(tls_failed), tab);

    state->tabs.push_back(std::move(owned));
    select_tab(tab);
    load_decision(tab, state->policy.resolve(uri));
    return tab;
}

gboolean finish_smoke(void *data) {
    auto *state = static_cast<WindowState *>(data);
    g_application_quit(G_APPLICATION(state->application));
    return G_SOURCE_REMOVE;
}

gboolean key_pressed(GtkEventControllerKey *, guint keyval, guint,
                     GdkModifierType modifiers, WindowState *state) {
    const bool control = (modifiers & GDK_CONTROL_MASK) != 0;
    const bool alternate = (modifiers & GDK_ALT_MASK) != 0;
    if (control && (keyval == GDK_KEY_l || keyval == GDK_KEY_L)) {
        gtk_widget_grab_focus(state->address);
        gtk_editable_select_region(GTK_EDITABLE(state->address), 0, -1);
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_t || keyval == GDK_KEY_T)) {
        new_tab(state, "vantage:new");
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        if (auto *tab = find_tab(state, state->view)) close_tab(tab);
        return TRUE;
    }
    if ((control && (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) || keyval == GDK_KEY_F5) {
        if (state->view) webkit_web_view_reload(state->view);
        return TRUE;
    }
    if (alternate && keyval == GDK_KEY_Left) {
        go_back(nullptr, state);
        return TRUE;
    }
    if (alternate && keyval == GDK_KEY_Right) {
        go_forward(nullptr, state);
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape && state->view && webkit_web_view_is_loading(state->view)) {
        webkit_web_view_stop_loading(state->view);
        return TRUE;
    }
    return FALSE;
}

void install_style(GtkWidget *window) {
    auto *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "window { background: #171716; color: #ece8df; }"
        "headerbar { min-height: 34px; padding: 0 6px; background: #242423; box-shadow: none; border-bottom: 1px solid #393936; }"
        ".tab-strip { margin-top: 3px; }"
        ".browser-tab { min-width: 150px; margin-right: 2px; border-radius: 8px 8px 0 0; background: #2d2d2b; }"
        ".browser-tab.active { background: #3a3936; }"
        ".browser-tab button { min-height: 28px; padding: 0 7px; border: 0; background: transparent; box-shadow: none; color: #d8d4cc; }"
        ".browser-tab .tab-select { min-width: 112px; }"
        ".browser-tab .tab-close { min-width: 20px; padding: 0 4px; }"
        ".browser-tab button:hover { background: #494741; }"
        ".new-tab { min-width: 28px; min-height: 28px; margin-left: 3px; }"
        ".navigation { background: #242423; border-bottom: 1px solid #393936; }"
        ".toolbar { padding: 6px 8px; }"
        ".toolbar button.flat, .new-tab.flat { min-width: 28px; min-height: 28px; padding: 2px; border: 0; border-radius: 7px; background: transparent; color: #d8d4cc; box-shadow: none; }"
        ".toolbar button.flat:hover, .new-tab.flat:hover { background: #3a3936; color: #fffaf0; }"
        ".toolbar button.flat:active, .new-tab.flat:active { background: #494741; }"
        ".toolbar entry { min-height: 30px; padding: 0 12px; border-radius: 8px; border: 1px solid #45433f; background: #191918; color: #f1ede3; box-shadow: none; }"
        ".toolbar entry:focus { border-color: #ff8a62; box-shadow: 0 0 0 1px #ff8a62; }"
        ".toolbar spinner { color: #ff8a62; margin: 0 2px; }"
        ".load-progress trough { min-height: 2px; background: transparent; border: 0; }"
        ".load-progress progress { min-height: 2px; background: #ff7657; border: 0; }"
    );
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(window),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void activate(GtkApplication *application, void *user_data) {
    auto *state = static_cast<WindowState *>(user_data);
    state->application = application;
    state->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(state->window), "Vantage Browser");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 1100, 760);

    auto *header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    auto *tab_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(tab_strip, "tab-strip");
    state->tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    auto *new_button = icon_button("list-add-symbolic", "New tab");
    gtk_widget_add_css_class(new_button, "new-tab");
    gtk_box_append(GTK_BOX(tab_strip), state->tab_box);
    gtk_box_append(GTK_BOX(tab_strip), new_button);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), tab_strip);
    gtk_window_set_titlebar(GTK_WINDOW(state->window), header);

    auto *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    auto *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "toolbar");
    auto *back = icon_button("go-previous-symbolic", "Back");
    auto *forward = icon_button("go-next-symbolic", "Forward");
    state->reload_stop = icon_button("view-refresh-symbolic", "Reload");
    state->address = gtk_entry_new();
    gtk_widget_set_hexpand(state->address, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->address), "Search or enter address");
    state->spinner = gtk_spinner_new();
    gtk_widget_set_visible(state->spinner, FALSE);
    gtk_box_append(GTK_BOX(toolbar), back);
    gtk_box_append(GTK_BOX(toolbar), forward);
    gtk_box_append(GTK_BOX(toolbar), state->reload_stop);
    gtk_box_append(GTK_BOX(toolbar), state->spinner);
    gtk_box_append(GTK_BOX(toolbar), state->address);

    state->progress = gtk_progress_bar_new();
    gtk_widget_add_css_class(state->progress, "load-progress");
    gtk_widget_set_visible(state->progress, FALSE);
    auto *navigation = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(navigation, "navigation");
    gtk_box_append(GTK_BOX(navigation), toolbar);
    gtk_box_append(GTK_BOX(navigation), state->progress);

    state->stack = gtk_stack_new();
    gtk_widget_set_vexpand(state->stack, TRUE);
    gtk_box_append(GTK_BOX(layout), navigation);
    gtk_box_append(GTK_BOX(layout), state->stack);
    gtk_window_set_child(GTK_WINDOW(state->window), layout);
    install_style(state->window);

    g_signal_connect(back, "clicked", G_CALLBACK(go_back), state);
    g_signal_connect(forward, "clicked", G_CALLBACK(go_forward), state);
    g_signal_connect(state->reload_stop, "clicked", G_CALLBACK(reload_or_stop), state);
    g_signal_connect(state->address, "activate", G_CALLBACK(submit_address), state);
    g_signal_connect(new_button, "clicked", G_CALLBACK(add_tab), state);
    auto *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), state);
    gtk_widget_add_controller(state->window, keys);

    const auto *initial = static_cast<const char *>(g_object_get_data(G_OBJECT(application), "initial-uri"));
    auto *tab = new_tab(state, initial ? initial : "vantage:new");
    gtk_window_present(GTK_WINDOW(state->window));
    if (state->smoke) {
        webkit_web_view_load_html(tab->view, "<!doctype html><title>Vant smoke</title><p>ok</p>", "https://smoke.invalid/");
        g_timeout_add(900, finish_smoke, state);
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
