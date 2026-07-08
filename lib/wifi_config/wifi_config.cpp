#include "wifi_config.h"

#include <DNSServer.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "pins.h"
#include "storage.h"  // SELF_JSON, SELF_BIN — self profile paths in /self_profile/


static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);
static const uint16_t  DNS_PORT = 53;
static const char     *WEB_ROOT = "/web/profile-generator.html";
static const char     *WEB_JSZIP = "/web/jszip.min.js";

static WebServer s_server(80);
static DNSServer s_dns;
static File      s_upload;

static bool     s_active = false;
static bool     s_connected = false;
static bool     s_done = false;
static bool     s_got_json = false;
static bool     s_got_bin = false;
static uint32_t s_touch = 0;

static char s_ssid[24];
static char s_pass[16];
static char s_wifi_qr[80];
static char s_url[24];

static void touch()
{
    s_touch = millis();
}

static void derive_identity()
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    snprintf(s_ssid, sizeof(s_ssid), "con-venience-%02X%02X", mac[4], mac[5]);
    snprintf(s_pass, sizeof(s_pass), "%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_wifi_qr, sizeof(s_wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", s_ssid, s_pass);
    snprintf(s_url, sizeof(s_url), "http://%u.%u.%u.%u/", AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
}

static void ensure_parent_dir(const char *path)
{
    char dir[64];
    strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir)
    {
        *slash = '\0';
        if (!SD.exists(dir))
        {
            SD.mkdir(dir);
        }
    }
}

static bool serve_sd(const char *path, const char *type)
{
    File f = SD.open(path, FILE_READ);
    if (!f)
    {
        return false;
    }
    s_server.streamFile(f, type);
    f.close();
    return true;
}

static void handle_root()
{
    touch();
    if (!serve_sd(WEB_ROOT, "text/html"))
    {
        s_server.send(500, "text/plain", "profile-generator.html missing on SD");
    }
}

static void handle_jszip()
{
    touch();
    if (!serve_sd(WEB_JSZIP, "application/javascript"))
    {
        s_server.send(404, "text/plain", "jszip missing");
    }
}

static void handle_profile_json()
{
    touch();
    if (!serve_sd(SELF_JSON, "application/json"))
    {
        s_server.send(404, "text/plain", "no profile yet");
    }
}

static void handle_avatar()
{
    touch();
    if (!serve_sd(SELF_BIN, "application/octet-stream"))
    {
        s_server.send(404, "text/plain", "no avatar yet");
    }
}

static void handle_captive()
{
    touch();
    s_server.sendHeader("Location", s_url, true);
    s_server.send(302, "text/plain", "");
}

// POST /upload — multipart with two file fields: "profile" -> SELF_JSON,
// "avatar" -> SELF_BIN. Written straight to the /self_profile/ folder; ESP
// parses nothing. "done" once both parts have landed.
static void handle_upload_body()
{
    HTTPUpload &up = s_server.upload();

    if (up.status == UPLOAD_FILE_START)
    {
        touch();
        const char *path = (up.name == "avatar") ? SELF_BIN : SELF_JSON;
        ensure_parent_dir(path);
        SD.remove(path);
        s_upload = SD.open(path, FILE_WRITE);
    }
    else if (up.status == UPLOAD_FILE_WRITE)
    {
        touch();
        if (s_upload)
        {
            s_upload.write(up.buf, up.currentSize);
        }
    }
    else if (up.status == UPLOAD_FILE_END)
    {
        if (s_upload)
        {
            s_upload.close();
        }
        if (up.name == "avatar")
        {
            s_got_bin = true;
        }
        else
        {
            s_got_json = true;
        }
        s_done = s_got_json && s_got_bin;
    }
}

static void handle_upload_done()
{
    touch();
    s_server.send(s_done ? 200 : 500, "text/plain", s_done ? "ok" : "incomplete upload");
}

static void on_wifi_event(arduino_event_id_t event)
{
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED)
    {
        s_connected = true;
        touch();
    }
}

void wifi_config_start()
{
    if (s_active)
    {
        return;
    }

    derive_identity();
    s_connected = false;
    s_done = false;
    s_got_json = false;
    s_got_bin = false;

    WiFi.onEvent(on_wifi_event);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    WiFi.softAP(s_ssid, s_pass);

    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(DNS_PORT, "*", AP_IP);

    s_server.on("/", HTTP_GET, handle_root);
    s_server.on("/jszip.min.js", HTTP_GET, handle_jszip);
    s_server.on("/profile.json", HTTP_GET, handle_profile_json);
    s_server.on("/avatar.bin", HTTP_GET, handle_avatar);
    s_server.on("/upload", HTTP_POST, handle_upload_done, handle_upload_body);
    s_server.on("/generate_204", handle_captive);
    s_server.on("/gen_204", handle_captive);
    s_server.on("/hotspot-detect.html", handle_captive);
    s_server.on("/ncsi.txt", handle_captive);
    s_server.onNotFound(handle_captive);
    s_server.begin();

    s_active = true;
    touch();
    Serial0.printf("[wifi_config] up ssid=%s pass=%s url=%s\n", s_ssid, s_pass, s_url);
}

void wifi_config_stop()
{
    if (!s_active)
    {
        return;
    }

    if (s_upload)
    {
        s_upload.close();
    }
    s_server.stop();
    s_dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    s_active = false;
    Serial0.println("[wifi_config] down");
}

void wifi_config_loop()
{
    if (!s_active)
    {
        return;
    }
    s_dns.processNextRequest();
    s_server.handleClient();
}

bool wifi_config_client_connected()
{
    return s_connected;
}
bool wifi_config_upload_done()
{
    return s_done;
}
uint32_t wifi_config_idle_ms()
{
    return millis() - s_touch;
}

const char *wifi_config_wifi_qr()
{
    return s_wifi_qr;
}
const char *wifi_config_url()
{
    return s_url;
}
const char *wifi_config_ssid()
{
    return s_ssid;
}