#include "native_app.h"

#include "navigation.h"
#include "user_data.h"

#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include <webkit/webkit.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct WindowState;
struct ApplicationState;
struct DownloadContext;

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
    std::string display_uri;
    bool closing{};
    bool hovered{};
    bool user_stopped{};
};

struct WindowState {
    ApplicationState *owner{};
    GtkApplication *application{};
    GtkWidget *window{};
    GtkWidget *address{};
    GtkWidget *address_popover{};
    GtkWidget *address_suggestions{};
    GtkWidget *reload_stop{};
    GtkWidget *reload_stack{};
    GtkWidget *reload_icon{};
    GtkWidget *stop_icon{};
    GtkWidget *progress{};
    GtkWidget *find_bar{};
    GtkWidget *find_entry{};
    GtkWidget *find_count{};
    GtkWidget *bookmark_button{};
    GtkWidget *menu_button{};
    GtkWidget *downloads_button{};
    GtkWidget *downloads_popover{};
    GtkWidget *downloads_box{};
    GtkWidget *downloads_stack{};
    GtkWidget *downloads_icon{};
    GtkWidget *downloads_spinner{};
    GtkWidget *tab_box{};
    GtkWidget *stack{};
    GtkWidget *new_tab_backdrop{};
    WebKitWebView *view{};
    WebKitNetworkSession *private_session{};
    TabState *middle_pressed_tab{};
    std::vector<std::unique_ptr<TabState>> tabs;
    vantage::NavigationPolicy policy;
    bool smoke{};
    bool private_mode{};
    bool closed{};
    bool new_tab_hovered{};
    double progress_fraction{};
    unsigned find_current{};
    unsigned find_total{};
    unsigned find_generation{};
    gint64 address_focused_at{};
    bool custom_find{};
    ~WindowState() { if (private_session) g_object_unref(private_session); }
};

struct ApplicationState {
    GtkApplication *application{};
    std::string initial_uri;
    bool smoke{};
    std::unique_ptr<vantage::UserDataStore> data;
    std::vector<DownloadContext *> active_downloads;
    std::vector<std::unique_ptr<WindowState>> windows;
};

void sync_active_chrome(WindowState *state);
TabState *find_tab(WindowState *state, WebKitWebView *view);
TabState *new_tab(WindowState *state, const std::string &uri);
void create_window(ApplicationState *owner, const std::string &initial_uri, bool smoke,
                   WindowState *source, bool private_mode = false);
std::string format_bytes(std::uint64_t bytes);
void cancel_download(ApplicationState *owner, std::int64_t id);
void address_changed(GtkEditable *, WindowState *state);
std::string html_escape(std::string_view value);
void print_page(GtkButton *, WindowState *state);

void update_find_count(WindowState *state) {
    const auto label = state->find_total ? std::to_string(state->find_current) + " / " +
        std::to_string(state->find_total) : "0 / 0";
    gtk_label_set_text(GTK_LABEL(state->find_count), label.c_str());
}

void find_counted(WebKitFindController *, guint count, WindowState *state) {
    if (state->custom_find) return;
    state->find_total = count;
    state->find_current = count ? 1 : 0;
    update_find_count(state);
}

std::string javascript_string(std::string_view value) {
    auto *escaped = g_strescape(std::string(value).c_str(), nullptr);
    std::string result = "\"" + std::string(escaped ? escaped : "") + "\"";
    g_free(escaped);
    return result;
}

struct FindEvaluation { WindowState *state{}; WebKitWebView *view{}; unsigned generation{}; };

void find_evaluated(GObject *source, GAsyncResult *result, void *data) {
    std::unique_ptr<FindEvaluation> evaluation(static_cast<FindEvaluation *>(data));
    GError *error = nullptr;
    auto *value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);
    if (evaluation->generation != evaluation->state->find_generation ||
        evaluation->state->view != evaluation->view) {
        if (value) g_object_unref(value);
        if (error) g_error_free(error);
        return;
    }
    const int count = value && jsc_value_is_number(value) ? jsc_value_to_int32(value) : -1;
    if (count >= 0) {
        evaluation->state->custom_find = true;
        evaluation->state->find_total = static_cast<unsigned>(count);
        evaluation->state->find_current = count ? 1U : 0U;
        update_find_count(evaluation->state);
    } else {
        evaluation->state->custom_find = false;
        const char *query = gtk_editable_get_text(GTK_EDITABLE(evaluation->state->find_entry));
        constexpr guint32 options = WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND;
        auto *controller = webkit_web_view_get_find_controller(evaluation->view);
        webkit_find_controller_search(controller, query, options, G_MAXUINT);
        webkit_find_controller_count_matches(controller, query, options, G_MAXUINT);
    }
    if (value) g_object_unref(value);
    if (error) g_error_free(error);
}

void find_changed(GtkEditable *entry, WindowState *state) {
    if (!state->view) return;
    auto *controller = webkit_web_view_get_find_controller(state->view);
    webkit_find_controller_search_finish(controller);
    const char *text = gtk_editable_get_text(entry);
    if (!text || !*text) {
        const auto generation = ++state->find_generation;
        state->custom_find = false;
        const auto clear_script = "window.__vantageFindGeneration=" + std::to_string(generation) +
            ";document.querySelectorAll('[data-vantage-find]').forEach(m=>m.replaceWith(document.createTextNode(m.textContent)));"
            "document.body?.normalize();delete window.__vantageFind";
        webkit_web_view_evaluate_javascript(state->view, clear_script.c_str(),
            -1, nullptr, nullptr, nullptr, nullptr, nullptr);
        state->find_current = state->find_total = 0;
        update_find_count(state);
        return;
    }
    state->custom_find = false;
    const auto query = javascript_string(text);
    const auto generation = ++state->find_generation;
    const std::string script = "(()=>{const generation=" + std::to_string(generation) +
        ";if((window.__vantageFindGeneration||0)>generation)return -2;window.__vantageFindGeneration=generation;"
        "document.querySelectorAll('[data-vantage-find]').forEach(m=>m.replaceWith(document.createTextNode(m.textContent)));"
        "document.body?.normalize();const q=" + query + ",needle=q.toLocaleLowerCase(),nodes=[],matches=[];"
        "const w=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT,{acceptNode(n){"
        "const p=n.parentElement;if(!p||p.closest('[contenteditable=true]')||/^(SCRIPT|STYLE|NOSCRIPT|TEXTAREA|INPUT)$/.test(p.tagName))return NodeFilter.FILTER_REJECT;"
        "return n.data.toLocaleLowerCase().includes(needle)?NodeFilter.FILTER_ACCEPT:NodeFilter.FILTER_REJECT}});"
        "for(let n;n=w.nextNode();)nodes.push(n);if(window.__vantageFindGeneration!==generation)return -2;"
        "for(const n of nodes){const original=n.data,lower=original.toLocaleLowerCase(),fragment=document.createDocumentFragment();let from=0,at;"
        "while((at=lower.indexOf(needle,from))>=0){fragment.append(original.slice(from,at));const mark=document.createElement('span');"
        "mark.dataset.vantageFind='';mark.className='vantage-find-match';mark.textContent=original.slice(at,at+needle.length);"
        "fragment.append(mark);matches.push(mark);from=at+needle.length}fragment.append(original.slice(from));n.replaceWith(fragment)}"
        "window.__vantageFind={matches,index:0};if(matches.length){matches[0].classList.add('vantage-find-current');"
        "matches[0].scrollIntoView({block:'center'})}return matches.length})()";
    auto *evaluation = new FindEvaluation{state, state->view, generation};
    webkit_web_view_evaluate_javascript(state->view, script.c_str(), -1, nullptr, nullptr, nullptr,
        find_evaluated, evaluation);
}

void move_custom_find(WindowState *state, int direction) {
    const std::string script = "(()=>{const f=window.__vantageFind;if(!f?.matches.length)return;"
        "f.matches[f.index].classList.remove('vantage-find-current');"
        "f.index=(f.index+" + std::to_string(direction) + "+f.matches.length)%f.matches.length;"
        "f.matches[f.index].classList.add('vantage-find-current');f.matches[f.index].scrollIntoView({block:'center'})})()";
    webkit_web_view_evaluate_javascript(state->view, script.c_str(), -1, nullptr, nullptr, nullptr,
        nullptr, nullptr);
}

void find_next(GtkButton *, WindowState *state) {
    if (!state->view || !state->find_total) return;
    if (state->custom_find) move_custom_find(state, 1);
    else webkit_find_controller_search_next(webkit_web_view_get_find_controller(state->view));
    state->find_current = state->find_current % state->find_total + 1;
    update_find_count(state);
}

void find_activate(GtkEntry *, WindowState *state) { find_next(nullptr, state); }

void find_previous(GtkButton *, WindowState *state) {
    if (!state->view || !state->find_total) return;
    if (state->custom_find) move_custom_find(state, -1);
    else webkit_find_controller_search_previous(webkit_web_view_get_find_controller(state->view));
    state->find_current = state->find_current <= 1 ? state->find_total : state->find_current - 1;
    update_find_count(state);
}

void close_find(GtkButton *, WindowState *state) {
    if (state->view) webkit_find_controller_search_finish(webkit_web_view_get_find_controller(state->view));
    if (state->view) {
        const auto generation = ++state->find_generation;
        const auto script = "window.__vantageFindGeneration=" + std::to_string(generation) +
            ";document.querySelectorAll('[data-vantage-find]').forEach(m=>m.replaceWith(document.createTextNode(m.textContent)));"
            "document.body?.normalize();delete window.__vantageFind";
        webkit_web_view_evaluate_javascript(state->view, script.c_str(), -1, nullptr, nullptr,
            nullptr, nullptr, nullptr);
    }
    gtk_widget_set_visible(state->find_bar, FALSE);
    if (state->view) gtk_widget_grab_focus(GTK_WIDGET(state->view));
}

void show_find(WindowState *state) {
    gtk_widget_set_visible(state->find_bar, TRUE);
    gtk_widget_grab_focus(state->find_entry);
    gtk_editable_select_region(GTK_EDITABLE(state->find_entry), 0, -1);
}

enum class ContextOpen { tab, window };
struct ContextOpenData { WindowState *window{}; std::string uri; ContextOpen mode{}; bool image{}; };
struct ImageAction { WindowState *window{}; std::string uri; bool save{}; };

GBytes *decode_data_uri(const std::string &uri) {
    const auto comma = uri.find(',');
    if (comma == std::string::npos) return nullptr;
    const auto metadata = std::string_view(uri).substr(0, comma);
    if (metadata.ends_with(";base64")) {
        gsize size = 0;
        auto *decoded = g_base64_decode(uri.c_str() + comma + 1, &size);
        return g_bytes_new_take(decoded, size);
    }
    auto *decoded = g_uri_unescape_string(uri.c_str() + comma + 1, nullptr);
    return decoded ? g_bytes_new_take(decoded, std::strlen(decoded)) : nullptr;
}

struct SaveImageData { GtkFileDialog *dialog{}; GBytes *bytes{}; };

void image_save_chosen(GObject *source, GAsyncResult *result, void *data) {
    auto *save = static_cast<SaveImageData *>(data);
    GError *error = nullptr;
    auto *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);
    if (file) {
        gsize size = 0;
        const auto *bytes = static_cast<const char *>(g_bytes_get_data(save->bytes, &size));
        g_file_replace_contents(file, bytes, size, nullptr, FALSE, G_FILE_CREATE_REPLACE_DESTINATION,
            nullptr, nullptr, &error);
        g_object_unref(file);
    }
    if (error) g_error_free(error);
    g_bytes_unref(save->bytes);
    g_object_unref(save->dialog);
    delete save;
}

void use_image_bytes(WindowState *window, const std::string &uri, bool save_image, GBytes *bytes) {
    if (!bytes) return;
    if (!save_image) {
        GError *error = nullptr;
        auto *texture = gdk_texture_new_from_bytes(bytes, &error);
        if (texture) {
            gdk_clipboard_set_texture(gtk_widget_get_clipboard(window->window), texture);
            g_object_unref(texture);
        } else {
            const auto semicolon = uri.find(';');
            const std::string mime = uri.starts_with("data:") && semicolon != std::string::npos
                ? uri.substr(5, semicolon - 5) : "image/png";
            auto *provider = gdk_content_provider_new_for_bytes(mime.c_str(), bytes);
            gdk_clipboard_set_content(gtk_widget_get_clipboard(window->window), provider);
            g_object_unref(provider);
        }
        if (error) g_error_free(error);
        g_bytes_unref(bytes);
        return;
    }
    auto *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save image as");
    const auto semicolon = uri.find(';');
    const auto mime = semicolon == std::string::npos ? std::string{} : uri.substr(11, semicolon - 11);
    const std::string extension = mime == "png" ? ".png" : mime == "webp" ? ".webp" :
        mime == "jpeg" ? ".jpg" : mime == "svg+xml" ? ".svg" : ".img";
    std::string name = "image" + extension;
    if (!uri.starts_with("data:")) {
        auto source = uri.substr(0, uri.find_first_of("?#"));
        const auto slash = source.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < source.size()) name = source.substr(slash + 1);
    }
    gtk_file_dialog_set_initial_name(dialog, name.c_str());
    auto *save = new SaveImageData{GTK_FILE_DIALOG(g_object_ref(dialog)), bytes};
    gtk_file_dialog_save(dialog, GTK_WINDOW(window->window), nullptr, image_save_chosen, save);
    g_object_unref(dialog);
}

