// Container for litehtml rendering using xwin/graph API

#include "XContainer.h"
#include "WidgetWebview/WidgetWebview.h"
#include "el_input.h"

#include <graph/graph_ex.h>
#include <graph/graph_image.h>
#include <ewoksys/basic_math.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/klog.h>
#include <x++/X.h>
#include <tinyhttpsc/tinyhttpsc.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

using namespace litehtml;
using namespace Ewok;

namespace {

static uint64_t make_char_width_key(const FontInfo* fontInfo, uint32_t codepoint)
{
    uintptr_t font_ptr = reinterpret_cast<uintptr_t>(fontInfo->font);
    uint64_t font_hash = ((uint64_t)(font_ptr >> 4)) & 0xFFFFFFULL;
    return (((uint64_t)(fontInfo->size & 0xFFFF)) << 48) |
           (((uint64_t)(codepoint & 0xFFFFFF)) << 24) |
           font_hash;
}

static bool next_utf8_codepoint(const char*& p, uint32_t& codepoint)
{
    unsigned char c0 = (unsigned char)*p;
    if(c0 == 0) {
        return false;
    }

    if(c0 < 0x80) {
        codepoint = c0;
        ++p;
        return true;
    }

    if((c0 & 0xE0) == 0xC0) {
        unsigned char c1 = (unsigned char)p[1];
        if((c1 & 0xC0) != 0x80) {
            codepoint = c0;
            ++p;
            return true;
        }
        codepoint = ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
        p += 2;
        return true;
    }

    if((c0 & 0xF0) == 0xE0) {
        unsigned char c1 = (unsigned char)p[1];
        unsigned char c2 = (unsigned char)p[2];
        if((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
            codepoint = c0;
            ++p;
            return true;
        }
        codepoint = ((uint32_t)(c0 & 0x0F) << 12) |
                    ((uint32_t)(c1 & 0x3F) << 6) |
                    (uint32_t)(c2 & 0x3F);
        p += 3;
        return true;
    }

    if((c0 & 0xF8) == 0xF0) {
        unsigned char c1 = (unsigned char)p[1];
        unsigned char c2 = (unsigned char)p[2];
        unsigned char c3 = (unsigned char)p[3];
        if((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
            codepoint = c0;
            ++p;
            return true;
        }
        codepoint = ((uint32_t)(c0 & 0x07) << 18) |
                    ((uint32_t)(c1 & 0x3F) << 12) |
                    ((uint32_t)(c2 & 0x3F) << 6) |
                    (uint32_t)(c3 & 0x3F);
        p += 4;
        return true;
    }

    codepoint = c0;
    ++p;
    return true;
}

static std::string trim_request_url(const std::string& url)
{
    size_t begin = 0;
    size_t end = url.size();

    while(begin < end) {
        char ch = url[begin];
        if(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++begin;
            continue;
        }
        break;
    }

    while(end > begin) {
        char ch = url[end - 1];
        if(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            --end;
            continue;
        }
        break;
    }

    if(end > begin + 1) {
        char first = url[begin];
        char last = url[end - 1];
        if((first == '"' && last == '"') ||
           (first == '\'' && last == '\'') ||
           (first == '`' && last == '`')) {
            ++begin;
            --end;
        }
    }

    return url.substr(begin, end - begin);
}

}

static inline int char_width_cache_slot(uint64_t key)
{
    return (int)(key & 8191ULL);
}

XContainer::XContainer(litehtml::context* html_context, WidgetWebview* webview)
{
    m_g = NULL;
    m_webview = webview;
    m_client_width = 640;
    m_client_height = 480;
    m_text_width_calls = 0;
    m_text_width_ms = 0;
    m_draw_text_calls = 0;
    m_draw_text_ms = 0;
    m_text_width_hits = 0;
    m_text_width_misses = 0;
    m_char_width_hits = 0;
    m_char_width_misses = 0;
    m_create_font_calls = 0;
    m_create_font_ms = 0;
    m_defer_image_load = false;
    memset(m_char_width_keys, 0, sizeof(m_char_width_keys));
    memset(m_char_width_vals, 0, sizeof(m_char_width_vals));
}

XContainer::~XContainer(void)
{
    for (auto& pair : m_fonts) {
        if (pair.second.font) {
            font_free(pair.second.font);
        }
    }
    m_fonts.clear();

    for (auto& pair : m_images) {
        if (pair.second.image) {
            graph_free(pair.second.image);
        }
    }
    m_images.clear();
}

uint32_t XContainer::web_color_to_graph(const litehtml::web_color& c)
{
    return (c.alpha << 24) | (c.red << 16) | (c.green << 8) | c.blue;
}

litehtml::uint_ptr XContainer::create_font(const litehtml::tchar_t* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm)
{
    uint64_t start_ms = kernel_tic_ms(0);
    std::string fontName = "system-cn";
    std::string key = "system-cn";

    if(weight >= 700) {
        fontName = "system-cn";
        key = "system-cn";
    }

    key += "-" + std::to_string(size) + "px";

    FontInfo fontInfo;
    fontInfo.font = NULL;
    fontInfo.size = size;

    if (m_fonts.find(key) != m_fonts.end()) {
        fontInfo = m_fonts[key];
    } else {
        fontInfo.font = font_new(fontName.c_str(), false);
        fontInfo.size = size;
        if (fontInfo.font == NULL) {
            m_fonts[key] = fontInfo;
            return 0;
        }
        m_fonts[key] = fontInfo;
    }

    if(fontInfo.font == NULL) {
        m_create_font_calls++;
        m_create_font_ms += (uint32_t)(kernel_tic_ms(0) - start_ms);
        return 0;
    }

    if (fm) {
        face_info_t face;
        if (font_get_face(fontInfo.font, size, &face) == 0) {
            // FreeType metrics are in 26.6 fixed-point format, divide by 64 to get pixels
            const int FACE_PIXEL_DENT = 64;
            uint32_t x_height = 0;
            fm->ascent = face.ascender / FACE_PIXEL_DENT;
            fm->descent = face.descender / FACE_PIXEL_DENT;
            fm->height = face.height / FACE_PIXEL_DENT;
            font_char_size('x', fontInfo.font, size, &x_height, NULL);
            fm->x_height = (int)x_height;
            fm->draw_spaces = italic == fontStyleItalic || decoration;
        }
    }

    m_create_font_calls++;
    m_create_font_ms += (uint32_t)(kernel_tic_ms(0) - start_ms);
    return (uint_ptr)&m_fonts[key];
}

void XContainer::delete_font(litehtml::uint_ptr hFont)
{
    return;
}

int XContainer::text_width(const litehtml::tchar_t* text, litehtml::uint_ptr hFont)
{
    FontInfo* fontInfo = (FontInfo*)hFont;

    if(!fontInfo || !fontInfo->font || !text) {
        return 0;
    }

    if(!text[0]) {
        return 0;
    }

    m_text_width_calls++;
    uint64_t start_ms = kernel_tic_ms(0);
    const char* p = text;
    uint32_t w = 0;
    bool cache_hit_only = true;
    while(*p) {
        uint32_t codepoint = 0;
        if(!next_utf8_codepoint(p, codepoint)) {
            break;
        }

        uint64_t char_key = make_char_width_key(fontInfo, codepoint);
        int slot = char_width_cache_slot(char_key);
        if(m_char_width_keys[slot] == char_key) {
            m_char_width_hits++;
            w += (uint32_t)m_char_width_vals[slot];
            continue;
        }

        cache_hit_only = false;
        m_char_width_misses++;
        uint32_t cw = 0;
        font_char_size(codepoint, fontInfo->font, fontInfo->size, &cw, NULL);
        m_char_width_keys[slot] = char_key;
        m_char_width_vals[slot] = (int)cw;
        w += cw;
    }

    m_text_width_ms += (uint32_t)(kernel_tic_ms(0) - start_ms);
    if(cache_hit_only) {
        m_text_width_hits++;
    } else {
        m_text_width_misses++;
    }
    return w;
}

void XContainer::draw_text(litehtml::uint_ptr hdc, const litehtml::tchar_t* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos)
{
    FontInfo* fontInfo = (FontInfo*)hFont;
    if (!fontInfo || !fontInfo->font || !text) {
        return;
    }

    graph_t* g = m_g;
    if (!g) {
        return;
    }

    uint32_t graph_color = web_color_to_graph(color);

    // Force opaque color if alpha is 0
    if ((graph_color >> 24) == 0) {
        graph_color = 0xFF000000 | (graph_color & 0xFFFFFF);
    }

    uint64_t start_ms = kernel_tic_ms(0);
    graph_draw_text_font(g, pos.x, pos.y, text, fontInfo->font, fontInfo->size, graph_color);
    m_draw_text_calls++;
    m_draw_text_ms += (uint32_t)(kernel_tic_ms(0) - start_ms);
}

void XContainer::resetPerfStats()
{
    m_text_width_calls = 0;
    m_text_width_ms = 0;
    m_draw_text_calls = 0;
    m_draw_text_ms = 0;
    m_text_width_hits = 0;
    m_text_width_misses = 0;
    m_char_width_hits = 0;
    m_char_width_misses = 0;
    m_create_font_calls = 0;
    m_create_font_ms = 0;
}

void XContainer::getPerfStats(uint32_t& textWidthCalls, uint32_t& textWidthMs,
                              uint32_t& drawTextCalls, uint32_t& drawTextMs,
                              uint32_t& textWidthHits, uint32_t& textWidthMisses,
                              uint32_t& charWidthHits, uint32_t& charWidthMisses,
                              uint32_t& createFontCalls, uint32_t& createFontMs) const
{
    textWidthCalls = m_text_width_calls;
    textWidthMs = m_text_width_ms;
    drawTextCalls = m_draw_text_calls;
    drawTextMs = m_draw_text_ms;
    textWidthHits = m_text_width_hits;
    textWidthMisses = m_text_width_misses;
    charWidthHits = m_char_width_hits;
    charWidthMisses = m_char_width_misses;
    createFontCalls = m_create_font_calls;
    createFontMs = m_create_font_ms;
}

int XContainer::pt_to_px(int pt)
{
    return pt;
}

int XContainer::get_default_font_size() const
{
    return 16;
}

const litehtml::tchar_t* XContainer::get_default_font_name() const
{
    return _t("sans-serif");
}

void XContainer::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker)
{
    graph_t* g = m_g;
    if (!g)
        return;

    if (!marker.image.empty())
    {
    }
    else
    {
        uint32_t color = web_color_to_graph(marker.color);

        switch (marker.marker_type)
        {
        case litehtml::list_style_type_circle:
            graph_circle(g, marker.pos.x, marker.pos.y, marker.pos.width, 1, color);
            break;
        case litehtml::list_style_type_disc:
            graph_fill_circle(g, marker.pos.x, marker.pos.y, marker.pos.width, color);
            break;
        case litehtml::list_style_type_square:
            graph_fill_rect(g, marker.pos.x, marker.pos.y, marker.pos.width, marker.pos.height, color);
            break;
        }
    }
}

const std::string XContainer::getFullURL(const std::string& src, const std::string& baseurl) {
    std::string clean_src = trim_request_url(src);
    std::string clean_baseurl = trim_request_url(baseurl);
    std::string path = clean_src;
    if(clean_src.starts_with("file://") || 
            clean_src.starts_with("http://") ||
            clean_src.starts_with("https://")) {
        return clean_src;
    }
    else if(clean_src.starts_with("res://")) {
        path = "file:/";
        path += X::getResFullName(clean_src.substr(6).c_str());
        return path;
    }
    else if(clean_src.starts_with("//")) {
        // Protocol-relative URL: //example.com/path
        // Use https by default, or http if baseurl uses http
        if(clean_baseurl.starts_with("http://")) {
            path = "http:" + clean_src;
        } else {
            path = "https:" + clean_src;
        }
        return path;
    }

    if (!clean_baseurl.empty()) {
        size_t slash = clean_baseurl.find_last_of('/');
        std::string base_dir = slash == std::string::npos ? clean_baseurl : clean_baseurl.substr(0, slash + 1);
        if(!base_dir.empty())
            path = base_dir + path;
    }
    return path;
}

std::string XContainer::normalizeURL(const std::string& url, const std::string& baseurl)
{
    return getFullURL(url, baseurl);
}

uint8_t* XContainer::loadURL(const std::string& url, int* sz)
{
    uint8_t* ret = NULL;
    if(sz != NULL)
        *sz = 0;

    std::string full_url = getFullURL(url, "");
    klog("[xBrowser] loadURL: %s -> %s\n", url.c_str(), full_url.c_str());
    if(full_url.starts_with("file://")) {
        std::string path = full_url.substr(6);
        if(!path.empty()) { //local file
            uint64_t start_ms = kernel_tic_ms(0);
            ret = vfs_readfile(path.c_str(), sz);
            uint32_t cost_ms = (uint32_t)(kernel_tic_ms(0) - start_ms);
            klog("[xBrowser] loadURL file: path=%s ok=%d size=%d cost=%u ms\n",
                path.c_str(), ret != NULL ? 1 : 0, sz != NULL ? *sz : 0, cost_ms);
            return ret;
        }
    }
    else if(full_url.starts_with("http://") || full_url.starts_with("https://")) {
        // Use tinyhttpsc to fetch HTTP/HTTPS URL
        uint64_t start_ms = kernel_tic_ms(0);
        std::string request_url = trim_request_url(full_url);
        TinyHttpsRequest* request = NewHttpsRequest(request_url.c_str());
        if(request == NULL) {
            klog("[xBrowser] loadURL http: create request failed url=%s\n", request_url.c_str());
            return NULL;
        }

        // Set timeout and max redirections
        HttpsRequestSetTimeout(request, 10000); // 10 seconds
        HttpsRequestSetMaxRedirections(request, 5);
        HttpsRequestAddHeader(request, "User-Agent", "ewokos-xbrowser/1");

        // Execute request
        TinyHttpsResponse* response = HttpsRequestFetch(request);

        if(response == NULL) {
            klog("[xBrowser] loadURL http: null response url=%s cost=%u ms\n",
                request_url.c_str(), (uint32_t)(kernel_tic_ms(0) - start_ms));
            HttpsRequestFree(request);
            return NULL;
        }

        int status_code = HttpsResponseGetStatusCode(response);
        int error_code = HttpsResponseGetErrorCode(response);
        int header_body_size = HttpsResponseGetBodySize(response);
        const char* error_msg = HttpsResponseGetErrorMsg(response);
        const char* content_type = HttpsResponseGetHeaderValueByKey(response, "content-type");
        const char* location = HttpsResponseGetHeaderValueByKey(response, "location");
        std::string error_msg_str = error_msg ? error_msg : "-";
        std::string content_type_str = content_type ? content_type : "-";
        std::string location_str = location ? location : "-";
        bool has_error = HttpsResponseError(response);

        if(has_error) {
            klog("[xBrowser] loadURL http error: url=%s status=%d err=%d body=%d type=%s location=%s msg=%s cost=%u ms\n",
                request_url.c_str(),
                status_code,
                error_code,
                header_body_size,
                content_type_str.c_str(),
                location_str.c_str(),
                error_msg_str.c_str(),
                (uint32_t)(kernel_tic_ms(0) - start_ms));
            HttpsResponseFree(response);
            HttpsRequestFree(request);
            return NULL;
        }

        if(status_code != 200) {
            klog("[xBrowser] loadURL http status: url=%s status=%d err=%d body=%d type=%s location=%s cost=%u ms\n",
                request_url.c_str(),
                status_code,
                error_code,
                header_body_size,
                content_type_str.c_str(),
                location_str.c_str(),
                (uint32_t)(kernel_tic_ms(0) - start_ms));
            HttpsResponseFree(response);
            HttpsRequestFree(request);
            return NULL;
        }

        // Read response body first (this will populate the body size)
        int body_size = 0;
        const char* body = HttpsResponseReadBody(response, &body_size);
        if(body == NULL || body_size <= 0) {
            klog("[xBrowser] loadURL http empty body: url=%s status=%d err=%d hdr_body=%d body=%d type=%s cost=%u ms\n",
                request_url.c_str(),
                status_code,
                error_code,
                header_body_size,
                body_size,
                content_type_str.c_str(),
                (uint32_t)(kernel_tic_ms(0) - start_ms));
            HttpsResponseFree(response);
            HttpsRequestFree(request);
            return NULL;
        }

        // Allocate memory and copy data
        ret = (uint8_t*)malloc(body_size+1);
        if(ret == NULL) {
            klog("[xBrowser] loadURL http: alloc failed url=%s body=%d\n", request_url.c_str(), body_size);
            HttpsResponseFree(response);
            HttpsRequestFree(request);
            return NULL;
        }

        memcpy(ret, body, body_size);
        HttpsResponseFree(response);
        HttpsRequestFree(request);
        ret[body_size] = 0;

        if(sz != NULL)
            *sz = body_size;
        klog("[xBrowser] loadURL http: url=%s status=%d size=%d type=%s cost=%u ms\n",
            request_url.c_str(),
            status_code,
            body_size,
            content_type_str.c_str(),
            (uint32_t)(kernel_tic_ms(0) - start_ms));
        return ret;
    }
    klog("[xBrowser] loadURL failed: %s\n", full_url.c_str());
    return NULL;
}


void XContainer::load_image(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, bool redraw_on_ready)
{
    (void)redraw_on_ready;
    if (src == NULL || src[0] == 0)
        return;

    std::string img_path = std::string(src);
    std::string base_url = baseurl ? std::string(baseurl) : std::string();
    std::string full_url = getFullURL(img_path, base_url);
    if(full_url.empty())
        return; 
    
    auto it = m_images.find(full_url);
    if (it != m_images.end()) {
        it->second.ref_count++;
        return;
    }

    if(m_defer_image_load) {
        for(const auto& pending_url : m_pending_image_urls) {
            if(pending_url == full_url) {
                return;
            }
        }
        m_pending_image_urls.push_back(full_url);
        return;
    }

    m_webview->addTask( { full_url,  HttpTask::TASK_IMAGE, false } );
}

void XContainer::setDeferImageLoad(bool defer)
{
    m_defer_image_load = defer;
    klog("[xBrowser] image defer: %d pending=%d\n", defer ? 1 : 0, (int)m_pending_image_urls.size());
}

void XContainer::flushPendingImages()
{
    if(m_pending_image_urls.empty()) {
        klog("[xBrowser] flush pending images: 0\n");
        return;
    }

    std::vector<std::string> pending_urls = m_pending_image_urls;
    klog("[xBrowser] flush pending images: %d\n", (int)pending_urls.size());
    std::vector<std::string> retry_urls;
    int queued_count = 0;
    for(const auto& url : pending_urls) {
        if(m_webview->addTask({ url, HttpTask::TASK_IMAGE, false })) {
            queued_count++;
        } else {
            retry_urls.push_back(url);
        }
    }
    m_pending_image_urls = retry_urls;
    klog("[xBrowser] flush pending images result: queued=%d retry=%d left=%d\n",
        queued_count, (int)retry_urls.size(), (int)m_pending_image_urls.size());
}

bool XContainer::loadImageData(const std::string& url, uint8_t* data, int sz)
{
    if (data == NULL || sz <= 0 || url.empty())
        return false;

    graph_t* img = graph_image_new_from_data(GRAPH_IMAGE_TYPE_AUTO, data, sz);
    if (img == NULL) {
        klog("[xBrowser] image decode failed: url=%s size=%d\n", url.c_str(), sz);
        return false;
    }

    ImageInfo info;
    info.image = img;
    info.ref_count = 1;
    m_images[url] = info;
    klog("[xBrowser] image cached: url=%s size=%d dim=%dx%d ref=%d\n",
        url.c_str(), sz, img->w, img->h, info.ref_count);
    return true;
}

void XContainer::get_image_size(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, litehtml::size& sz)
{
    sz.width = 0;
    sz.height = 0;

    if (src == NULL || src[0] == 0)
        return;

    std::string img_path = std::string(src);
    std::string base_url = baseurl ? std::string(baseurl) : std::string();
    std::string full_url = getFullURL(img_path, base_url);
    if(full_url.empty())
        return;
    
    auto it = m_images.find(full_url);
    if (it != m_images.end() && it->second.image != NULL) {
        sz.width = it->second.image->w;
        sz.height = it->second.image->h;
        return;
    }
    return;
}

void XContainer::draw_background(litehtml::uint_ptr hdc, const litehtml::background_paint& bg)
{
    graph_t* g = m_g;
    if (!g)
        return;

    bool do_image = false;
    if (!bg.image.empty() && bg.image_size.width > 0 && bg.image_size.height > 0) {
        std::string full_url = getFullURL(bg.image, m_base_url);
        if (!full_url.empty()) {
            auto it = m_images.find(full_url);
            graph_t* img = NULL;
            if (it != m_images.end() && it->second.image != NULL) {
                img = it->second.image;
            } 

            if (img != NULL) {
                graph_blt_fit_alpha(img, 0, 0, img->w, img->h,
                    g, bg.clip_box.x, bg.clip_box.y, bg.clip_box.width, bg.clip_box.height, 0xFF);
                return;
            }
        }
        do_image = true;
    }

    uint32_t color = web_color_to_graph(bg.color);
    uint8_t alpha = color >> 24;

    if(!do_image) {
        if(alpha == 0)
            return;
        graph_fill_rect(g, bg.clip_box.x, bg.clip_box.y, bg.clip_box.width, bg.clip_box.height, color);
    } else {
        // Keep image boxes visible before the real bitmap arrives so HTML can
        // render immediately and swap the image in on a later repaint.
        uint32_t fill = alpha != 0 ? color : 0xFFE8E8E8;
        uint32_t stroke = alpha != 0 ? color : 0xFFB0B0B0;
        graph_fill_rect(g, bg.clip_box.x, bg.clip_box.y, bg.clip_box.width, bg.clip_box.height, fill);
        graph_rect(g, bg.clip_box.x, bg.clip_box.y, bg.clip_box.width, bg.clip_box.height, stroke);
    }
}

void XContainer::draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root)
{
    graph_t* g = m_g;
    if (!g)
        return;

    if (borders.top.width != 0 && borders.top.style > litehtml::border_style_hidden) {
        uint32_t color = web_color_to_graph(borders.top.color);
        graph_rect(g, draw_pos.x, draw_pos.y, draw_pos.width, draw_pos.height, color);
    }
}

