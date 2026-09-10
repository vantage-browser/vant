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
    GtkWidget *body{};
    GtkWidget *backdrop{};
    GtkWidget *label{};
    GtkWidget *icon_stack{};
    GtkWidget *favicon{};
    GtkWidget *spinner{};
    std::string internal_uri;
    bool closing{};
};

struct WindowState {
    GtkApplication *application{};
    GtkWidget *window{};
    GtkWidget *address{};
    GtkWidget *reload_stop{};
    GtkWidget *reload_stack{};
    GtkWidget *reload_icon{};
    GtkWidget *stop_icon{};
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
        if (tab->window->view == tab->view) {
            const char *shown = decision.uri == "vantage:new" ? "" : decision.uri.c_str();
            gtk_editable_set_text(GTK_EDITABLE(tab->window->address), shown);
        }
    }
}

void submit_address(GtkEntry *, WindowState *state) {
    const char *text = gtk_editable_get_text(GTK_EDITABLE(state->address));
    if (auto *tab = find_tab(state, state->view)) {
        load_decision(tab, state->policy.resolve(text ? text : ""));
        gtk_widget_grab_focus(GTK_WIDGET(tab->view));
    }
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
    if (loading) {
        gtk_stack_set_visible_child(GTK_STACK(state->reload_stack), state->stop_icon);
        gtk_widget_add_css_class(state->reload_stop, "stop-loading");
    } else {
        gtk_stack_set_visible_child(GTK_STACK(state->reload_stack), state->reload_icon);
        gtk_widget_remove_css_class(state->reload_stop, "stop-loading");
    }
    gtk_widget_set_tooltip_text(state->reload_stop, loading ? "Stop loading" : "Reload");
    gtk_widget_set_visible(state->progress, loading);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress),
        webkit_web_view_get_estimated_load_progress(state->view));

    if (auto *tab = find_tab(state, state->view)) {
        const char *uri = webkit_web_view_get_uri(state->view);
        const std::string shown = tab->internal_uri == "vantage:new" ? "" :
            (!tab->internal_uri.empty() ? tab->internal_uri : (uri ? uri : ""));
        if (!gtk_widget_has_focus(state->address))
            gtk_editable_set_text(GTK_EDITABLE(state->address), shown.c_str());
        const char *title = webkit_web_view_get_title(state->view);
        gtk_window_set_title(GTK_WINDOW(state->window), title && *title ? title : "Vantage Browser");
    }
}

void sync_tab_activity(TabState *tab) {
    const bool loading = webkit_web_view_is_loading(tab->view);
    if (loading) {
        gtk_spinner_start(GTK_SPINNER(tab->spinner));
        gtk_stack_set_visible_child(GTK_STACK(tab->icon_stack), tab->spinner);
    } else {
        gtk_spinner_stop(GTK_SPINNER(tab->spinner));
        gtk_stack_set_visible_child(GTK_STACK(tab->icon_stack), tab->favicon);
    }
}

void loading_changed(WebKitWebView *view, GParamSpec *, TabState *tab) {
    sync_tab_activity(tab);
    if (tab->window->view == view) sync_active_chrome(tab->window);
}

void favicon_changed(WebKitWebView *, GParamSpec *, TabState *tab) {
    if (auto *favicon = webkit_web_view_get_favicon(tab->view))
        gtk_image_set_from_paintable(GTK_IMAGE(tab->favicon), GDK_PAINTABLE(favicon));
    sync_tab_activity(tab);
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
        if (!gtk_widget_has_focus(tab->window->address))
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
    if (!tab->internal_uri.empty() && uri && std::string_view(uri) == "about:blank") return FALSE;
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
        if (candidate.get() == tab) {
            gtk_widget_add_css_class(candidate->body, "active");
            gtk_widget_remove_css_class(candidate->body, "inactive");
        } else {
            gtk_widget_remove_css_class(candidate->body, "active");
            gtk_widget_add_css_class(candidate->body, "inactive");
        }
        gtk_widget_queue_draw(candidate->backdrop);
    }
    sync_active_chrome(state);
}

void tab_selected(GtkButton *, TabState *tab) { select_tab(tab); }