struct ImageFetch { WindowState *window{}; std::string uri; bool save{}; SoupSession *session{}; };

void image_fetched(GObject *source, GAsyncResult *result, void *data) {
    auto *fetch = static_cast<ImageFetch *>(data);
    GError *error = nullptr;
    auto *bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    if (bytes) use_image_bytes(fetch->window, fetch->uri, fetch->save, bytes);
    if (error) g_error_free(error);
    g_object_unref(fetch->session);
    delete fetch;
}

void image_action(GSimpleAction *, GVariant *, void *data) {
    const auto *action = static_cast<const ImageAction *>(data);
    if (action->uri.starts_with("data:image/")) {
        auto *bytes = decode_data_uri(action->uri);
        use_image_bytes(action->window, action->uri, action->save, bytes);
        return;
    }
    auto *message = soup_message_new("GET", action->uri.c_str());
    if (!message) return;
    const char *page = action->window->view ? webkit_web_view_get_uri(action->window->view) : nullptr;
    if (page) soup_message_headers_replace(soup_message_get_request_headers(message), "Referer", page);
    auto *session = soup_session_new();
    auto *fetch = new ImageFetch{action->window, action->uri, action->save,
        SOUP_SESSION(g_object_ref(session))};
    soup_session_send_and_read_async(session, message, G_PRIORITY_DEFAULT, nullptr, image_fetched, fetch);
    g_object_unref(message);
    g_object_unref(session);
}

void append_image_data_action(WebKitContextMenu *menu, WindowState *window, const char *uri,
                              const char *label, bool save) {
    auto *action = g_simple_action_new(save ? "save-data-image" : "copy-data-image", nullptr);
    auto *data = new ImageAction{window, uri ? uri : "", save};
    g_signal_connect_data(action, "activate", G_CALLBACK(image_action), data,
        [](void *value, GClosure *) { delete static_cast<ImageAction *>(value); }, G_CONNECT_DEFAULT);
    webkit_context_menu_append(menu, webkit_context_menu_item_new_from_gaction(G_ACTION(action), label, nullptr));
    g_object_unref(action);
}

void context_open(GSimpleAction *, GVariant *, void *data) {
    const auto *open = static_cast<const ContextOpenData *>(data);
    if (open->image) {
        WindowState *target = open->window;
        TabState *tab = nullptr;
        if (open->mode == ContextOpen::window) {
            create_window(open->window->owner, "vantage:new", false, open->window);
            target = open->window->owner->windows.back().get();
            tab = find_tab(target, target->view);
        } else tab = new_tab(target, "vantage:new");
        if (tab) {
            tab->internal_uri = "vantage:image";
            tab->display_uri = open->uri;
            const auto page = "<!doctype html><html><head><meta charset=utf-8><title>Image</title><style>"
                "html{color-scheme:dark}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#20201f}"
                "img{display:block;max-width:100%;max-height:100vh;object-fit:contain}</style></head><body><img src='" +
                html_escape(open->uri) + "' alt=''></body></html>";
            webkit_web_view_load_html(tab->view, page.c_str(), nullptr);
        }
    } else if (open->mode == ContextOpen::tab) new_tab(open->window, open->uri);
    else create_window(open->window->owner, open->uri, false, open->window);
}

enum class PageActionKind { save, print, source };
struct PageActionData { TabState *tab{}; PageActionKind kind{}; };
struct PageSaveData { GtkFileDialog *dialog{}; WebKitWebView *view{}; };

void page_save_finished(GObject *source, GAsyncResult *result, void *data) {
    auto *save = static_cast<PageSaveData *>(data);
    GError *error = nullptr;
    auto *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);
    if (file) {
        webkit_web_view_save_to_file(save->view, file, WEBKIT_SAVE_MODE_MHTML, nullptr, nullptr, nullptr);
        g_object_unref(file);
    }
    if (error) g_error_free(error);
    g_object_unref(save->view);
    g_object_unref(save->dialog);
    delete save;
}

struct SourceData { WindowState *window{}; WebKitWebView *view{}; std::string uri; };

void source_loaded(GObject *source, GAsyncResult *result, void *data) {
    auto *request = static_cast<SourceData *>(data);
    GError *error = nullptr;
    gsize size = 0;
    auto *bytes = webkit_web_resource_get_data_finish(WEBKIT_WEB_RESOURCE(source), result, &size, &error);
    if (bytes && !request->window->closed) {
        auto *encoded = g_base64_encode(bytes, size);
        const auto base = javascript_string(request->uri);
        const std::string page = "<!doctype html><html><head><meta charset=utf-8><title>Source</title><style>"
            "html{color-scheme:dark}body{margin:0;background:#191918;color:#ddd8ce;font:13px/1.55 'JetBrains Mono','DejaVu Sans Mono',monospace}"
            "header{position:sticky;top:0;padding:12px 18px;background:#252423;border-bottom:1px solid #403e3a;color:#ffb07c}"
            "pre{margin:0;padding:18px;white-space:pre-wrap;overflow-wrap:anywhere}.comment{color:#85827b}.tag{color:#ff8a62}"
            ".attr{color:#ffd37a}.string,a{color:#9fd7ff}a:hover{color:#ff8a62}</style></head><body><header>Source: " +
            html_escape(request->uri) + "</header><pre id=s></pre><script>const raw=new TextDecoder().decode(Uint8Array.from(atob('" + encoded + "'),c=>c.charCodeAt(0))),base=" + base + ";"
            "const esc=s=>s.replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));"
            "function tag(t){let out='',p=0,re=/([\\w:-]+)(\\s*=\\s*)([\"'])(.*?)\\3/g,m;while((m=re.exec(t))){out+=esc(t.slice(p,m.index));"
            "let n='<span class=\"attr\">'+esc(m[1])+'</span>'+esc(m[2]),v=esc(m[4]);"
            "if(/^(src|href|action|poster|data-src)$/i.test(m[1])){try{let u=new URL(m[4],base).href;out+=n+'<a target=\"_blank\" href=\"'+esc(u).replace(/\"/g,'&quot;')+'\">'+esc(m[3])+v+esc(m[3])+'</a>'}catch{out+=n+'<span class=\"string\">'+esc(m[3])+v+esc(m[3])+'</span>'}}"
            "else out+=n+'<span class=\"string\">'+esc(m[3])+v+esc(m[3])+'</span>';p=re.lastIndex}out+=esc(t.slice(p));return out.replace(/(&lt;\\/?)([\\w:-]+)/,'$1<span class=\"tag\">$2</span>')}"
            "document.getElementById('s').innerHTML=raw.split(/(<!--[\\s\\S]*?-->|<[^>]+>)/g).map(x=>x.startsWith('<!--')?'<span class=comment>'+esc(x)+'</span>':x.startsWith('<')?tag(x):esc(x)).join('');"
            "</script></body></html>";
        auto *tab = new_tab(request->window, "vantage:new");
        tab->internal_uri = "vantage:source";
        tab->display_uri = "view-source:" + request->uri;
        webkit_web_view_load_html(tab->view, page.c_str(), request->uri.c_str());
        g_free(encoded);
        g_free(bytes);
    } else if (bytes) g_free(bytes);
    if (error) g_error_free(error);
    g_object_unref(request->view);
    delete request;
}

void page_action(GSimpleAction *, GVariant *, void *data) {
    const auto *action = static_cast<const PageActionData *>(data);
    if (!action->tab || action->tab->closing) return;
    if (action->kind == PageActionKind::print) {
        print_page(nullptr, action->tab->window);
    } else if (action->kind == PageActionKind::save) {
        auto *dialog = gtk_file_dialog_new();
        gtk_file_dialog_set_title(dialog, "Save page as");
        gtk_file_dialog_set_initial_name(dialog, "page.mhtml");
        auto *save = new PageSaveData{GTK_FILE_DIALOG(g_object_ref(dialog)),
            WEBKIT_WEB_VIEW(g_object_ref(action->tab->view))};
        gtk_file_dialog_save(dialog, GTK_WINDOW(action->tab->window->window), nullptr, page_save_finished, save);
        g_object_unref(dialog);
    } else {
        const char *uri = webkit_web_view_get_uri(action->tab->view);
        auto *resource = webkit_web_view_get_main_resource(action->tab->view);
        if (!resource || !uri) return;
        auto *request = new SourceData{action->tab->window,
            WEBKIT_WEB_VIEW(g_object_ref(action->tab->view)), uri};
        webkit_web_resource_get_data(resource, nullptr, source_loaded, request);
    }
}

void append_page_action(WebKitContextMenu *menu, TabState *tab, const char *label, PageActionKind kind) {
    auto *action = g_simple_action_new("page-action", nullptr);
    auto *data = new PageActionData{tab, kind};
    g_signal_connect_data(action, "activate", G_CALLBACK(page_action), data,
        [](void *value, GClosure *) { delete static_cast<PageActionData *>(value); }, G_CONNECT_DEFAULT);
    webkit_context_menu_append(menu, webkit_context_menu_item_new_from_gaction(G_ACTION(action), label, nullptr));
    g_object_unref(action);
}

void append_context_open(WebKitContextMenu *menu, WindowState *window, const char *uri,
                         const char *label, ContextOpen mode, bool image = false) {
    auto *action = g_simple_action_new(mode == ContextOpen::tab ? "context-open-tab" : "context-open-window", nullptr);
    auto *data = new ContextOpenData{window, uri ? uri : "", mode, image};
    g_signal_connect_data(action, "activate", G_CALLBACK(context_open), data,
        [](void *value, GClosure *) { delete static_cast<ContextOpenData *>(value); }, G_CONNECT_DEFAULT);
    auto *item = webkit_context_menu_item_new_from_gaction(G_ACTION(action), label, nullptr);
    webkit_context_menu_append(menu, item);
    g_object_unref(action);
}

gboolean context_menu(WebKitWebView *, WebKitContextMenu *menu,
                      WebKitHitTestResult *hit, TabState *tab) {
    const bool link = webkit_hit_test_result_context_is_link(hit);
    const bool image = webkit_hit_test_result_context_is_image(hit);
    webkit_context_menu_remove_all(menu);
    if (link) {
        const char *uri = webkit_hit_test_result_get_link_uri(hit);
        append_context_open(menu, tab->window, uri, "Open link in new tab", ContextOpen::tab);
        append_context_open(menu, tab->window, uri, "Open link in new window", ContextOpen::window);
        webkit_context_menu_append(menu, webkit_context_menu_item_new_from_stock_action(
            WEBKIT_CONTEXT_MENU_ACTION_COPY_LINK_TO_CLIPBOARD));
    }
    if (link && image) webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());
    if (image) {
        const char *uri = webkit_hit_test_result_get_image_uri(hit);
        append_context_open(menu, tab->window, uri, "Open image in new tab", ContextOpen::tab, true);
        append_context_open(menu, tab->window, uri, "Open image in new window", ContextOpen::window, true);
        append_image_data_action(menu, tab->window, uri, "Save image as…", true);
        append_image_data_action(menu, tab->window, uri, "Copy image", false);
        webkit_context_menu_append(menu, webkit_context_menu_item_new_from_stock_action(
            WEBKIT_CONTEXT_MENU_ACTION_COPY_IMAGE_URL_TO_CLIPBOARD));
    }
    if (link || image) webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());
    append_page_action(menu, tab, "Save page as…", PageActionKind::save);
    append_page_action(menu, tab, "Print…", PageActionKind::print);
    append_page_action(menu, tab, "View page source", PageActionKind::source);
    return FALSE;
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string html_escape(std::string_view value) {
    auto *escaped = g_markup_escape_text(value.data(), static_cast<gssize>(value.size()));
    std::string result = escaped ? escaped : "";
    g_free(escaped);
    return result;
}

std::string download_name(const vantage::DownloadEntry &entry) {
    auto name = std::filesystem::path(entry.destination).filename().string();
    if (!name.empty()) return name;
    auto source = entry.uri.substr(0, entry.uri.find_first_of("?#"));
    const auto slash = source.find_last_of('/');
    if (slash != std::string::npos) source.erase(0, slash + 1);
    auto *decoded = g_uri_unescape_string(source.c_str(), nullptr);
    name = decoded && *decoded ? decoded : "Download";
    g_free(decoded);
    return name;
}

