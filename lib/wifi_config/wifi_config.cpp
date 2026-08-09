#include "wifi_config.h"

#include <DNSServer.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_random.h>

#include "pins.h"
#include "storage.h"  // SELF_JSON, SELF_BIN, storageEnsureSelfId, storageEnsureParentDir

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);
static const uint16_t  DNS_PORT = 53;
static const char     *WEB_ROOT = "/web/profile-generator.html";
static const char     *WEB_JSZIP = "/web/jszip.min.js";
static const size_t    AVATAR_BYTES = 4096;       // 128x128 2bpp, fixed full frame
static const size_t    PROFILE_JSON_MAX = 65536;  // upper bound so one POST cannot fill the SD

static WebServer s_server(80);
static DNSServer s_dns;
static File      s_upload;

static bool     s_active = false;
static bool     s_connected = false;
static bool     s_done = false;
static bool     s_gotJson = false;
static bool     s_gotBin = false;
static uint32_t s_touch = 0;

static wifi_event_id_t s_eventId = 0;

static bool   s_upValid = false;
static bool   s_upOk = false;
static bool   s_upAvatar = false;
static size_t s_upWritten = 0;

static char s_ssid[24];
static char s_pass[20];
static char s_wifiQr[80];
static char s_url[24];
static char s_selfId[UUID_LEN] = {0};
static char s_prevId[UUID_LEN] = {0};

static void touch()
{
    s_touch = millis();
}

static void deriveSsidUrl()
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    snprintf(s_ssid, sizeof(s_ssid), "con-venience-%02X%02X", mac[4], mac[5]);
    snprintf(s_url, sizeof(s_url), "http://%u.%u.%u.%u/", AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
}

static void genPassword()
{
    uint8_t r[8] = {0};
    esp_fill_random(r, sizeof(r));

    snprintf(s_pass, sizeof(s_pass), "%02X%02X%02X%02X%02X%02X%02X%02X", r[0], r[1], r[2], r[3],
             r[4], r[5], r[6], r[7]);
}