void draw_tab_backdrop(GtkDrawingArea *, cairo_t *cr, int width, int height, void *data) {
    auto *tab = static_cast<TabState *>(data);
    if (tab->window->view != tab->view) return;

    const double edge = 9.0;
    const double top = 6.5;
    const double radius = 8.0;
    cairo_new_path(cr);
    cairo_move_to(cr, 0, height);
    cairo_curve_to(cr, edge * 0.55, height, edge, height - edge * 0.45, edge, height - edge);
    cairo_line_to(cr, edge, top + radius);
    cairo_curve_to(cr, edge, top + 3, edge + 3, top, edge + radius, top);
    cairo_line_to(cr, width - edge - radius, top);
    cairo_curve_to(cr, width - edge - 3, top, width - edge, top + 3, width - edge, top + radius);
    cairo_line_to(cr, width - edge, height - edge);
    cairo_curve_to(cr, width - edge, height - edge * 0.45, width - edge * 0.55, height, width, height);
    cairo_line_to(cr, 0, height);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 0x2c / 255.0, 0x2c / 255.0, 0x2c / 255.0);
    cairo_fill(cr);

    cairo_new_path(cr);
    cairo_move_to(cr, 0.5, height - 0.5);
    cairo_curve_to(cr, edge * 0.55, height - 0.5, edge + 0.5, height - edge * 0.45, edge + 0.5, height - edge);
    cairo_line_to(cr, edge + 0.5, top + radius);
    cairo_curve_to(cr, edge + 0.5, top + 3, edge + 3, top + 0.5, edge + radius, top + 0.5);
    cairo_line_to(cr, width - edge - radius, top + 0.5);
    cairo_curve_to(cr, width - edge - 3, top + 0.5, width - edge - 0.5, top + 3, width - edge - 0.5, top + radius);
    cairo_line_to(cr, width - edge - 0.5, height - edge);
    cairo_curve_to(cr, width - edge - 0.5, height - edge * 0.45, width - edge * 0.55, height - 0.5, width - 0.5, height - 0.5);
    cairo_set_source_rgb(cr, 0x39 / 255.0, 0x39 / 255.0, 0x36 / 255.0);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
}

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

gboolean close_tab_deferred(void *data) {
    close_tab(static_cast<TabState *>(data));
    return G_SOURCE_REMOVE;
}

void queue_tab_close(TabState *tab) {
    if (tab->closing) return;
    tab->closing = true;
    g_idle_add(close_tab_deferred, tab);
}
void tab_closed(GtkButton *, TabState *tab) { queue_tab_close(tab); }

void header_middle_pressed(GtkGestureClick *gesture, int, double x, double y, WindowState *state) {
    auto *header = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    for (const auto &tab : state->tabs) {
        graphene_rect_t bounds;
        if (!gtk_widget_compute_bounds(tab->tab, header, &bounds)) continue;
        if (x >= bounds.origin.x && x <= bounds.origin.x + bounds.size.width &&
            y >= bounds.origin.y && y <= bounds.origin.y + bounds.size.height) {
            queue_tab_close(tab.get());
            return;
        }
    }
}
void add_tab(GtkButton *, WindowState *state) { new_tab(state, "vantage:new"); }

GtkWidget *icon_button(const char *icon, const char *tooltip) {
    auto *button = gtk_button_new_from_icon_name(icon);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_tooltip_text(button, tooltip);
    return button;
}

enum class ToolbarIcon { back, forward, reload };

void draw_toolbar_icon(GtkDrawingArea *, cairo_t *cr, int width, int height, void *data) {
    const auto icon = static_cast<ToolbarIcon>(GPOINTER_TO_INT(data));
    const double cx = width / 2.0;
    const double cy = height / 2.0;
    cairo_set_source_rgb(cr, 0xd8 / 255.0, 0xd4 / 255.0, 0xcc / 255.0);
    cairo_set_line_width(cr, 1.8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    if (icon == ToolbarIcon::back || icon == ToolbarIcon::forward) {
        const double direction = icon == ToolbarIcon::back ? 1.0 : -1.0;
        const double tip = cx - direction * 5.0;
        const double tail = cx + direction * 5.0;
        cairo_move_to(cr, cx, cy - 5.0);
        cairo_line_to(cr, tip, cy);
        cairo_line_to(cr, cx, cy + 5.0);
        cairo_move_to(cr, tip, cy);
        cairo_line_to(cr, tail, cy);
        cairo_stroke(cr);
        return;
    }
    cairo_arc(cr, cx, cy, 6.0, -0.65, 4.65);
    cairo_stroke(cr);
    cairo_move_to(cr, cx - 1.0, cy - 6.1);
    cairo_line_to(cr, cx - 5.2, cy - 6.0);
    cairo_line_to(cr, cx - 4.2, cy - 2.0);
    cairo_stroke(cr);
}

GtkWidget *drawn_icon(ToolbarIcon icon) {
    auto *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 20, 20);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_toolbar_icon,
        GINT_TO_POINTER(static_cast<int>(icon)), nullptr);
    return area;
}