std::string query_value(std::string_view uri, std::string_view key) {
    const auto query = uri.find('?');
    if (query == std::string_view::npos) return {};
    const std::string needle = std::string(key) + "=";
    for (std::size_t start = query + 1; start < uri.size();) {
        const auto end = uri.find('&', start);
        const auto part = uri.substr(start, end == std::string_view::npos ? uri.size() - start : end - start);
        if (part.starts_with(needle)) {
            auto *decoded = g_uri_unescape_string(std::string(part.substr(needle.size())).c_str(), nullptr);
            std::string result = decoded ? decoded : "";
            g_free(decoded);
            return result;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return {};
}

std::vector<std::string> split_values(std::string_view text) {
    std::vector<std::string> values;
    for (std::size_t start = 0; start <= text.size();) {
        const auto end = text.find('|', start);
        values.emplace_back(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return values;
}

std::string favicon_html(ApplicationState *owner, const std::string &uri) {
    const auto data = owner->data->favicon(uri);
    if (!data.empty()) return "<img class=favicon src='" + html_escape(data) + "' alt=''>";
    return "<span class=fallback>◉</span>";
}

struct HistoryStamp { std::string key; std::string heading; std::string time; };

HistoryStamp history_stamp(std::int64_t seconds) {
    auto *date = g_date_time_new_from_unix_local(seconds);
    auto *today = g_date_time_new_now_local();
    auto *yesterday = g_date_time_add_days(today, -1);
    auto format = [date](const char *pattern) {
        auto *value = g_date_time_format(date, pattern);
        std::string result = value ? value : "";
        g_free(value);
        return result;
    };
    const auto key = format("%Y-%m-%d");
    auto *today_key = g_date_time_format(today, "%Y-%m-%d");
    auto *yesterday_key = g_date_time_format(yesterday, "%Y-%m-%d");
    const auto long_date = format("%A, %B %e, %Y");
    std::string heading = key == (today_key ? today_key : "") ? "Today - " + long_date :
        key == (yesterday_key ? yesterday_key : "") ? "Yesterday - " + long_date : long_date;
    auto time = format("%l:%M %p");
    if (!time.empty() && time.front() == ' ') time.erase(time.begin());
    g_free(today_key);
    g_free(yesterday_key);
    g_date_time_unref(yesterday);
    g_date_time_unref(today);
    g_date_time_unref(date);
    return {key, std::move(heading), std::move(time)};
}

std::string uri_host(const std::string &uri) {
    GError *error = nullptr;
    auto *parsed = g_uri_parse(uri.c_str(), G_URI_FLAGS_NONE, &error);
    const char *host = parsed ? g_uri_get_host(parsed) : nullptr;
    std::string result = host ? host : uri;
    if (parsed) g_uri_unref(parsed);
    if (error) g_error_free(error);
    return result;
}

std::string internal_page(WindowState *state, std::string_view uri) {
    std::string title;
    std::string content;
    if (uri == "vantage:history") {
        title = "History";
        std::string active_day;
        for (const auto &entry : state->owner->data->history()) {
            const auto stamp = history_stamp(entry.visited_at);
            if (stamp.key != active_day) {
                if (!active_day.empty()) content += "</div></section>";
                active_day = stamp.key;
                content += "<section class=history-day><h2>" + html_escape(stamp.heading) + "</h2><div class=history-list>";
            }
            const auto shown = entry.title.empty() ? entry.uri : entry.title;
            content += "<div class='item history-item' data-search='" + html_escape(shown + " " + entry.uri + " " + stamp.time) + "'><input class=pick type=checkbox value='" +
                std::to_string(entry.id) + "'><time>" + html_escape(stamp.time) + "</time>" + favicon_html(state->owner, entry.uri) + "<a class='details history-details' href='" + html_escape(entry.uri) +
                "'><strong>" + html_escape(shown) + "</strong><span>" + html_escape(uri_host(entry.uri)) + "</span></a><details class=rowmenu><summary title='History actions'>⋮</summary>"
                "<div><button data-site='" + html_escape(entry.uri) + "' onclick='moreFromSite(this)'>More from this site</button>"
                "<a href='vantage:history-delete?ids=" + std::to_string(entry.id) + "'>Delete from history</a></div></details></div>";
        }
        if (!active_day.empty()) content += "</div></section>";
        if (content.empty()) content = "<p class=empty>No browsing history yet.</p>";
    } else if (uri == "vantage:bookmarks") {
        title = "Bookmarks";
        content += "<details class=add><summary>Add bookmark</summary><div class=form><input id=addTitle placeholder='Title'><input id=addUri placeholder='https://example.com'><button onclick=addBookmark()>Add</button></div></details>";
        for (const auto &entry : state->owner->data->bookmarks()) {
            const auto shown = entry.title.empty() ? entry.uri : entry.title;
            content += "<div class=item data-search='" + html_escape(shown + " " + entry.uri) + "'><input class=pick type=checkbox value='" +
                html_escape(entry.uri) + "'>" + favicon_html(state->owner, entry.uri) + "<a class=details href='" + html_escape(entry.uri) +
                "'><strong>" + html_escape(shown) + "</strong><span>" + html_escape(entry.uri) + "</span></a><details class=edit><summary>Edit</summary>"
                "<div class=form><input class=editTitle value='" + html_escape(shown) + "'><input class=editUri value='" + html_escape(entry.uri) +
                "'><button data-old='" + html_escape(entry.uri) + "' onclick=editBookmark(this)>Save</button></div></details></div>";
        }
        if (content.empty()) content = "<p class=empty>No bookmarks yet. Use the star in the address bar.</p>";
    } else if (uri == "vantage:downloads") {
        title = "Downloads";
        for (const auto &entry : state->owner->data->downloads()) {
            const auto filename = download_name(entry);
            const auto extension = std::filesystem::path(filename).extension().string();
            const auto progress = entry.status == "downloading" ? " · " + format_bytes(entry.received) + (entry.total ? " / " + format_bytes(entry.total) : "") : "";
            content += "<div class=item data-search='" + html_escape(filename + " " + entry.uri) + "'><span class=fileicon>" +
                html_escape(extension.empty() ? "FILE" : extension.substr(1, 4)) + "</span><div class=details><strong>" + html_escape(filename) +
                "</strong><span>" + html_escape(entry.status + progress) + " · " + html_escape(entry.uri) + "</span></div><div class=actions>"
                "<a title='Copy download link' href='vantage:download-copy?id=" + std::to_string(entry.id) + "'><svg viewBox='0 0 24 24'><path d='M10 13a5 5 0 0 0 7 0l2-2a5 5 0 0 0-7-7l-1 1'/><path d='M14 11a5 5 0 0 0-7 0l-2 2a5 5 0 0 0 7 7l1-1'/></svg></a>"
                "<a title='Show in Files' href='vantage:download-show?id=" + std::to_string(entry.id) + "'><svg viewBox='0 0 24 24'><path d='M3 6h7l2 2h9v11H3z'/></svg></a>" +
                (entry.status == "downloading" ? "<a title='Cancel download' href='vantage:download-cancel?id=" + std::to_string(entry.id) + "'><svg viewBox='0 0 24 24'><path d='M6 6l12 12M18 6L6 18'/></svg></a>" : "") +
                "<a title='Remove from history' href='vantage:download-delete?id=" + std::to_string(entry.id) + "'><svg viewBox='0 0 24 24'><path d='M5 7h14M9 7V4h6v3M8 7l1 13h6l1-13'/></svg></a></div></div>";
        }
        if (content.empty()) content = "<p class=empty>No downloads yet.</p>";
    } else if (uri == "vantage:settings") {
        title = "Settings";
        content = "<div class=item><strong>Privacy by default</strong><span>Vantage does not include telemetry. Private windows use ephemeral storage and do not write browsing history.</span></div>";
    } else {
        title = "About Vantage";
        content = "<div class=item><strong>Vantage Browser</strong><span>A lightweight, privacy-focused WebKit browser.</span></div>";
    }
    const bool selectable = uri == "vantage:history" || uri == "vantage:bookmarks";
    const std::string bulk = selectable ? "<button class=bulk onclick=bulkDelete('" + std::string(uri == "vantage:history" ? "history-delete" : "bookmark-delete") + "')>Delete selected</button>" : "";
    return "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>" + title + "</title><style>html{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#20201f;color:#eee9df;"
        "font:15px Inter,'Avenir Next','Segoe UI',system-ui,sans-serif}main{width:min(980px,calc(100% - 48px));margin:48px auto}"
        ".top{display:flex;flex-direction:column;align-items:center;gap:14px;margin-bottom:28px}.top h1{font-size:24px;margin:0}.top .search{width:min(520px,100%)}.bulk{align-self:flex-end;margin-top:-56px}"
        ".search,.form input{height:42px;border:1px solid #4a4844;border-radius:22px;background:#2b2a29;color:#fff;padding:0 18px;outline:none}"
        ".search:focus,.form input:focus{border-color:#ff8a62}.bulk{justify-self:end}[hidden]{display:none!important}.item{display:flex;align-items:center;gap:14px;padding:14px 16px;margin:0 0 10px;"
        "border:1px solid #403e3a;border-radius:11px;background:#292827;color:inherit}.item:hover{border-color:#67635d;background:#302f2d}"
        ".pick{width:17px;height:17px;accent-color:#ff7657}.favicon{width:20px;height:20px;object-fit:contain}.fallback{width:20px;text-align:center;color:#8b8881}"
        ".details{display:flex;flex:1;min-width:0;flex-direction:column;gap:4px;color:inherit;text-decoration:none}.details strong,.details span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
        ".item span,.empty{color:#aaa59c}.actions{display:flex;gap:4px}.actions a{display:grid;place-items:center;width:34px;height:34px;background:transparent;color:#c9c4ba;text-decoration:none}.actions svg{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}.actions a:hover{color:#ff8a62}.bulk,.form button{border:0;border-radius:7px;background:#3b3936;color:#eee9df;padding:8px 11px;cursor:pointer}.bulk:hover,.form button:hover{background:#4b4844}.fileicon{display:grid;place-items:center;width:42px;height:46px;border-radius:6px;background:#3f9e91;color:#fff!important;font:bold 10px ui-monospace,monospace;text-transform:uppercase}"
        ".add{margin-bottom:16px}.add summary,.edit summary{cursor:pointer;color:#ccc7bd}.form{display:flex;gap:8px;margin-top:10px}.form input{flex:1;border-radius:8px}.edit{max-width:60px}.edit[open]{max-width:100%;flex:1}"
        ".rowmenu{position:relative}.rowmenu summary{list-style:none;cursor:pointer;font-size:22px;padding:4px 8px}.rowmenu summary::-webkit-details-marker{display:none}.rowmenu>div{position:absolute;z-index:2;right:0;top:32px;width:170px;padding:6px;background:#343331;border:1px solid #4d4a45;border-radius:8px;box-shadow:0 8px 24px #0008}.rowmenu button,.rowmenu a{display:block;width:100%;padding:9px;border:0;background:transparent;color:#eee9df;text-align:left;text-decoration:none}.rowmenu button:hover,.rowmenu a:hover{color:#ff8a62}"
        ".history-day{margin:0 0 16px;border:1px solid #403e3a;border-radius:11px;background:#292827;overflow:visible}.history-day h2{margin:0;padding:14px 16px 9px;font-size:14px}.history-list{padding:0 8px 8px}.history-item{gap:10px;margin:0;padding:7px 8px;border:0;border-radius:7px;background:transparent}.history-item:hover{border:0;background:#353432}.history-item time{width:76px;flex:none;color:#aaa59c;font-size:12px}.history-item .favicon,.history-item .fallback{width:17px;height:17px}.history-details{flex-direction:row;align-items:baseline;gap:8px}.history-details strong{font-size:13px}.history-details span{font-size:12px}.history-item .rowmenu summary{font-size:19px;padding:1px 6px}"
        "</style></head><body><main><div class=top><h1>" + title + "</h1><input class=search type=search placeholder='Search " + title +
        "' id=pageSearch oninput=filterRows(this.value)>" + bulk + "</div>" + content +
        "</main><script>function filterRows(q){q=q.toLowerCase();document.querySelectorAll('[data-search]').forEach(e=>e.hidden=!e.dataset.search.toLowerCase().includes(q))}"
        "function selected(){return [...document.querySelectorAll('.pick:checked')].map(e=>e.value)}function bulkDelete(a){const v=selected();if(v.length)location.href='vantage:'+a+'?ids='+encodeURIComponent(v.join('|'))}"
        "function addBookmark(){location.href='vantage:bookmark-add?title='+encodeURIComponent(addTitle.value)+'&uri='+encodeURIComponent(addUri.value)}"
        "function editBookmark(b){const f=b.parentElement;location.href='vantage:bookmark-edit?old='+encodeURIComponent(b.dataset.old)+'&title='+encodeURIComponent(f.querySelector('.editTitle').value)+'&uri='+encodeURIComponent(f.querySelector('.editUri').value)}"
        "function moreFromSite(b){try{pageSearch.value=new URL(b.dataset.site).hostname;filterRows(pageSearch.value);b.closest('details').open=false}catch(e){}}"
        "</script></body></html>";
}

TabState *find_tab(WindowState *state, WebKitWebView *view) {
    const auto found = std::find_if(state->tabs.begin(), state->tabs.end(),
        [view](const auto &tab) { return tab->view == view; });
    return found == state->tabs.end() ? nullptr : found->get();
}

void load_decision(TabState *tab, const vantage::NavigationDecision &decision) {
    if (decision.kind == vantage::NavigationKind::web) {
        tab->internal_uri.clear();
        tab->display_uri.clear();
        webkit_web_view_load_uri(tab->view, decision.uri.c_str());
    } else if (decision.kind == vantage::NavigationKind::internal) {
        tab->internal_uri = decision.uri;
        tab->display_uri.clear();
        const char *icon = decision.uri == "vantage:history" ? "document-open-recent-symbolic" :
            decision.uri == "vantage:downloads" ? "folder-download-symbolic" :
            decision.uri == "vantage:bookmarks" ? "starred-symbolic" :
            decision.uri == "vantage:settings" ? "preferences-system-symbolic" :
            decision.uri == "vantage:about" ? "help-about-symbolic" : "web-browser-symbolic";
        gtk_image_set_from_icon_name(GTK_IMAGE(tab->favicon), icon);
        if (decision.uri != "vantage:new" && !decision.uri.starts_with("about:")) {
            const auto page = internal_page(tab->window, decision.uri);
            webkit_web_view_load_html(tab->view, page.c_str(), nullptr);
        } else webkit_web_view_load_html(tab->view,
            "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>New Tab</title><style>html{color-scheme:dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;"
            "display:grid;place-items:center;background:#20201f;font-family:Inter,'Avenir Next','Segoe UI',system-ui,sans-serif}"
            "form{width:min(620px,calc(100% - 48px))}input{width:100%;height:48px;padding:0 20px;border:1px solid #4a4844;"
            "border-radius:24px;outline:none;background:#2b2a29;color:#fff;font:16px Inter,'Avenir Next','Segoe UI',system-ui,sans-serif;"
            "box-shadow:0 8px 24px #0004}input::placeholder{color:#aaa59c}input:focus{border-color:#ff8a62;"
            "box-shadow:0 0 0 1px #ff8a62,0 8px 24px #0005}</style></head><body>"
            "<form action='https://www.google.com/search' method=get><input name=q type=search autocomplete=off spellcheck=false "
            "placeholder='Search' aria-label='Search'></form></body></html>",
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
    if (webkit_web_view_is_loading(state->view)) {
        if (auto *tab = find_tab(state, state->view)) tab->user_stopped = true;
        webkit_web_view_stop_loading(state->view);
    }
    else if (auto *tab = find_tab(state, state->view); tab && !tab->internal_uri.empty())
        load_decision(tab, state->policy.resolve(tab->internal_uri));
    else webkit_web_view_reload(state->view);
}

void sync_active_chrome(WindowState *state) {
    if (!state->view) return;
    const bool loading = webkit_web_view_is_loading(state->view);
    if (loading) {
        gtk_stack_set_visible_child(GTK_STACK(state->reload_stack), state->stop_icon);
    } else {
        gtk_stack_set_visible_child(GTK_STACK(state->reload_stack), state->reload_icon);
    }
    gtk_widget_set_tooltip_text(state->reload_stop, loading ? "Stop loading" : "Reload");
    gtk_widget_set_opacity(state->progress, loading ? 1.0 : 0.0);
    state->progress_fraction = webkit_web_view_get_estimated_load_progress(state->view);
    gtk_widget_queue_draw(state->progress);

    if (auto *tab = find_tab(state, state->view)) {
        const char *uri = webkit_web_view_get_uri(state->view);
        const std::string shown = tab->internal_uri == "vantage:new" ? "" :
            (!tab->display_uri.empty() ? tab->display_uri :
             (!tab->internal_uri.empty() ? tab->internal_uri : (uri ? uri : "")));
        if (!gtk_widget_has_focus(state->address))
            gtk_editable_set_text(GTK_EDITABLE(state->address), shown.c_str());
        const char *title = webkit_web_view_get_title(state->view);
        gtk_window_set_title(GTK_WINDOW(state->window), title && *title ? title : "Vantage Browser");
        const bool bookmarked = !shown.empty() && state->owner->data->is_bookmarked(shown);
        gtk_button_set_icon_name(GTK_BUTTON(state->bookmark_button),
            bookmarked ? "starred-symbolic" : "non-starred-symbolic");
        gtk_widget_set_tooltip_text(state->bookmark_button,
            bookmarked ? "Remove bookmark" : "Bookmark this tab");
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

struct FaviconRequest {
    WindowState *state{};
    WebKitWebView *view{};
    SoupSession *session{};
    SoupMessage *message{};
    std::string page_uri;
};

void finish_favicon_request(FaviconRequest *request) {
    if (request->message) g_object_unref(request->message);
    if (request->session) g_object_unref(request->session);
    g_object_unref(request->view);
    delete request;
}

void fallback_favicon_downloaded(GObject *source, GAsyncResult *result, void *data) {
    auto *request = static_cast<FaviconRequest *>(data);
    GError *error = nullptr;
    auto *bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    auto *tab = find_tab(request->state, request->view);
    if (bytes && tab) {
        gsize size = 0;
        const auto *raw = static_cast<const guchar *>(g_bytes_get_data(bytes, &size));
        if (!request->state->private_mode && raw && size) {
            auto *encoded = g_base64_encode(raw, size);
            const char *mime = request->message
                ? soup_message_headers_get_content_type(soup_message_get_response_headers(request->message), nullptr)
                : nullptr;
            request->state->owner->data->set_favicon(request->page_uri,
                std::string("data:") + (mime && *mime ? mime : "image/x-icon") + ";base64," + encoded);
            g_free(encoded);
        }
        if (auto *texture = gdk_texture_new_from_bytes(bytes, &error)) {
            gtk_image_set_from_paintable(GTK_IMAGE(tab->favicon), GDK_PAINTABLE(texture));
            g_object_unref(texture);
            sync_tab_activity(tab);
        }
        g_bytes_unref(bytes);
    }
    if (error) g_error_free(error);
    finish_favicon_request(request);
}

void fallback_favicon_url_ready(GObject *source, GAsyncResult *result, void *data) {
    auto *request = static_cast<FaviconRequest *>(data);
    GError *error = nullptr;
    auto *value = webkit_web_view_evaluate_javascript_finish(
        WEBKIT_WEB_VIEW(source), result, &error);
    if (!value || error || !find_tab(request->state, request->view)) {
        if (value) g_object_unref(value);
        if (error) g_error_free(error);
        finish_favicon_request(request);
        return;
    }
    auto *url = jsc_value_to_string(value);
    g_object_unref(value);
    const bool supported = url && (g_str_has_prefix(url, "https://") || g_str_has_prefix(url, "http://"));
    auto *message = supported ? soup_message_new("GET", url) : nullptr;
    g_free(url);
    if (!message) {
        finish_favicon_request(request);
        return;
    }
    request->session = soup_session_new();
    request->message = SOUP_MESSAGE(g_object_ref(message));
    soup_session_send_and_read_async(request->session, message, G_PRIORITY_LOW, nullptr,
        fallback_favicon_downloaded, request);
    g_object_unref(message);
}

void load_changed(WebKitWebView *view, WebKitLoadEvent event, TabState *tab) {
    if (event == WEBKIT_LOAD_STARTED) tab->user_stopped = false;
    if (event != WEBKIT_LOAD_FINISHED) return;
    if (!tab->window->private_mode && tab->internal_uri.empty()) {
        const char *uri = webkit_web_view_get_uri(view);
        const char *title = webkit_web_view_get_title(view);
        if (uri && (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://")))
            tab->window->owner->data->add_history(uri, title ? title : uri, now_seconds());
    }
    const char *page_uri = webkit_web_view_get_uri(view);
    if (!page_uri || (!g_str_has_prefix(page_uri, "http://") && !g_str_has_prefix(page_uri, "https://"))) return;
    auto *request = new FaviconRequest{tab->window,
        WEBKIT_WEB_VIEW(g_object_ref(view)), nullptr, nullptr, page_uri};
    constexpr auto script = "document.querySelector('link[rel~=icon]')?.href || new URL('/favicon.ico',location.href).href";
    webkit_web_view_evaluate_javascript(view, script, -1, nullptr, nullptr, nullptr,
        fallback_favicon_url_ready, request);
}

void progress_changed(WebKitWebView *view, GParamSpec *, TabState *tab) {
    if (tab->window->view == view) {
        tab->window->progress_fraction = webkit_web_view_get_estimated_load_progress(view);
        gtk_widget_queue_draw(tab->window->progress);
    }
}

void draw_load_progress(GtkDrawingArea *, cairo_t *cr, int width, int height, void *data) {
    const auto fraction = std::clamp(static_cast<WindowState *>(data)->progress_fraction, 0.0, 1.0);
    cairo_set_source_rgb(cr, 0xff / 255.0, 0x76 / 255.0, 0x57 / 255.0);
    cairo_rectangle(cr, 0, 0, width * fraction, height);
    cairo_fill(cr);
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
    gtk_label_set_text(GTK_LABEL(tab->label), title && *title ? title : "New Tab");
    if (tab->window->view == view)
        gtk_window_set_title(GTK_WINDOW(tab->window->window), title && *title ? title : "Vantage Browser");
}

gboolean decide_policy(WebKitWebView *view, WebKitPolicyDecision *decision,
                       WebKitPolicyDecisionType type, TabState *tab) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) return FALSE;
    auto *navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    auto *action = webkit_navigation_policy_decision_get_navigation_action(navigation);
    auto *request = webkit_navigation_action_get_request(action);
    const char *uri = webkit_uri_request_get_uri(request);
    if (!tab->internal_uri.empty() && uri && std::string_view(uri) == "about:blank") return FALSE;
    const std::string_view target = uri ? uri : "";
    const char *current_uri = webkit_web_view_get_uri(view);
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION && target.starts_with("about:") &&
        current_uri && (g_str_has_prefix(current_uri, "http://") || g_str_has_prefix(current_uri, "https://"))) {
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        webkit_policy_decision_ignore(decision);
        const auto resolved_window = tab->window->policy.resolve(target);
        if (resolved_window.kind == vantage::NavigationKind::web)
            new_tab(tab->window, resolved_window.uri);
        else
            new_tab(tab->window, "vantage:new");
        return TRUE;
    }
    if (webkit_navigation_action_get_mouse_button(action) == GDK_BUTTON_MIDDLE &&
        !target.starts_with("vantage:")) {
        const auto resolved_middle = tab->window->policy.resolve(uri ? uri : "");
        if (resolved_middle.kind == vantage::NavigationKind::web) {
            webkit_policy_decision_ignore(decision);
            new_tab(tab->window, resolved_middle.uri);
            return TRUE;
        }
    }
    if (!tab->internal_uri.empty() && target.starts_with("vantage:")) {
        auto *data = tab->window->owner->data.get();
        if (target.starts_with("vantage:history-delete")) {
            std::vector<std::int64_t> ids;
            for (const auto &value : split_values(query_value(target, "ids")))
                try { ids.push_back(std::stoll(value)); } catch (const std::exception &) {}
            data->remove_history(ids);
            webkit_policy_decision_ignore(decision);
            load_decision(tab, tab->window->policy.resolve("vantage:history"));
            return TRUE;
        }
        if (target.starts_with("vantage:bookmark-delete")) {
            for (const auto &value : split_values(query_value(target, "ids"))) data->remove_bookmark(value);
            webkit_policy_decision_ignore(decision);
            load_decision(tab, tab->window->policy.resolve("vantage:bookmarks"));
            return TRUE;
        }
        if (target.starts_with("vantage:bookmark-add") || target.starts_with("vantage:bookmark-edit")) {
            const auto bookmark_uri = query_value(target, "uri");
            const auto title = query_value(target, "title");
            const auto resolved = tab->window->policy.resolve(bookmark_uri);
            if (resolved.kind == vantage::NavigationKind::web) {
                if (target.starts_with("vantage:bookmark-edit")) data->update_bookmark(query_value(target, "old"), {resolved.uri, title});
                else data->add_bookmark({resolved.uri, title.empty() ? resolved.uri : title});
            }
            webkit_policy_decision_ignore(decision);
            load_decision(tab, tab->window->policy.resolve("vantage:bookmarks"));
            return TRUE;
        }
        if (target.starts_with("vantage:download-")) {
            std::int64_t id = 0;
            try { id = std::stoll(query_value(target, "id")); } catch (const std::exception &) {}
            const auto downloads = data->downloads();
            const auto found = std::find_if(downloads.begin(), downloads.end(), [id](const auto &entry) { return entry.id == id; });
            if (found != downloads.end()) {
                if (target.starts_with("vantage:download-cancel")) cancel_download(tab->window->owner, id);
                else if (target.starts_with("vantage:download-delete")) data->remove_download(id);
                else if (target.starts_with("vantage:download-copy")) {
                    auto *clipboard = gtk_widget_get_clipboard(tab->window->window);
                    gdk_clipboard_set_text(clipboard, found->uri.c_str());
                } else if (target.starts_with("vantage:download-show")) {
                    if (!found->destination.empty()) {
                        const auto folder = std::filesystem::path(found->destination).parent_path();
                        GError *launch_error = nullptr;
                        auto *nautilus = g_find_program_in_path("nautilus");
                        auto *process = nautilus ? g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &launch_error,
                            nautilus, "--new-window", folder.c_str(), nullptr) : nullptr;
                        if (process) g_object_unref(process);
                        if (!process) {
                            if (launch_error) g_clear_error(&launch_error);
                            auto *folder_uri = g_filename_to_uri(folder.c_str(), nullptr, nullptr);
                            if (folder_uri) {
                                g_app_info_launch_default_for_uri(folder_uri, nullptr, nullptr);
                                g_free(folder_uri);
                            }
                        }
                        g_free(nautilus);
                    }
                }
            }
            webkit_policy_decision_ignore(decision);
            load_decision(tab, tab->window->policy.resolve("vantage:downloads"));
            return TRUE;
        }
    }
    const auto resolved = tab->window->policy.resolve(uri ? uri : "");
    if (resolved.kind == vantage::NavigationKind::web) {
        tab->internal_uri.clear();
        tab->display_uri.clear();
        return FALSE;
    }
    webkit_policy_decision_ignore(decision);
    if (resolved.kind == vantage::NavigationKind::internal) load_decision(tab, resolved);
    return TRUE;
}

gboolean tls_failed(WebKitWebView *, const char *, GTlsCertificate *, GTlsCertificateFlags, TabState *) {
    return FALSE;
}

gboolean load_failed(WebKitWebView *view, WebKitLoadEvent, const char *failing_uri,
                     GError *error, TabState *tab) {
    const bool cancelled = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
        g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED) ||
        g_error_matches(error, WEBKIT_POLICY_ERROR,
            WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE);
    if (cancelled && !tab->user_stopped) return TRUE;
    auto *escaped_uri = g_markup_escape_text(failing_uri ? failing_uri : "Unknown address", -1);
    auto *escaped_message = g_markup_escape_text(error && error->message ? error->message : "Unknown error", -1);
    const std::string eyebrow = cancelled ? "Navigation stopped" : "Navigation error";
    const std::string heading = cancelled ? "Operation cancelled." : "This page is unavailable.";
    const std::string explanation = cancelled ? "Vantage stopped loading this page at your request." :
        "Vantage could not finish loading the requested address.";
    const std::string page =
        "<!doctype html><html><head><meta charset=utf-8><title>Page unavailable</title>"
        "<style>html{color-scheme:dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;"
        "background:#11100f;color:#e8e3d9;font:16px system-ui,sans-serif}main{width:min(680px,calc(100% - 48px));padding:42px;"
        "background:#1b1a19;border:1px solid #393632;border-radius:14px}small{color:#ff8a62;font:700 12px ui-monospace,monospace;"
        "letter-spacing:.14em;text-transform:uppercase}h1{margin:12px 0 10px;font-size:38px;line-height:1.05}p{color:#aaa49a}"
        "pre{overflow:auto;margin:24px 0 0;padding:18px;background:#0c0c0b;border-left:3px solid #ff7657;color:#d8d4cc;"
        "font:14px/1.6 ui-monospace,monospace;white-space:pre-wrap}.key{color:#79d8b0}.value{color:#ffd37a}</style></head>"
        "<body><main><small>" + eyebrow + "</small><h1>" + heading + "</h1>"
        "<p>" + explanation + "</p><pre><span class=key>url</span>     <span class=value>" +
        std::string(escaped_uri) + "</span>\n<span class=key>error</span>   " + std::string(escaped_message) +
        "</pre></main></body></html>";
    g_free(escaped_uri);
    g_free(escaped_message);
    webkit_web_view_load_alternate_html(view, page.c_str(), failing_uri, failing_uri);
    return TRUE;
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
    if (gtk_widget_get_visible(state->find_bar)) find_changed(GTK_EDITABLE(state->find_entry), state);
}

void tab_selected(GtkButton *, TabState *tab) { select_tab(tab); }

void rounded_rectangle(cairo_t *cr, double x, double y, double width, double height,
                       double radius) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -G_PI_2, 0);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, G_PI_2);
    cairo_arc(cr, x + radius, y + height - radius, radius, G_PI_2, G_PI);
    cairo_arc(cr, x + radius, y + radius, radius, G_PI, G_PI + G_PI_2);
    cairo_close_path(cr);
}

void draw_tab_backdrop(GtkDrawingArea *, cairo_t *cr, int width, int height, void *data) {
    auto *tab = static_cast<TabState *>(data);
    if (tab->window->view != tab->view) {
        if (tab->hovered) {
            rounded_rectangle(cr, 9, 7, width - 18, 30, 7);
            cairo_set_source_rgb(cr, 0x35 / 255.0, 0x34 / 255.0, 0x32 / 255.0);
            cairo_fill(cr);
        }
        return;
    }

    const double edge = 9.0;
    const double inset = 9.0;
    const double left = inset;
    const double right = width - inset;
    const double top = 7.0;
    const double radius = 8.0;
    cairo_new_path(cr);
    cairo_move_to(cr, 0, height);
    cairo_curve_to(cr, edge * 0.55, height, left, height - edge * 0.45, left, height - edge);
    cairo_line_to(cr, left, top + radius);
    cairo_curve_to(cr, left, top + 3, left + 3, top, left + radius, top);
    cairo_line_to(cr, right - radius, top);
    cairo_curve_to(cr, right - 3, top, right, top + 3, right, top + radius);
    cairo_line_to(cr, right, height - edge);
    cairo_curve_to(cr, right, height - edge * 0.45, width - edge * 0.55, height, width, height);
    cairo_line_to(cr, 0, height);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 0x2c / 255.0, 0x2c / 255.0, 0x2c / 255.0);
    cairo_fill(cr);

    cairo_new_path(cr);
    cairo_move_to(cr, 0.5, height - 0.5);
    cairo_curve_to(cr, edge * 0.55, height - 0.5, left + 0.5, height - edge * 0.45, left + 0.5, height - edge);
    cairo_line_to(cr, left + 0.5, top + radius);
    cairo_curve_to(cr, left + 0.5, top + 3, left + 3, top + 0.5, left + radius, top + 0.5);
    cairo_line_to(cr, right - radius, top + 0.5);
    cairo_curve_to(cr, right - 3, top + 0.5, right - 0.5, top + 3, right - 0.5, top + radius);
    cairo_line_to(cr, right - 0.5, height - edge);
    cairo_curve_to(cr, right - 0.5, height - edge * 0.45, width - edge * 0.55, height - 0.5, width - 0.5, height - 0.5);
    cairo_set_source_rgb(cr, 0x39 / 255.0, 0x39 / 255.0, 0x36 / 255.0);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
}

void tab_pointer_entered(GtkEventControllerMotion *, double, double, TabState *tab) {
    tab->hovered = true;
    gtk_widget_queue_draw(tab->backdrop);
}

void tab_pointer_left(GtkEventControllerMotion *, TabState *tab) {
    tab->hovered = false;
    gtk_widget_queue_draw(tab->backdrop);
}

void draw_new_tab_backdrop(GtkDrawingArea *, cairo_t *cr, int width, int height, void *data) {
    auto *state = static_cast<WindowState *>(data);
    if (!state->new_tab_hovered) return;
    rounded_rectangle(cr, 0, 0, width, height, 7);
    cairo_set_source_rgb(cr, 0x3a / 255.0, 0x39 / 255.0, 0x36 / 255.0);
    cairo_fill(cr);
}

void new_tab_pointer_entered(GtkEventControllerMotion *, double, double, WindowState *state) {
    state->new_tab_hovered = true;
    gtk_widget_queue_draw(state->new_tab_backdrop);
}

void new_tab_pointer_left(GtkEventControllerMotion *, WindowState *state) {
    state->new_tab_hovered = false;
    gtk_widget_queue_draw(state->new_tab_backdrop);
}

void close_tab(TabState *tab) {
    auto *state = tab->window;
    const auto found = std::find_if(state->tabs.begin(), state->tabs.end(),
        [tab](const auto &candidate) { return candidate.get() == tab; });
    if (found == state->tabs.end()) return;
    if (state->middle_pressed_tab == tab) state->middle_pressed_tab = nullptr;
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

TabState *tab_at(WindowState *state, GtkWidget *header, double x, double y) {
    for (const auto &tab : state->tabs) {
        graphene_rect_t bounds;
        if (!gtk_widget_compute_bounds(tab->tab, header, &bounds)) continue;
        if (x >= bounds.origin.x && x <= bounds.origin.x + bounds.size.width &&
            y >= bounds.origin.y && y <= bounds.origin.y + bounds.size.height) {
            return tab.get();
        }
    }
    return nullptr;
}

void header_middle_pressed(GtkGestureClick *gesture, int, double x, double y, WindowState *state) {
    auto *header = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    state->middle_pressed_tab = tab_at(state, header, x, y);
}

void header_middle_released(GtkGestureClick *gesture, int, double x, double y, WindowState *state) {
    auto *header = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    auto *pressed = state->middle_pressed_tab;
    state->middle_pressed_tab = nullptr;
    if (pressed && tab_at(state, header, x, y) == pressed) queue_tab_close(pressed);
}
void add_tab(GtkButton *, WindowState *state) { new_tab(state, "vantage:new"); }

void open_internal(WindowState *state, const char *uri) {
    if (auto *tab = find_tab(state, state->view)) load_decision(tab, state->policy.resolve(uri));
}

void close_main_menu(WindowState *state) {
    if (auto *popover = gtk_menu_button_get_popover(GTK_MENU_BUTTON(state->menu_button)))
        gtk_popover_popdown(GTK_POPOVER(popover));
}
void show_history(GtkButton *, WindowState *state) { close_main_menu(state); open_internal(state, "vantage:history"); }
void show_downloads(GtkButton *, WindowState *state) { close_main_menu(state); open_internal(state, "vantage:downloads"); }
void show_bookmarks(GtkButton *, WindowState *state) { close_main_menu(state); open_internal(state, "vantage:bookmarks"); }
void show_settings(GtkButton *, WindowState *state) { close_main_menu(state); open_internal(state, "vantage:settings"); }
void show_about(GtkButton *, WindowState *state) { close_main_menu(state); open_internal(state, "vantage:about"); }
void menu_new_tab(GtkButton *, WindowState *state) { close_main_menu(state); new_tab(state, "vantage:new"); }
void menu_new_window(GtkButton *, WindowState *state) {
    close_main_menu(state);
    create_window(state->owner, "vantage:new", false, state, false);
}
void menu_new_private_window(GtkButton *, WindowState *state) {
    close_main_menu(state);
    create_window(state->owner, "vantage:new", false, state, true);
}
void delete_history(GtkButton *, WindowState *state) {
    close_main_menu(state);
    state->owner->data->clear_history();
    open_internal(state, "vantage:history");
}
void toggle_bookmark(GtkButton *, WindowState *state) {
    if (!state->view) return;
    const char *uri = webkit_web_view_get_uri(state->view);
    if (!uri || (!g_str_has_prefix(uri, "http://") && !g_str_has_prefix(uri, "https://"))) return;
    if (state->owner->data->is_bookmarked(uri)) state->owner->data->remove_bookmark(uri);
    else {
        const char *title = webkit_web_view_get_title(state->view);
        state->owner->data->add_bookmark({uri, title && *title ? title : uri});
    }
    sync_active_chrome(state);
}
void zoom_in(GtkButton *, WindowState *state) {
    close_main_menu(state);
    if (state->view) webkit_web_view_set_zoom_level(state->view,
        std::min(5.0, webkit_web_view_get_zoom_level(state->view) + 0.1));
}
void zoom_out(GtkButton *, WindowState *state) {
    close_main_menu(state);
    if (state->view) webkit_web_view_set_zoom_level(state->view,
        std::max(0.25, webkit_web_view_get_zoom_level(state->view) - 0.1));
}
void zoom_reset(GtkButton *, WindowState *state) {
    close_main_menu(state);
    if (state->view) webkit_web_view_set_zoom_level(state->view, 1.0);
}
void print_page(GtkButton *, WindowState *state) {
    close_main_menu(state);
    if (!state->view) return;
    auto *operation = webkit_print_operation_new(state->view);
    webkit_print_operation_run_dialog(operation, GTK_WINDOW(state->window));
    g_object_unref(operation);
}

GtkWidget *menu_item(const char *label, const char *shortcut, GCallback callback, WindowState *state) {
    auto *button = gtk_button_new();
    gtk_widget_add_css_class(button, "menu-item");
    auto *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    auto *name = gtk_label_new(label);
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_widget_set_hexpand(name, TRUE);
    gtk_box_append(GTK_BOX(row), name);
    if (shortcut && *shortcut) {
        auto *keys = gtk_label_new(shortcut);
        gtk_widget_add_css_class(keys, "shortcut");
        gtk_box_append(GTK_BOX(row), keys);
    }
    gtk_button_set_child(GTK_BUTTON(button), row);
    g_signal_connect(button, "clicked", callback, state);
    return button;
}

GtkWidget *create_main_menu(WindowState *state) {
    auto *popover = gtk_popover_new();
    gtk_widget_add_css_class(popover, "main-menu");
    auto *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_size_request(box, 310, -1);
    gtk_box_append(GTK_BOX(box), menu_item("New tab", "Ctrl+T", G_CALLBACK(menu_new_tab), state));
    gtk_box_append(GTK_BOX(box), menu_item("New window", "Ctrl+N", G_CALLBACK(menu_new_window), state));
    gtk_box_append(GTK_BOX(box), menu_item("New private window", "Ctrl+Shift+N", G_CALLBACK(menu_new_private_window), state));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), menu_item("History", "Ctrl+H", G_CALLBACK(show_history), state));
    gtk_box_append(GTK_BOX(box), menu_item("Downloads", "Ctrl+J", G_CALLBACK(show_downloads), state));
    gtk_box_append(GTK_BOX(box), menu_item("Bookmarks", "", G_CALLBACK(show_bookmarks), state));
    gtk_box_append(GTK_BOX(box), menu_item("Delete history", "Ctrl+Shift+Delete", G_CALLBACK(delete_history), state));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    auto *zoom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(zoom, "zoom-row");
    auto *zoom_label = gtk_label_new("Zoom");
    gtk_widget_set_hexpand(zoom_label, TRUE);
    gtk_widget_set_halign(zoom_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(zoom), zoom_label);
    auto *minus = gtk_button_new_with_label("−");
    auto *reset = gtk_button_new_with_label("100%");
    auto *plus = gtk_button_new_with_label("+");
    g_signal_connect(minus, "clicked", G_CALLBACK(zoom_out), state);
    g_signal_connect(reset, "clicked", G_CALLBACK(zoom_reset), state);
    g_signal_connect(plus, "clicked", G_CALLBACK(zoom_in), state);
    gtk_box_append(GTK_BOX(zoom), minus); gtk_box_append(GTK_BOX(zoom), reset); gtk_box_append(GTK_BOX(zoom), plus);
    gtk_box_append(GTK_BOX(box), zoom);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), menu_item("Print", "Ctrl+P", G_CALLBACK(print_page), state));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), menu_item("About Vantage", "", G_CALLBACK(show_about), state));
    gtk_box_append(GTK_BOX(box), menu_item("Settings", "", G_CALLBACK(show_settings), state));
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    return popover;
}