static void buildWifiQr()
{
    snprintf(s_wifiQr, sizeof(s_wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", s_ssid, s_pass);
}

static bool serveSd(const char *path, const char *type)
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

static void handleRoot()
{
    touch();
    if (!serveSd(WEB_ROOT, "text/html"))
    {
        s_server.send(500, "text/plain", "profile-generator.html missing on SD");
    }
}

static void handleJszip()
{
    touch();
    if (!serveSd(WEB_JSZIP, "application/javascript"))
    {
        s_server.send(404, "text/plain", "jszip missing");
    }
}

static void handleProfileJson()
{
    touch();
    if (!serveSd(SELF_JSON, "application/json"))
    {
        s_server.send(404, "text/plain", "no profile yet");
    }
}

static void handleAvatar()
{
    touch();
    if (!serveSd(SELF_BIN, "application/octet-stream"))
    {
        s_server.send(404, "text/plain", "no avatar yet");
    }
}

static void handleCaptive()
{
    touch();
    s_server.sendHeader("Location", s_url, true);
    s_server.send(302, "text/plain", "");
}

// POST /upload — multipart with exactly two accepted file fields: "profile" ->
// SELF_JSON, "avatar" -> SELF_BIN. Any other field name is ignored. A part
// counts as received only if it opened, wrote in full within its size bound,
// and (for the avatar) is exactly AVATAR_BYTES; otherwise the partial file is
// dropped and the done-state does not advance. When both parts are in, a random
// self id is generated (or the existing one kept) and captured for display.
static void handleUploadBody()
{
    HTTPUpload &up = s_server.upload();

    if (up.status == UPLOAD_FILE_START)
    {
        touch();
        s_upValid = false;
        s_upOk = false;
        s_upAvatar = false;
        s_upWritten = 0;

        const char *path = nullptr;
        if (up.name == "avatar")
        {
            s_upValid = true;
            s_upAvatar = true;
            path = SELF_BIN;
        }
        else if (up.name == "profile")
        {
            s_upValid = true;
            path = SELF_JSON;
        }

        if (s_upValid)
        {
            if (s_upAvatar)
            {
                s_gotBin = false;
            }
            else
            {
                s_gotJson = false;
            }
            s_done = false;
            if (!s_upAvatar)
            {
                storageReadSelfId(s_prevId);
            }
            storageEnsureParentDir(path);
            SD.remove(path);
            s_upload = SD.open(path, FILE_WRITE);
            s_upOk = (bool)s_upload;
        }
    }
    else if (up.status == UPLOAD_FILE_WRITE)
    {
        touch();
        if (s_upOk && s_upload)
        {
            size_t limit = s_upAvatar ? AVATAR_BYTES : PROFILE_JSON_MAX;
            if (s_upWritten + up.currentSize > limit)
            {
                s_upOk = false;
            }
            else if (s_upload.write(up.buf, up.currentSize) != up.currentSize)
            {
                s_upOk = false;
            }
            else
            {
                s_upWritten += up.currentSize;
            }
        }
    }
    else if (up.status == UPLOAD_FILE_END)
    {
        if (s_upload)
        {
            s_upload.close();
        }

        bool good = s_upValid && s_upOk;
        if (good && s_upAvatar && s_upWritten != AVATAR_BYTES)
        {
            good = false;
        }

        if (!good)
        {
            if (s_upValid)
            {
                SD.remove(s_upAvatar ? SELF_BIN : SELF_JSON);
            }
        }
        else if (s_upAvatar)
        {
            s_gotBin = true;
        }
        else
        {
            s_gotJson = true;
        }

        s_done = s_gotJson && s_gotBin;
        if (s_done)
        {
            storageEnsureSelfId(s_selfId, s_prevId);
        }
    }
}

static void handleUploadDone()
{
    touch();
    s_server.send(s_done ? 200 : 500, "text/plain", s_done ? "ok" : "incomplete upload");
}

static void onWifiEvent(arduino_event_id_t event)
{
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED)
    {
        s_connected = true;
        touch();
    }
}

void wifiConfigStart()
{
    if (s_active)
    {
        return;
    }

    deriveSsidUrl();
    s_connected = false;
    s_done = false;
    s_gotJson = false;
    s_gotBin = false;
    s_selfId[0] = '\0';
    s_prevId[0] = '\0';

    s_eventId = WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_AP);
    // RF is up only after mode(WIFI_AP); esp_fill_random yields true random past this point
    genPassword();
    buildWifiQr();

    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    WiFi.softAP(s_ssid, s_pass);

    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(DNS_PORT, "*", AP_IP);

    s_server.on("/", HTTP_GET, handleRoot);
    s_server.on("/jszip.min.js", HTTP_GET, handleJszip);
    s_server.on("/profile.json", HTTP_GET, handleProfileJson);
    s_server.on("/avatar.bin", HTTP_GET, handleAvatar);
    s_server.on("/upload", HTTP_POST, handleUploadDone, handleUploadBody);
    s_server.on("/generate_204", handleCaptive);
    s_server.on("/gen_204", handleCaptive);
    s_server.on("/hotspot-detect.html", handleCaptive);
    s_server.on("/ncsi.txt", handleCaptive);
    s_server.onNotFound(handleCaptive);
    s_server.begin();

    s_active = true;
    touch();
    Serial0.printf("[wifi_config] up ssid=%s url=%s\n", s_ssid, s_url);
}

void wifiConfigStop()
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
    WiFi.removeEvent(s_eventId);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    s_connected = false;
    s_done = false;
    s_gotJson = false;
    s_gotBin = false;
    s_active = false;
    Serial0.println("[wifi_config] down");
}

void wifiConfigLoop()
{
    if (!s_active)
    {
        return;
    }
    s_dns.processNextRequest();
    s_server.handleClient();
}

bool wifiConfigClientConnected()
{
    return s_connected;
}
bool wifiConfigUploadDone()
{
    return s_done;
}
uint32_t wifiConfigIdleMs()
{
    return millis() - s_touch;
}

const char *wifiConfigWifiQr()
{
    return s_wifiQr;
}
const char *wifiConfigUrl()
{
    return s_url;
}
const char *wifiConfigSsid()
{
    return s_ssid;
}
const char *wifiConfigSelfId()
{
    return s_selfId;
}