GtkWidget *drawn_icon_button(ToolbarIcon icon, const char *tooltip) {
    auto *button = gtk_button_new();
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_button_set_child(GTK_BUTTON(button), drawn_icon(icon));
    return button;
}

gboolean focus_address_deferred(void *data) {
    auto *state = static_cast<WindowState *>(data);
    gtk_editable_set_text(GTK_EDITABLE(state->address), "");
    gtk_widget_grab_focus(state->address);
    return G_SOURCE_REMOVE;
}

TabState *new_tab(WindowState *state, const std::string &uri) {
    auto owned = std::make_unique<TabState>();
    auto *tab = owned.get();
    tab->window = state;
    tab->view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    tab->page = GTK_WIDGET(tab->view);
    gtk_widget_set_vexpand(tab->page, TRUE);
    gtk_stack_add_child(GTK_STACK(state->stack), tab->page);

    tab->tab = gtk_overlay_new();
    gtk_widget_add_css_class(tab->tab, "browser-tab");
    gtk_widget_set_size_request(tab->tab, 168, 38);
    tab->backdrop = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(tab->backdrop), draw_tab_backdrop, tab, nullptr);
    gtk_overlay_set_child(GTK_OVERLAY(tab->tab), tab->backdrop);
    tab->body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(tab->body, "browser-tab-body");
    gtk_widget_set_margin_start(tab->body, 9);
    gtk_widget_set_margin_end(tab->body, 9);
    gtk_widget_set_margin_top(tab->body, 7);
    auto *hover_surface = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(hover_surface, "tab-hover-surface");
    auto *select = gtk_button_new();
    gtk_widget_add_css_class(select, "tab-select");
    auto *tab_content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    tab->icon_stack = gtk_stack_new();
    gtk_widget_set_size_request(tab->icon_stack, 18, 18);
    tab->favicon = gtk_image_new_from_icon_name("web-browser-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(tab->favicon), 18);
    tab->spinner = gtk_spinner_new();
    gtk_widget_set_size_request(tab->spinner, 18, 18);
    gtk_stack_add_child(GTK_STACK(tab->icon_stack), tab->favicon);
    gtk_stack_add_child(GTK_STACK(tab->icon_stack), tab->spinner);
    tab->label = gtk_label_new("New tab");
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 24);
    gtk_box_append(GTK_BOX(tab_content), tab->icon_stack);
    gtk_box_append(GTK_BOX(tab_content), tab->label);
    gtk_button_set_child(GTK_BUTTON(select), tab_content);
    auto *close = icon_button("window-close-symbolic", "Close tab");
    gtk_widget_add_css_class(close, "tab-close");
    gtk_box_append(GTK_BOX(hover_surface), select);
    gtk_box_append(GTK_BOX(hover_surface), close);
    gtk_box_append(GTK_BOX(tab->body), hover_surface);
    gtk_overlay_add_overlay(GTK_OVERLAY(tab->tab), tab->body);
    gtk_box_append(GTK_BOX(state->tab_box), tab->tab);

    g_signal_connect(select, "clicked", G_CALLBACK(tab_selected), tab);
    g_signal_connect(close, "clicked", G_CALLBACK(tab_closed), tab);
    g_signal_connect(tab->view, "notify::uri", G_CALLBACK(uri_changed), tab);
    g_signal_connect(tab->view, "notify::title", G_CALLBACK(title_changed), tab);
    g_signal_connect(tab->view, "notify::is-loading", G_CALLBACK(loading_changed), tab);
    g_signal_connect(tab->view, "notify::estimated-load-progress", G_CALLBACK(progress_changed), tab);
    g_signal_connect(tab->view, "notify::favicon", G_CALLBACK(favicon_changed), tab);
    g_signal_connect(tab->view, "decide-policy", G_CALLBACK(decide_policy), tab);
    g_signal_connect(tab->view, "load-failed-with-tls-errors", G_CALLBACK(tls_failed), tab);

    state->tabs.push_back(std::move(owned));
    select_tab(tab);
    auto *session = webkit_web_view_get_network_session(tab->view);
    auto *data_manager = webkit_network_session_get_website_data_manager(session);
    webkit_website_data_manager_set_favicons_enabled(data_manager, TRUE);
    load_decision(tab, state->policy.resolve(uri));
    if (uri == "vantage:new" || uri.starts_with("about:")) g_idle_add(focus_address_deferred, state);
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
        ".tab-strip { margin-top: 2px; }"
        ".browser-tab { min-width: 150px; margin-right: 0; background: transparent; }"
        ".browser-tab-body { background: transparent; }"
        ".tab-hover-surface { margin: 4px 3px; border-radius: 7px; background: transparent; }"
        ".browser-tab-body.inactive .tab-hover-surface:hover { background: #353432; }"
        ".browser-tab button { min-height: 22px; padding: 0 7px; border: 0; background: transparent; box-shadow: none; color: #d8d4cc; }"
        ".browser-tab .tab-select { min-width: 112px; }"
        ".browser-tab .tab-close { min-width: 20px; padding: 0 4px; opacity: 0; }"
        ".browser-tab:hover .tab-close, .browser-tab-body.active .tab-close { opacity: 1; }"
        ".browser-tab button:hover { background: transparent; }"
        ".browser-tab .tab-close:hover { background: transparent; color: #ff7657; }"
        ".new-tab { min-width: 28px; min-height: 28px; margin-left: 3px; }"
        ".navigation { background: #2c2c2c; border-bottom: 1px solid #393936; }"
        ".toolbar { padding: 6px 8px; background: #2c2c2c; }"
        ".toolbar button.flat, .new-tab.flat { min-width: 28px; min-height: 28px; padding: 2px; border: 0; border-radius: 7px; background: transparent; color: #d8d4cc; box-shadow: none; }"
        ".toolbar button.flat:hover, .new-tab.flat:hover { background: #3a3936; color: #fffaf0; }"
        ".toolbar button.flat:active, .new-tab.flat:active { background: #494741; }"
        ".toolbar button.stop-loading { font-size: 27px; font-weight: 400; }"
        ".toolbar entry { min-height: 30px; padding: 0 12px; border-radius: 8px; border: 1px solid #45433f; background: #191918; color: #f1ede3; box-shadow: none; }"
        ".toolbar entry:focus { border-color: #ff8a62; box-shadow: 0 0 0 1px #ff8a62; }"
        ".browser-tab spinner { color: #ff8a62; }"
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
    gtk_widget_set_hexpand(tab_strip, TRUE);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), tab_strip);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), gtk_label_new(nullptr));
    auto *header_middle = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(header_middle), GDK_BUTTON_MIDDLE);
    gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(header_middle), TRUE);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(header_middle), GTK_PHASE_CAPTURE);
    g_signal_connect(header_middle, "pressed", G_CALLBACK(header_middle_pressed), state);
    gtk_widget_add_controller(header, GTK_EVENT_CONTROLLER(header_middle));
    gtk_window_set_titlebar(GTK_WINDOW(state->window), header);

    auto *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    auto *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "toolbar");
    auto *back = drawn_icon_button(ToolbarIcon::back, "Back");
    auto *forward = drawn_icon_button(ToolbarIcon::forward, "Forward");
    state->reload_stop = gtk_button_new();
    gtk_widget_add_css_class(state->reload_stop, "flat");
    gtk_widget_set_tooltip_text(state->reload_stop, "Reload");
    state->reload_stack = gtk_stack_new();
    state->reload_icon = drawn_icon(ToolbarIcon::reload);
    state->stop_icon = gtk_label_new("×");
    gtk_stack_add_child(GTK_STACK(state->reload_stack), state->reload_icon);
    gtk_stack_add_child(GTK_STACK(state->reload_stack), state->stop_icon);
    gtk_stack_set_visible_child(GTK_STACK(state->reload_stack), state->reload_icon);
    gtk_button_set_child(GTK_BUTTON(state->reload_stop), state->reload_stack);
    state->address = gtk_entry_new();
    gtk_widget_set_hexpand(state->address, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->address), "Search or enter address");
    gtk_box_append(GTK_BOX(toolbar), back);
    gtk_box_append(GTK_BOX(toolbar), forward);
    gtk_box_append(GTK_BOX(toolbar), state->reload_stop);
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
    if (!initial || std::string_view(initial).starts_with("vantage:") ||
        std::string_view(initial).starts_with("about:")) {
        gtk_widget_grab_focus(state->address);
    }
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