struct DownloadContext {
    ApplicationState *owner{};
    WebKitDownload *download{};
    std::int64_t record{};
    bool failed{};
    bool cancelled{};
    bool private_mode{};
    std::string destination;
    std::string suggested_name;
};

std::string format_bytes(std::uint64_t bytes) {
    static constexpr const char *units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) { value /= 1024.0; ++unit; }
    std::ostringstream out;
    if (unit == 0) out << std::fixed << std::setprecision(0);
    else out << std::fixed << std::setprecision(value < 10 ? 1 : 0);
    out << value << ' ' << units[unit];
    return out.str();
}

void clear_box(GtkWidget *box) {
    while (auto *child = gtk_widget_get_first_child(box)) gtk_box_remove(GTK_BOX(box), child);
}

void history_suggestion_clicked(GtkButton *button, WindowState *state) {
    const auto *uri = static_cast<const char *>(g_object_get_data(G_OBJECT(button), "suggestion-uri"));
    if (!uri) return;
    gtk_popover_popdown(GTK_POPOVER(state->address_popover));
    gtk_editable_set_text(GTK_EDITABLE(state->address), uri);
    submit_address(nullptr, state);
}

void address_changed(GtkEditable *editable, WindowState *state) {
    if (!gtk_widget_has_focus(state->address)) return;
    const std::string query = gtk_editable_get_text(editable);
    clear_box(state->address_suggestions);
    if (query.empty()) {
        gtk_popover_popdown(GTK_POPOVER(state->address_popover));
        return;
    }
    std::string needle = query;
    std::ranges::transform(needle, needle.begin(), [](unsigned char value) { return std::tolower(value); });
    std::vector<std::string> seen;
    unsigned shown = 0;
    for (const auto &entry : state->owner->data->history()) {
        std::string searchable = entry.title + " " + entry.uri;
        std::ranges::transform(searchable, searchable.begin(), [](unsigned char value) { return std::tolower(value); });
        if (searchable.find(needle) == std::string::npos || std::ranges::find(seen, entry.uri) != seen.end()) continue;
        seen.push_back(entry.uri);
        auto *button = gtk_button_new();
        gtk_widget_add_css_class(button, "address-suggestion");
        auto *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        auto *title = gtk_label_new((entry.title.empty() ? entry.uri : entry.title).c_str());
        auto *uri = gtk_label_new(entry.uri.c_str());
        gtk_widget_add_css_class(uri, "suggestion-uri");
        gtk_widget_set_halign(title, GTK_ALIGN_START);
        gtk_widget_set_halign(uri, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
        gtk_label_set_ellipsize(GTK_LABEL(uri), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_append(GTK_BOX(labels), title);
        gtk_box_append(GTK_BOX(labels), uri);
        gtk_button_set_child(GTK_BUTTON(button), labels);
        g_object_set_data_full(G_OBJECT(button), "suggestion-uri", g_strdup(entry.uri.c_str()), g_free);
        g_signal_connect(button, "clicked", G_CALLBACK(history_suggestion_clicked), state);
        gtk_box_append(GTK_BOX(state->address_suggestions), button);
        if (++shown == 8) break;
    }
    if (shown) {
        gtk_popover_popup(GTK_POPOVER(state->address_popover));
        gtk_popover_present(GTK_POPOVER(state->address_popover));
    }
    else gtk_popover_popdown(GTK_POPOVER(state->address_popover));
}

GtkWidget *download_row(const std::string &name, const std::string &detail, bool active) {
    auto *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "download-row");
    GtkWidget *icon = active ? gtk_spinner_new() : gtk_image_new_from_icon_name("document-save-symbolic");
    if (active) { gtk_widget_add_css_class(icon, "download-spinner"); gtk_spinner_start(GTK_SPINNER(icon)); }
    gtk_box_append(GTK_BOX(row), icon);
    auto *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(labels, TRUE);
    auto *title = gtk_label_new(name.c_str());
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(title), 48);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    auto *status = gtk_label_new(detail.c_str());
    gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(status), 48);
    gtk_widget_add_css_class(status, "download-detail");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(labels), title);
    gtk_box_append(GTK_BOX(labels), status);
    gtk_box_append(GTK_BOX(row), labels);
    return row;
}