void XContainer::transform_text(litehtml::tstring& text, litehtml::text_transform tt)
{
}

void XContainer::set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius, bool valid_x, bool valid_y)
{
}

void XContainer::del_clip()
{
}

void XContainer::clear_images()
{
    for (auto& pair : m_images) {
        if (pair.second.image) {
            graph_free(pair.second.image);
        }
    }
    m_images.clear();
}

void XContainer::clear_inputs()
{
    // Inputs are DOM-owned elements. XContainer only keeps non-owning references.
    m_vecInput.clear();
}

void XContainer::get_client_rect(litehtml::position& client) const
{
    client.width = m_client_width;
    client.height = m_client_height;
}

void XContainer::set_client_size(int width, int height)
{
    m_client_width = width;
    m_client_height = height;
}

void XContainer::on_anchor_click(const litehtml::tchar_t* url, const litehtml::element::ptr& el)
{
}

void XContainer::set_cursor(const litehtml::tchar_t* cursor)
{
}

void XContainer::import_css(litehtml::tstring& text, const litehtml::tstring& url, litehtml::tstring& baseurl)
{
    //klog("import_css: url:%s baseurl:%s text:%s\n", url.c_str(), baseurl.c_str(), text.c_str());
    if (url.empty())
        return;

    std::string css_path = std::string(url);
    std::string base_url = std::string(baseurl);
    std::string full_url = getFullURL(css_path, base_url);
    if(full_url.empty())
        return;
    m_webview->loadCSS(full_url);
}

