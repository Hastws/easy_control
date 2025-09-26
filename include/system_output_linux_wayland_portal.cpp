// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#if defined(__linux__) && defined(AUTOALG_USE_WAYLAND_PORTAL)
#include <gio/gio.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "system_output.hpp"

// Detect if WAYLAND is active
static EC_INLINE bool is_wayland() {
  const char* wl = std::getenv("WAYLAND_DISPLAY");
  return wl && *wl;
}

// stb_image for PNG decode (fetched by CMake)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

bool portal_screenshot_png(std::vector<unsigned char>& out_png) {
  if (!is_wayland()) return false;

  GError* err = nullptr;
  GDBusProxy* proxy = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS, nullptr,
      "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Screenshot", nullptr, &err);
  if (!proxy) {
    if (err) g_error_free(err);
    return false;
  }

  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&opts, "{sv}", "interactive", g_variant_new_boolean(FALSE));
  g_variant_builder_add(&opts, "{sv}", "modal", g_variant_new_boolean(FALSE));
  g_variant_builder_add(&opts, "{sv}", "include-cursor", g_variant_new_boolean(TRUE));

  GVariant* res = g_dbus_proxy_call_sync(proxy, "Screenshot", g_variant_new("(ssa{sv})", "", "", &opts), G_DBUS_CALL_FLAGS_NONE,
                                         -1, nullptr, &err);
  if (!res) {
    if (err) g_error_free(err);
    g_object_unref(proxy);
    return false;
  }

  const char* handle = nullptr;
  g_variant_get(res, "(o)", &handle);
  std::string handle_path = handle ? handle : "";
  g_variant_unref(res);
  if (handle_path.empty()) {
    g_object_unref(proxy);
    return false;
  }

  GDBusProxy* req = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, nullptr,
                                                  "org.freedesktop.portal.Desktop", handle_path.c_str(),
                                                  "org.freedesktop.portal.Request", nullptr, &err);
  if (!req) {
    if (err) g_error_free(err);
    g_object_unref(proxy);
    return false;
  }

  bool got = false;
  std::string uri;
  auto on_signal = [](GDBusProxy* /*proxy*/, gchar* /*sender*/, gchar* signal, GVariant* parameters, void* user_data) {
    if (std::string(signal) != "Response") return;
    bool* gotp = static_cast<bool*>(user_data);
    guint32 resp;
    GVariant* dict;
    g_variant_get(parameters, "(u@a{sv})", &resp, &dict);
    if (resp == 0) {
      GVariant* v = g_variant_lookup_value(dict, "uri", G_VARIANT_TYPE_STRING);
      if (v) {
        const char* uris = g_variant_get_string(v, nullptr);
        *((std::string*)(((void**)user_data) + 1)) = uris ? uris : "";
        g_variant_unref(v);
      }
    }
    g_variant_unref(dict);
    *gotp = true;
  };
  void* udata[2];
  udata[0] = &got;
  udata[1] = &uri;
  g_signal_connect(req, "g-signal", G_CALLBACK(on_signal), udata);

  for (int i = 0; i < 500 && !got; ++i) {  // ~5s
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(10 * 1000);
  }

  if (!got || uri.empty()) {
    g_object_unref(req);
    g_object_unref(proxy);
    return false;
  }

  GFile* file = g_file_new_for_uri(uri.c_str());
  if (!file) {
    g_object_unref(req);
    g_object_unref(proxy);
    return false;
  }

  GError* err2 = nullptr;
  GFileInputStream* is = g_file_read(file, nullptr, &err2);
  if (!is) {
    if (err2) g_error_free(err2);
    g_object_unref(file);
    g_object_unref(req);
    g_object_unref(proxy);
    return false;
  }

  const gsize chunk = 64 * 1024;
  std::vector<unsigned char> buf;
  buf.reserve(256 * 1024);
  while (true) {
    unsigned char tmp[chunk];
    gssize n = g_input_stream_read(G_INPUT_STREAM(is), tmp, chunk, nullptr, &err2);
    if (n < 0) {
      if (err2) g_error_free(err2);
      g_object_unref(is);
      g_object_unref(file);
      g_object_unref(req);
      g_object_unref(proxy);
      return false;
    }
    if (n == 0) break;
    buf.insert(buf.end(), tmp, tmp + n);
  }

  g_object_unref(is);
  g_object_unref(file);
  g_object_unref(req);
  g_object_unref(proxy);

  out_png.swap(buf);
  return !out_png.empty();
}

}  // namespace

namespace autoalg {

bool SystemOutput::CaptureScreenWithCursor(int displayIndex, ImageRGBA& out) {
  (void)displayIndex;
  if (!is_wayland()) return false;

  std::vector<unsigned char> png;
  if (!portal_screenshot_png(png)) return false;

  int w = 0, h = 0, n = 0;
  unsigned char* data = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &n, 4);
  if (!data) return false;

  out.width = w;
  out.height = h;
  out.pixels.assign(data, data + (size_t)w * h * 4);
  stbi_image_free(data);
  return true;
}

int SystemOutput::GetDisplayCount() {
  if (is_wayland()) return 1;  // portal abstracts monitors
  return 0;
}

std::string SystemOutput::GetDisplayInfo(int index) {
  (void)index;
  return "Linux Wayland (xdg-desktop-portal)";
}

}  // namespace autoalg
#endif