void cancel_download(ApplicationState *owner, std::int64_t id) {
    const auto found = std::find_if(owner->active_downloads.begin(), owner->active_downloads.end(),
        [id](const auto *context) { return context->record == id; });
    if (found == owner->active_downloads.end()) return;
    (*found)->cancelled = true;
    if (!(*found)->private_mode) owner->data->update_download(id, "cancelled");
    webkit_download_cancel((*found)->download);
}

void cancel_download_clicked(GtkButton *button, ApplicationState *owner) {
    const auto *id = static_cast<const std::int64_t *>(g_object_get_data(G_OBJECT(button), "download-id"));
    if (id) cancel_download(owner, *id);
}

void view_download_history(GtkButton *, WindowState *state) {
    gtk_popover_popdown(GTK_POPOVER(state->downloads_popover));
    open_internal(state, "vantage:downloads");
}

void rebuild_download_popover(WindowState *state) {
    clear_box(state->downloads_box);
    std::size_t shown = 0;
    for (auto *context : state->owner->active_downloads) {
        const auto received = webkit_download_get_received_data_length(context->download);
        auto *response = webkit_download_get_response(context->download);
        const auto total = response && webkit_uri_response_get_content_length(response) > 0
            ? static_cast<std::uint64_t>(webkit_uri_response_get_content_length(response)) : 0;
        const auto name = context->destination.empty() ? "Download" : std::filesystem::path(context->destination).filename().string();
        const auto detail = format_bytes(received) + (total ? " / " + format_bytes(total) : "") ;
        auto *row = download_row(name, detail, true);
        auto *cancel = gtk_button_new_from_icon_name("process-stop-symbolic");
        gtk_widget_add_css_class(cancel, "download-cancel");
        gtk_widget_set_tooltip_text(cancel, "Cancel download");
        auto *id = g_new(std::int64_t, 1);
        *id = context->record;
        g_object_set_data_full(G_OBJECT(cancel), "download-id", id, g_free);
        g_signal_connect(cancel, "clicked", G_CALLBACK(cancel_download_clicked), state->owner);
        gtk_box_append(GTK_BOX(row), cancel);
        gtk_box_append(GTK_BOX(state->downloads_box), row);
        ++shown;
    }
    for (const auto &entry : state->owner->data->downloads(5)) {
        if (entry.status == "downloading") continue;
        gtk_box_append(GTK_BOX(state->downloads_box), download_row(
            download_name(entry), entry.status, false));
        if (++shown >= 5) break;
    }
    if (!shown) {
        auto *empty = gtk_label_new("No recent downloads");
        gtk_widget_add_css_class(empty, "download-empty");
        gtk_box_append(GTK_BOX(state->downloads_box), empty);
    }
    gtk_box_append(GTK_BOX(state->downloads_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    auto *history = gtk_button_new_with_label("View download history");
    gtk_widget_add_css_class(history, "download-history");
    g_signal_connect(history, "clicked", G_CALLBACK(view_download_history), state);
    gtk_box_append(GTK_BOX(state->downloads_box), history);
}

void downloads_visibility_changed(GtkWidget *popover, GParamSpec *, WindowState *state) {
    if (gtk_widget_get_visible(popover)) rebuild_download_popover(state);
}

void refresh_download_chrome(ApplicationState *owner) {
    const bool active = !owner->active_downloads.empty();
    for (const auto &window : owner->windows) {
        if (window->closed) continue;
        gtk_stack_set_visible_child(GTK_STACK(window->downloads_stack),
            active ? window->downloads_spinner : window->downloads_icon);
        if (active) gtk_spinner_start(GTK_SPINNER(window->downloads_spinner));
        else gtk_spinner_stop(GTK_SPINNER(window->downloads_spinner));
        if (gtk_widget_get_visible(window->downloads_popover)) rebuild_download_popover(window.get());
    }
}

gboolean window_closing(GtkWindow *, WindowState *state) {
    state->closed = true;
    return FALSE;
}

gboolean download_destination(WebKitDownload *download, const char *suggested, DownloadContext *context) {
    context->suggested_name = suggested && *suggested ? suggested : "Download";
    const char *source = webkit_uri_request_get_uri(webkit_download_get_request(download));
    const bool needs_safe_name = !suggested || std::string_view(suggested).size() > 180 ||
        (source && g_str_has_prefix(source, "data:"));
    if (!needs_safe_name) return FALSE;
    const char *downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    std::filesystem::path directory = downloads ? downloads : g_get_home_dir();
    std::error_code directory_error;
    std::filesystem::create_directories(directory, directory_error);
    std::string filename = suggested ? suggested : "download";
    if (filename.size() > 180 || (source && g_str_has_prefix(source, "data:"))) {
        std::string extension = ".bin";
        auto *response = webkit_download_get_response(download);
        const char *mime = response ? webkit_uri_response_get_mime_type(response) : nullptr;
        if (mime && g_str_has_prefix(mime, "image/")) {
            const std::string_view type = mime + 6;
            extension = type == "svg+xml" ? ".svg" : type == "jpeg" ? ".jpg" : "." + std::string(type);
        }
        filename = "image" + extension;
    }
    auto destination = vantage::safe_download_path(directory, filename);
    for (unsigned suffix = 1; std::filesystem::exists(destination); ++suffix) {
        const auto stem = destination.stem().string();
        const auto extension = destination.extension().string();
        destination = directory / (stem + " (" + std::to_string(suffix) + ")" + extension);
    }
    auto *uri = g_filename_to_uri(destination.c_str(), nullptr, nullptr);
    if (!uri) {
        context->failed = true;
        webkit_download_cancel(download);
        return TRUE;
    }
    webkit_download_set_destination(download, uri);
    context->destination = destination.string();
    g_free(uri);
    return TRUE;
}

void download_created_destination(WebKitDownload *download, const char *destination, DownloadContext *context) {
    const char *reported = webkit_download_get_destination(download);
    if (!reported || !*reported) reported = destination;
    auto *file = reported ? g_file_new_for_uri(reported) : nullptr;
    auto *path = file ? g_file_get_path(file) : nullptr;
    if (file) g_object_unref(file);
    if (!path && reported && g_path_is_absolute(reported)) path = g_strdup(reported);
    if (path) context->destination = path;
    g_free(path);
    if (context->destination.empty()) {
        const char *downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
        context->destination = (std::filesystem::path(downloads ? downloads : g_get_home_dir()) /
            context->suggested_name).string();
    }
    const char *source = webkit_uri_request_get_uri(webkit_download_get_request(download));
    if (!context->private_mode && context->record == 0)
        context->record = context->owner->data->add_download(source ? source : "", context->destination,
            "downloading", now_seconds());
    refresh_download_chrome(context->owner);
}

void download_failed(WebKitDownload *, GError *error, DownloadContext *context) {
    context->failed = true;
    if (!context->private_mode && context->record != 0 && !context->cancelled) {
        const std::string status = error && error->message ? "failed: " + std::string(error->message) : "failed";
        context->owner->data->update_download(context->record, status);
    }
}
void download_received(WebKitDownload *download, guint64, DownloadContext *context) {
    const auto received = webkit_download_get_received_data_length(download);
    auto *response = webkit_download_get_response(download);
    const auto total = response && webkit_uri_response_get_content_length(response) > 0
        ? static_cast<std::uint64_t>(webkit_uri_response_get_content_length(response)) : 0;
    if (!context->private_mode && context->record != 0)
        context->owner->data->update_download_progress(context->record, received, total);
    refresh_download_chrome(context->owner);
}
void download_finished(WebKitDownload *download, DownloadContext *context) {
    if (!context->failed && !context->cancelled && !context->private_mode && context->record != 0) {
        const auto received = webkit_download_get_received_data_length(download);
        auto *response = webkit_download_get_response(download);
        const auto total = response && webkit_uri_response_get_content_length(response) > 0
            ? static_cast<std::uint64_t>(webkit_uri_response_get_content_length(response)) : received;
        context->owner->data->update_download_progress(context->record, received, total);
        context->owner->data->update_download(context->record, "complete");
    }
    auto &active = context->owner->active_downloads;
    active.erase(std::remove(active.begin(), active.end(), context), active.end());
    refresh_download_chrome(context->owner);
    g_object_unref(context->download);
    delete context;
}
void download_started(WebKitNetworkSession *, WebKitDownload *download, ApplicationState *owner) {
    auto *view = webkit_download_get_web_view(download);
    bool private_mode = false;
    for (const auto &window : owner->windows)
        if (find_tab(window.get(), view)) { private_mode = window->private_mode; break; }
    auto *context = new DownloadContext{owner, WEBKIT_DOWNLOAD(g_object_ref(download)), 0, false, false, private_mode, {}, {}};
    owner->active_downloads.push_back(context);
    refresh_download_chrome(owner);
    g_signal_connect(download, "decide-destination", G_CALLBACK(download_destination), context);
    g_signal_connect(download, "created-destination", G_CALLBACK(download_created_destination), context);
    g_signal_connect(download, "failed", G_CALLBACK(download_failed), context);
    g_signal_connect(download, "received-data", G_CALLBACK(download_received), context);
    g_signal_connect(download, "finished", G_CALLBACK(download_finished), context);
}

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
    constexpr double end_angle = 4.65;
    cairo_arc(cr, cx, cy, 6.0, -0.65, end_angle);
    cairo_stroke(cr);
    const double tip_x = cx + 6.0 * std::cos(end_angle);
    const double tip_y = cy + 6.0 * std::sin(end_angle);
    constexpr double arrow_rotation = -G_PI / 8.0;
    const double arc_tangent_x = -std::sin(end_angle);
    const double arc_tangent_y = std::cos(end_angle);
    const double tangent_x = arc_tangent_x * std::cos(arrow_rotation) - arc_tangent_y * std::sin(arrow_rotation);
    const double tangent_y = arc_tangent_x * std::sin(arrow_rotation) + arc_tangent_y * std::cos(arrow_rotation);
    const double base_x = tip_x - 4.0 * tangent_x;
    const double base_y = tip_y - 4.0 * tangent_y;
    const double normal_x = -tangent_y;
    const double normal_y = tangent_x;
    cairo_move_to(cr, base_x + 2.0 * normal_x, base_y + 2.0 * normal_y);
    cairo_line_to(cr, tip_x, tip_y);
    cairo_line_to(cr, base_x - 2.0 * normal_x, base_y - 2.0 * normal_y);
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

gboolean select_address_deferred(void *data) {
    auto *state = static_cast<WindowState *>(data);
    if (!state->closed) gtk_editable_select_region(GTK_EDITABLE(state->address), 0, -1);
    return G_SOURCE_REMOVE;
}

void address_focus_changed(GtkWidget *widget, GParamSpec *, WindowState *state) {
    if (gtk_widget_has_focus(widget)) {
        state->address_focused_at = g_get_monotonic_time();
        g_idle_add(select_address_deferred, state);
    }
}

void address_clicked(GtkGestureClick *, int, double, double, WindowState *state) {
    if (g_get_monotonic_time() - state->address_focused_at <= 200000)
        g_idle_add(select_address_deferred, state);
}

TabState *new_tab(WindowState *state, const std::string &uri) {
    auto owned = std::make_unique<TabState>();
    auto *tab = owned.get();
    tab->window = state;
    tab->view = state->private_session
        ? WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", state->private_session, nullptr))
        : WEBKIT_WEB_VIEW(webkit_web_view_new());
    auto *find_style = webkit_user_style_sheet_new(
        ".vantage-find-match{display:contents!important;background:transparent!important;color:#ffd37a!important;"
        "border:0!important;border-radius:0!important;padding:0!important;margin:0!important;box-shadow:none!important}"
        ".vantage-find-match.vantage-find-current{display:contents!important;background:transparent!important;color:#ff8a62!important}",
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_STYLE_LEVEL_USER, nullptr, nullptr);
    webkit_user_content_manager_add_style_sheet(webkit_web_view_get_user_content_manager(tab->view), find_style);
    webkit_user_style_sheet_unref(find_style);
    tab->page = GTK_WIDGET(tab->view);
    gtk_widget_set_vexpand(tab->page, TRUE);
    gtk_stack_add_child(GTK_STACK(state->stack), tab->page);

    tab->tab = gtk_overlay_new();
    gtk_widget_add_css_class(tab->tab, "browser-tab");
    gtk_widget_set_hexpand(tab->tab, FALSE);
    gtk_widget_set_size_request(tab->tab, 184, 38);
    tab->backdrop = gtk_drawing_area_new();
    gtk_widget_set_hexpand(tab->backdrop, TRUE);
    gtk_widget_set_halign(tab->backdrop, GTK_ALIGN_FILL);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(tab->backdrop), draw_tab_backdrop, tab, nullptr);
    gtk_overlay_set_child(GTK_OVERLAY(tab->tab), tab->backdrop);
    tab->body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(tab->body, "browser-tab-body");
    gtk_widget_set_halign(tab->body, GTK_ALIGN_FILL);
    gtk_widget_set_valign(tab->body, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(tab->body, TRUE);
    gtk_widget_set_size_request(tab->body, -1, 28);
    auto *hover_surface = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(hover_surface, "tab-hover-surface");
    gtk_widget_set_hexpand(hover_surface, TRUE);
    auto *select = gtk_button_new();
    gtk_widget_add_css_class(select, "tab-select");
    gtk_widget_set_hexpand(select, TRUE);
    auto *tab_content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    tab->icon_stack = gtk_stack_new();
    gtk_widget_set_size_request(tab->icon_stack, 18, 18);
    gtk_stack_set_hhomogeneous(GTK_STACK(tab->icon_stack), TRUE);
    gtk_stack_set_vhomogeneous(GTK_STACK(tab->icon_stack), TRUE);
    tab->favicon = gtk_image_new_from_icon_name("web-browser-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(tab->favicon), 18);
    tab->spinner = gtk_spinner_new();
    gtk_widget_set_size_request(tab->spinner, 18, 18);
    gtk_stack_add_child(GTK_STACK(tab->icon_stack), tab->favicon);
    gtk_stack_add_child(GTK_STACK(tab->icon_stack), tab->spinner);
    tab->label = gtk_label_new("New Tab");
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

    auto *tab_motion = gtk_event_controller_motion_new();
    g_signal_connect(tab_motion, "enter", G_CALLBACK(tab_pointer_entered), tab);
    g_signal_connect(tab_motion, "leave", G_CALLBACK(tab_pointer_left), tab);
    gtk_widget_add_controller(tab->tab, tab_motion);

    g_signal_connect(select, "clicked", G_CALLBACK(tab_selected), tab);
    g_signal_connect(close, "clicked", G_CALLBACK(tab_closed), tab);
    g_signal_connect(tab->view, "notify::uri", G_CALLBACK(uri_changed), tab);
    g_signal_connect(tab->view, "notify::title", G_CALLBACK(title_changed), tab);
    g_signal_connect(tab->view, "notify::is-loading", G_CALLBACK(loading_changed), tab);
    g_signal_connect(tab->view, "notify::estimated-load-progress", G_CALLBACK(progress_changed), tab);
    g_signal_connect(tab->view, "notify::favicon", G_CALLBACK(favicon_changed), tab);
    g_signal_connect(tab->view, "load-changed", G_CALLBACK(load_changed), tab);
    g_signal_connect(tab->view, "load-failed", G_CALLBACK(load_failed), tab);
    g_signal_connect(tab->view, "decide-policy", G_CALLBACK(decide_policy), tab);
    g_signal_connect(tab->view, "context-menu", G_CALLBACK(context_menu), tab);
    g_signal_connect(tab->view, "load-failed-with-tls-errors", G_CALLBACK(tls_failed), tab);
    g_signal_connect(webkit_web_view_get_find_controller(tab->view), "counted-matches",
        G_CALLBACK(find_counted), state);

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
    if (control && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        show_find(state);
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_t || keyval == GDK_KEY_T)) {
        new_tab(state, "vantage:new");
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_n || keyval == GDK_KEY_N)) {
        if ((modifiers & GDK_SHIFT_MASK) != 0)
            create_window(state->owner, "vantage:new", false, state, true);
        else create_window(state->owner, "vantage:new", false, state, false);
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_j || keyval == GDK_KEY_J)) {
        open_internal(state, "vantage:downloads");
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
        open_internal(state, "vantage:history");
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_d || keyval == GDK_KEY_D)) {
        toggle_bookmark(nullptr, state);
        return TRUE;
    }
    if (control && (modifiers & GDK_SHIFT_MASK) != 0 && keyval == GDK_KEY_Delete) {
        state->owner->data->clear_history();
        open_internal(state, "vantage:history");
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_p || keyval == GDK_KEY_P)) {
        print_page(nullptr, state);
        return TRUE;
    }
    if (control && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        if (auto *tab = find_tab(state, state->view)) close_tab(tab);
        return TRUE;
    }
    if ((control && (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) || keyval == GDK_KEY_F5) {
        reload_or_stop(nullptr, state);
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
    if (keyval == GDK_KEY_Escape && gtk_widget_get_visible(state->find_bar)) {
        close_find(nullptr, state);
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape && state->view && webkit_web_view_is_loading(state->view)) {
        if (auto *tab = find_tab(state, state->view)) tab->user_stopped = true;
        webkit_web_view_stop_loading(state->view);
        return TRUE;
    }
    return FALSE;
}

void install_style(GtkWidget *window) {
    auto *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "window { background: #171716; color: #ece8df; }"
        "headerbar { min-height: 34px; padding: 0 6px; background: #242423; box-shadow: inset 0 -1px #393936; border: 0; }"
        "headerbar.private-header { background: #2d2927; }"
        ".tab-strip { margin-top: 2px; }"
        ".browser-tab { min-width: 184px; margin-right: 0; background: transparent; }"
        ".browser-tab-body { background: transparent; }"
        ".tab-hover-surface { min-height: 24px; margin: 3px 9px 1px; border-radius: 7px; background: transparent; }"
        ".browser-tab button { min-height: 22px; padding: 0 7px; border: 0; outline: none; background: transparent; box-shadow: none; color: #d8d4cc; }"
        ".browser-tab .tab-select { min-width: 112px; }"
        ".browser-tab .tab-close { min-width: 20px; padding: 0 4px; opacity: 0; }"
        ".browser-tab:hover .tab-close, .browser-tab-body.active .tab-close { opacity: 1; }"
        ".browser-tab button:hover { background: transparent; }"
        ".browser-tab .tab-close:hover { background: transparent; color: #ff7657; }"
        ".new-tab { min-width: 28px; min-height: 28px; margin-left: 3px; }"
        ".navigation { background: #2c2c2c; border-bottom: 1px solid #393936; }"
        ".toolbar { padding: 6px 8px; background: #2c2c2c; }"
        ".toolbar button.flat, .new-tab.flat { min-width: 28px; min-height: 28px; padding: 2px; border: 0; border-radius: 7px; background: transparent; color: #d8d4cc; box-shadow: none; }"
        ".toolbar button.flat:hover { background: #3a3936; color: #fffaf0; }"
        ".toolbar button.flat:active { background: #494741; }"
        ".new-tab.flat:hover, .new-tab.flat:active { background: transparent; color: #fffaf0; }"
        ".toolbar .stop-icon { font-size: 27px; font-weight: 400; }"
        ".address-wrap entry { min-height: 30px; padding: 0 38px 0 12px; border-radius: 8px; border: 1px solid #45433f; background: #191918; color: #f1ede3; box-shadow: none; }"
        ".toolbar entry:focus { border-color: #ff8a62; box-shadow: 0 0 0 1px #ff8a62; }"
        ".address-suggestions contents { padding: 7px; border: 1px solid #474641; border-radius: 10px; background: #2c2c2c; }"
        ".address-suggestion { min-width: 520px; padding: 8px 11px; border: 0; border-radius: 7px; background: transparent; color: #eee9df; box-shadow: none; }"
        ".address-suggestion:hover { background: #45433f; }.address-suggestion .suggestion-uri { color: #aaa59c; font-size: 12px; }"
        ".address-bookmark { margin-right: 4px; }"
        ".main-menu contents { padding: 8px; border: 1px solid #474641; border-radius: 12px; background: #2c2c2c; }"
        ".main-menu .menu-item { padding: 8px 10px; border: 0; border-radius: 7px; background: transparent; color: #eee9df; box-shadow: none; }"
        ".main-menu .menu-item:hover { background: #474642; }"
        ".main-menu .shortcut { color: #aaa59c; }"
        ".main-menu separator { margin: 5px 2px; background: #494844; }"
        ".main-menu .zoom-row { padding: 5px 10px; }"
        ".main-menu .zoom-row button { min-width: 34px; padding: 5px; border: 0; border-radius: 6px; background: transparent; color: #eee9df; box-shadow: none; }"
        ".main-menu .zoom-row button:hover { background: #474642; }"
        ".privacy-indicator { color: #ff9a76; }"
        ".download-spinner { color: #8bdd68; }"
        ".downloads-popover contents { padding: 9px; border: 1px solid #474641; border-radius: 12px; background: #2c2c2c; }"
        ".downloads-popover .download-row { padding: 9px 10px; border-radius: 8px; color: #eee9df; }"
        ".downloads-popover .download-row:hover { background: #3b3936; }"
        ".downloads-popover .download-detail,.downloads-popover .download-empty { color: #aaa59c; }"
        ".downloads-popover .download-empty { padding: 18px; }"
        ".downloads-popover .download-history { padding: 9px; border: 0; border-radius: 7px; background: transparent; color: #eee9df; box-shadow: none; }"
        ".downloads-popover .download-history:hover { background: #474642; }"
        ".downloads-popover .download-cancel { min-width: 30px; min-height: 30px; padding: 4px; border: 0; border-radius: 6px; background: transparent; color: #c9c4ba; box-shadow: none; }"
        ".downloads-popover .download-cancel:hover { color: #ff8a62; }"
        ".browser-tab spinner { color: #ff8a62; }"
        ".load-progress { min-width: 0; min-height: 2px; background: transparent; }"
        ".find-bar { padding: 6px; border: 1px solid #4a4844; border-radius: 9px; background: #2b2a29; box-shadow: 0 7px 20px #0009; }"
        ".find-bar entry { min-width: 240px; min-height: 30px; padding: 0 9px; border: 0; border-radius: 6px; background: #222120; color: #fff; box-shadow: none; }"
        ".find-bar entry:focus { border-color: #ff8a62; }.find-bar label { color: #aaa59c; min-width: 54px; }"
        ".find-bar button { min-width: 28px; min-height: 28px; padding: 2px; border: 0; border-radius: 6px; background: transparent; color: #d8d4cc; box-shadow: none; }"
        ".find-bar button:hover { background: #3a3936; color: #ff8a62; }"
    );
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(window),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void create_window(ApplicationState *owner, const std::string &initial_uri, bool smoke,
                   WindowState *source, bool private_mode) {
    auto owned_state = std::make_unique<WindowState>();
    auto *state = owned_state.get();
    state->owner = owner;
    state->application = owner->application;
    state->smoke = smoke;
    state->private_mode = private_mode;
    if (private_mode) state->private_session = webkit_network_session_new_ephemeral();
    owner->windows.push_back(std::move(owned_state));

    state->window = gtk_application_window_new(owner->application);
    g_signal_connect(state->window, "close-request", G_CALLBACK(window_closing), state);
    gtk_window_set_title(GTK_WINDOW(state->window), private_mode ? "Vantage Private" : "Vantage Browser");
    const int source_width = source ? gtk_widget_get_width(source->window) : 0;
    const int source_height = source ? gtk_widget_get_height(source->window) : 0;
    gtk_window_set_default_size(GTK_WINDOW(state->window),
        source_width > 0 ? source_width : 1100, source_height > 0 ? source_height : 760);

    auto *header = gtk_header_bar_new();
    if (private_mode) gtk_widget_add_css_class(header, "private-header");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    auto *tab_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(tab_strip, "tab-strip");
    state->tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    auto *new_button = gtk_button_new();
    gtk_widget_add_css_class(new_button, "flat");
    gtk_widget_set_tooltip_text(new_button, "New tab");
    gtk_widget_add_css_class(new_button, "new-tab");
    auto *new_tab_content = gtk_overlay_new();
    state->new_tab_backdrop = gtk_drawing_area_new();
    gtk_widget_set_size_request(state->new_tab_backdrop, 24, 24);
    gtk_widget_set_can_target(state->new_tab_backdrop, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->new_tab_backdrop),
        draw_new_tab_backdrop, state, nullptr);
    gtk_overlay_set_child(GTK_OVERLAY(new_tab_content), state->new_tab_backdrop);
    auto *new_tab_icon = gtk_image_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_can_target(new_tab_icon, FALSE);
    gtk_widget_set_halign(new_tab_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(new_tab_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(new_tab_icon, 2);
    gtk_overlay_add_overlay(GTK_OVERLAY(new_tab_content), new_tab_icon);
    gtk_button_set_child(GTK_BUTTON(new_button), new_tab_content);
    auto *new_tab_motion = gtk_event_controller_motion_new();
    g_signal_connect(new_tab_motion, "enter", G_CALLBACK(new_tab_pointer_entered), state);
    g_signal_connect(new_tab_motion, "leave", G_CALLBACK(new_tab_pointer_left), state);
    gtk_widget_add_controller(new_button, new_tab_motion);
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
    g_signal_connect(header_middle, "released", G_CALLBACK(header_middle_released), state);
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
    gtk_widget_set_size_request(state->reload_stack, 20, 20);
    gtk_stack_set_hhomogeneous(GTK_STACK(state->reload_stack), TRUE);
    gtk_stack_set_vhomogeneous(GTK_STACK(state->reload_stack), TRUE);
    state->reload_icon = drawn_icon(ToolbarIcon::reload);
    state->stop_icon = gtk_label_new("×");
    gtk_widget_add_css_class(state->stop_icon, "stop-icon");
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
    auto *address_wrap = gtk_overlay_new();
    gtk_widget_add_css_class(address_wrap, "address-wrap");
    gtk_widget_set_hexpand(address_wrap, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(address_wrap), state->address);
    state->address_popover = gtk_popover_new();
    gtk_widget_add_css_class(state->address_popover, "address-suggestions");
    gtk_popover_set_position(GTK_POPOVER(state->address_popover), GTK_POS_BOTTOM);
    gtk_popover_set_has_arrow(GTK_POPOVER(state->address_popover), FALSE);
    state->address_suggestions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_popover_set_child(GTK_POPOVER(state->address_popover), state->address_suggestions);
    gtk_widget_set_parent(state->address_popover, address_wrap);
    state->bookmark_button = icon_button("non-starred-symbolic", "Bookmark this tab");
    gtk_widget_add_css_class(state->bookmark_button, "address-bookmark");
    gtk_widget_set_halign(state->bookmark_button, GTK_ALIGN_END);
    gtk_widget_set_valign(state->bookmark_button, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(address_wrap), state->bookmark_button);
    gtk_box_append(GTK_BOX(toolbar), address_wrap);
    if (private_mode) {
        auto *privacy = icon_button("security-high-symbolic", "Private window: history and site data are not saved");
        gtk_widget_add_css_class(privacy, "privacy-indicator");
        gtk_widget_set_sensitive(privacy, FALSE);
        gtk_box_append(GTK_BOX(toolbar), privacy);
    }
    state->downloads_button = gtk_menu_button_new();
    gtk_widget_add_css_class(state->downloads_button, "flat");
    gtk_widget_set_tooltip_text(state->downloads_button, "Downloads");
    state->downloads_stack = gtk_stack_new();
    gtk_widget_set_size_request(state->downloads_stack, 20, 20);
    state->downloads_icon = gtk_image_new_from_icon_name("folder-download-symbolic");
    state->downloads_spinner = gtk_spinner_new();
    gtk_widget_add_css_class(state->downloads_spinner, "download-spinner");
    gtk_stack_add_child(GTK_STACK(state->downloads_stack), state->downloads_icon);
    gtk_stack_add_child(GTK_STACK(state->downloads_stack), state->downloads_spinner);
    gtk_stack_set_visible_child(GTK_STACK(state->downloads_stack), state->downloads_icon);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(state->downloads_button), state->downloads_stack);
    state->downloads_popover = gtk_popover_new();
    gtk_widget_add_css_class(state->downloads_popover, "downloads-popover");
    state->downloads_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(state->downloads_box, 360, -1);
    gtk_popover_set_child(GTK_POPOVER(state->downloads_popover), state->downloads_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(state->downloads_button), state->downloads_popover);
    g_signal_connect(state->downloads_popover, "notify::visible", G_CALLBACK(downloads_visibility_changed), state);
    gtk_box_append(GTK_BOX(toolbar), state->downloads_button);
    state->menu_button = gtk_menu_button_new();
    gtk_widget_add_css_class(state->menu_button, "flat");
    gtk_widget_set_tooltip_text(state->menu_button, "Customize and control Vantage");
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(state->menu_button), "view-more-symbolic");
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(state->menu_button), create_main_menu(state));
    gtk_box_append(GTK_BOX(toolbar), state->menu_button);

    state->progress = gtk_drawing_area_new();
    gtk_widget_set_size_request(state->progress, -1, 2);
    gtk_widget_set_hexpand(state->progress, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->progress), draw_load_progress, state, nullptr);
    gtk_widget_add_css_class(state->progress, "load-progress");
    gtk_widget_set_opacity(state->progress, 0.0);
    auto *navigation = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(navigation, "navigation");
    gtk_box_append(GTK_BOX(navigation), toolbar);
    gtk_box_append(GTK_BOX(navigation), state->progress);

    state->find_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(state->find_bar, "find-bar");
    gtk_widget_set_halign(state->find_bar, GTK_ALIGN_END);
    gtk_widget_set_valign(state->find_bar, GTK_ALIGN_START);
    gtk_widget_set_margin_top(state->find_bar, 10);
    gtk_widget_set_margin_end(state->find_bar, 10);
    state->find_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->find_entry), "Find in page");
    state->find_count = gtk_label_new("0 / 0");
    auto *find_up = gtk_button_new_from_icon_name("pan-up-symbolic");
    auto *find_down = gtk_button_new_from_icon_name("pan-down-symbolic");
    auto *find_close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(find_up, "Previous match");
    gtk_widget_set_tooltip_text(find_down, "Next match");
    gtk_widget_set_tooltip_text(find_close, "Close");
    gtk_box_append(GTK_BOX(state->find_bar), state->find_entry);
    gtk_box_append(GTK_BOX(state->find_bar), state->find_count);
    gtk_box_append(GTK_BOX(state->find_bar), find_up);
    gtk_box_append(GTK_BOX(state->find_bar), find_down);
    gtk_box_append(GTK_BOX(state->find_bar), find_close);
    gtk_widget_set_visible(state->find_bar, FALSE);

    state->stack = gtk_stack_new();
    gtk_widget_set_vexpand(state->stack, TRUE);
    auto *page_overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(page_overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(page_overlay), state->stack);
    gtk_overlay_add_overlay(GTK_OVERLAY(page_overlay), state->find_bar);
    gtk_box_append(GTK_BOX(layout), navigation);
    gtk_box_append(GTK_BOX(layout), page_overlay);
    gtk_window_set_child(GTK_WINDOW(state->window), layout);
    install_style(state->window);

    g_signal_connect(back, "clicked", G_CALLBACK(go_back), state);
    g_signal_connect(forward, "clicked", G_CALLBACK(go_forward), state);
    g_signal_connect(state->reload_stop, "clicked", G_CALLBACK(reload_or_stop), state);
    g_signal_connect(state->address, "activate", G_CALLBACK(submit_address), state);
    g_signal_connect(state->address, "changed", G_CALLBACK(address_changed), state);
    g_signal_connect(state->address, "notify::has-focus", G_CALLBACK(address_focus_changed), state);
    auto *address_click = gtk_gesture_click_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(address_click), GTK_PHASE_BUBBLE);
    g_signal_connect(address_click, "pressed", G_CALLBACK(address_clicked), state);
    gtk_widget_add_controller(state->address, GTK_EVENT_CONTROLLER(address_click));
    g_signal_connect(state->bookmark_button, "clicked", G_CALLBACK(toggle_bookmark), state);
    g_signal_connect(state->find_entry, "changed", G_CALLBACK(find_changed), state);
    g_signal_connect(state->find_entry, "activate", G_CALLBACK(find_activate), state);
    g_signal_connect(find_up, "clicked", G_CALLBACK(find_previous), state);
    g_signal_connect(find_down, "clicked", G_CALLBACK(find_next), state);
    g_signal_connect(find_close, "clicked", G_CALLBACK(close_find), state);
    g_signal_connect(new_button, "clicked", G_CALLBACK(add_tab), state);
    auto *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), state);
    gtk_widget_add_controller(state->window, keys);

    auto *tab = new_tab(state, initial_uri);
    auto *network_session = webkit_web_view_get_network_session(tab->view);
    if (!g_object_get_data(G_OBJECT(network_session), "vantage-download-handler")) {
        g_signal_connect(network_session, "download-started", G_CALLBACK(download_started), owner);
        g_object_set_data(G_OBJECT(network_session), "vantage-download-handler", owner);
    }
    gtk_window_present(GTK_WINDOW(state->window));
    if (initial_uri.starts_with("vantage:") || initial_uri.starts_with("about:")) {
        gtk_widget_grab_focus(state->address);
    }
    if (state->smoke) {
        webkit_web_view_load_html(tab->view, "<!doctype html><title>Vant smoke</title><p>ok</p>", "https://smoke.invalid/");
        g_timeout_add(900, finish_smoke, state);
    }
}

void activate(GtkApplication *application, void *user_data) {
    auto *owner = static_cast<ApplicationState *>(user_data);
    owner->application = application;
    create_window(owner, owner->initial_uri, owner->smoke, nullptr, false);
    owner->smoke = false;
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
    ApplicationState state;
    state.application = application.get();
    state.initial_uri = initial_uri;
    state.smoke = smoke;
    state.data = std::make_unique<UserDataStore>(
        std::filesystem::path(g_get_user_data_dir()) / "vantage-browser" / "browser.sqlite3", smoke);
    state.data->reconcile_downloads();
    g_signal_connect(application.get(), "activate", G_CALLBACK(activate), &state);
    return g_application_run(G_APPLICATION(application.get()), 0, nullptr);
}

} // namespace vantage