void XContainer::set_caption(const litehtml::tchar_t* caption)
{
}

void XContainer::set_base_url(const litehtml::tchar_t* base_url)
{
    if (base_url != NULL) {
        m_base_url = std::string(base_url);
    } else {
        m_base_url.clear();
    }
}

litehtml::element* XContainer::create_element(const litehtml::tchar_t* tag_name,
                                    const litehtml::string_map& attributes,
                                    litehtml::document* doc)
{
    if (!t_strcasecmp(tag_name, _t("input"))) {
        auto iter = attributes.find(_t("type"));
        if (iter != attributes.end()) {
            if (!t_strcasecmp(iter->second.c_str(), _t("text"))) {
                auto input = new el_input(doc, this, HtmlInputType::TEXT);
                m_vecInput.push_back(input);
                return input;
            }
            else if (!t_strcasecmp(iter->second.c_str(), _t("button"))) {
                auto input = new el_input(doc, this, HtmlInputType::BUTTON);
                m_vecInput.push_back(input);
                return input;
            }
        }
    }
    return NULL;
}

void XContainer::get_media_features(litehtml::media_features& media) const
{
    litehtml::position client;
    get_client_rect(client);
    media.type       = litehtml::media_type_screen;
    media.width      = client.width;
    media.height     = client.height;
    media.device_width  = 640;
    media.device_height = 480;
    media.color     = 8;
    media.monochrome  = 0;
    media.color_index = 256;
    media.resolution  = 96;
}

void XContainer::get_language(litehtml::tstring& language, litehtml::tstring& culture) const
{
    language = _t("en");
    culture = _t("");
}

void XContainer::link(litehtml::document* ptr, const litehtml::element::ptr& el)
{
}
