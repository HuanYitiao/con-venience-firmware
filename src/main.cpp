#include <Arduino.h>

#include <NimBLEDevice.h>
#include <SPI.h>
#include <Wire.h>

#include "acom.h"
#include "audio.h"
#include "ble.h"
#include "button.h"
#include "display.h"
#include "fsm.h"
#include "io_expander.h"
#include "packing.h"
#include "pcf85063a.h"
#include "pins.h"
#include "power.h"
#include "storage.h"
#include "wifi_config.h"

static Contact self = {};
static Contact currentContact = {};
#define CONTACTS_MAX 64
static char contactNames[CONTACTS_MAX][NAME_LEN] = {};
static int  contactCount = 0;
// List index currentContact was last loaded from; -1 forces a reload.
static int  lastContactIndex = -1;
static bool idleShowQR = false;
static bool needProvision = false;
// Card in and mounted. Without it nothing works: the UI locks on a prompt.
static bool sdReady = false;
// Last GPB0 reading, for spotting a card going in while the prompt is up.
static bool sdSensed = false;

static Pcf85063a rtc(Wire, 0x51);
static bool      timeValid = false;

// Time without a button press or state change before the device switches off.
// Which states may time out is decided by the FSM (EVENT_OVERTIME_SHUTDOWN is
// ignored in pairing, contact card, settings and BLE transfer).
#define IDLE_SHUTDOWN_MS (30UL * 1000)  // TODO: back to 60 s after testing
// While off, wake on the RTC this often for housekeeping (0 = buttons only).
#define TIMER_WAKE_INTERVAL_S (60UL * 60)
// Low battery: how long the warning stays up before the device switches off
// (buttons only, no RTC wake-ups). Button presses do not extend it.
#define LOW_BATTERY_SHUTDOWN_MS (5UL * 1000)
// Stay on while powered from USB (for flashing / serial debugging): no idle
// shutdown, no low-battery shutdown (VBAT is meaningless without a battery),
// and an RTC wake boots fully instead of going back to sleep.
#define USB_POWER_NO_SLEEP 1
#define USB_POLL_MS 500
// No-SD prompt: let the card seat in the slot before rebooting into it.
#define SD_INSERT_SETTLE_MS 500

static bool usbPresent = false;

static bool    bleResultReady = false;
static bool    bleResultSuccess = false;
static uint8_t bleRxBuf[PACKING_BUF_MAX];
static size_t  bleRxLen = 0;

static void onBleComplete(bool success, const uint8_t *data, size_t len)
{
    bleResultSuccess = success;
    bleResultReady = true;
    if (success && data && len > 0)
    {
        memcpy(bleRxBuf, data, len);
        bleRxLen = len;
    }
}

static void startBle()
{
    Serial0.println("startBle called");
    uint8_t ownMac[6];
    uint8_t peerMac[6];
    uint8_t peerType;

    const ble_addr_t *addr = NimBLEDevice::getAddress().getBase();
    for (int i = 0; i < 6; i++)
        ownMac[i] = addr->val[5 - i];

    if (!acomHasMac(peerMac, &peerType))
    {
        Serial0.println("startBle: no peer mac");
        fsmHandleEvent(EVENT_BLE_FAILURE);
        return;
    }
    Serial0.printf("startBle: peer MAC %02x:%02x:%02x:%02x:%02x:%02x type=%d\n", peerMac[0],
                   peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5], peerType);

    ble_role_t role = (memcmp(ownMac, peerMac, 6) < 0) ? BLE_ROLE_SERVER : BLE_ROLE_CLIENT;
    Serial0.printf("BLE role: %s\n", role == BLE_ROLE_SERVER ? "SERVER" : "CLIENT");

    static uint8_t packBuf[PACKING_BUF_MAX];
    size_t         packLen = packingPack(packBuf, sizeof(packBuf));
    Serial0.printf("packLen: %d\n", packLen);
    if (packLen == 0)
    {
        Serial0.println("startBle: pack failed");
        fsmHandleEvent(EVENT_BLE_FAILURE);
        return;
    }

    bleStart(peerMac, peerType, role, packBuf, packLen, onBleComplete);
    Serial0.println("startBle: bleStart called");
}

static bool isBleActiveState(state_t s)
{
    return s == STATE_BLE_CONNECTING || s == STATE_BLE_TRANSFER;
}

static void dispatchEvent(event_t event)
{
    state_t before = fsmGetState();
    fsmHandleEvent(event);
    state_t after = fsmGetState();

    if (before != after)
    {
        if (after == STATE_PAIRING)
        {
            acomStart();
        }

        if (after == STATE_BLE_CONNECTING)
        {
            startBle();
        }

        // Leaving the BLE flow entirely (not the CONNECTING->TRANSFER hop) tears it down.
        if (isBleActiveState(before) && !isBleActiveState(after))
        {
            if (!bleResultReady)
            {
                bleStop();
            }
        }

        if (before == STATE_PAIRING && after != STATE_PAIRING)
        {
            acomStop();
        }

        if (after == STATE_SETTINGS)
        {
            wifiConfigStart(rtc);
        }
        if (before == STATE_SETTINGS && after != STATE_SETTINGS)
        {
            wifiConfigStop();
        }

        // Demo reset: archive the profile folders, then reboot so the normal boot
        // path finds no profile and goes straight to provisioning.
        if (after == STATE_PROFILE_RESET)
        {
            bool ok = storageArchiveProfiles() > 0;
            drawResetting(ok);
            delay(ok ? 1000 : 3000);
            ESP.restart();
        }

        displayResetScroll();
        Serial0.printf("[EVENT] %-20s | %s -> %s\n", eventName(event), stateName(before),
                       stateName(after));
    }
    else
    {
        Serial0.printf("[EVENT] %-20s | %s (no change)\n", eventName(event), stateName(before));
    }
}

void settingsService()
{
    static bool wasActive = false;

    if (fsmGetState() != STATE_SETTINGS)
    {
        wasActive = false;
        return;
    }

    wifiConfigLoop();

    static bool lastConn = false;
    static bool lastDone = false;
    if (!wasActive || wifiConfigClientConnected() != lastConn || wifiConfigUploadDone() != lastDone)
    {
        wasActive = true;
        lastConn = wifiConfigClientConnected();
        lastDone = wifiConfigUploadDone();
        drawSettings();
    }
}

// Live RTC snapshot for the settings clock page. Re-read a few times a second, only
// while that page is showing; readTrusted() decides whether the time may be believed.
#define CLOCK_REFRESH_MS 250
static ClockView clockView = {CLOCK_RTC_ERROR, 0, 0, 0, 0, 0, 0};

static void clockService()
{
    static bool          wasActive = false;
    static unsigned long lastReadMs = 0;

    if (fsmGetState() != STATE_SETTINGS_CLOCK)
    {
        wasActive = false;
        return;
    }

    unsigned long now = millis();
    if (wasActive && now - lastReadMs < CLOCK_REFRESH_MS)
    {
        return;
    }
    wasActive = true;
    lastReadMs = now;

    pcf_time_t t;
    bool       trusted = false;
    if (rtc.readTrusted(t, trusted) != PCF_OK)
    {
        clockView = {CLOCK_RTC_ERROR, 0, 0, 0, 0, 0, 0};
        return;
    }
    if (!trusted)
    {
        clockView = {CLOCK_NOT_SET, 0, 0, 0, 0, 0, 0};
        return;
    }
    clockView = {CLOCK_OK, t.year, t.month, t.day, t.hours, t.minutes, t.seconds};
}

static void batteryService()
{
    static uint32_t lastCheckMs = 0;
    static uint8_t  lowStreak = 0;

    uint32_t now = millis();
    if (usbPresent)
    {
        lowStreak = 0;
        return;
    }
    if (now < BOOT_SETTLE_MS || now - lastCheckMs < BATTERY_CHECK_INTERVAL_MS)
    {
        return;
    }
    lastCheckMs = now;

    uint16_t vbatMv = powerReadBatteryMv();
    Serial0.printf("VBAT: %u mV (lowStreak=%u)\n", vbatMv, lowStreak);
    powerBatteryLevelUpdate(vbatMv);

    if (vbatMv < BATTERY_LOW_MV)
    {
        if (lowStreak < 255)
        {
            lowStreak++;
        }
        if (lowStreak >= BATTERY_LOW_CONFIRM_COUNT)
        {
            dispatchEvent(EVENT_BATTERY_LOW);
        }
    }
    else
    {
        lowStreak = 0;
    }
}

// Checked once at boot, for every wake reason: a wake must not hand out the
// full UI on a flat battery before batteryService() (BOOT_SETTLE_MS + 3
// checks) would catch it. Only re-reads while the reading stays low. The first
// reading also feeds the battery gauge.
static bool bootBatteryLow()
{
    for (int i = 0; i < BATTERY_LOW_CONFIRM_COUNT; i++)
    {
        uint16_t vbatMv = powerReadBatteryMv();
        Serial0.printf("[power] boot VBAT: %u mV\n", vbatMv);
        if (i == 0)
        {
            powerBatteryLevelUpdate(vbatMv);
        }
        if (vbatMv >= BATTERY_LOW_MV)
        {
            return false;
        }
    }
    return true;
}

// Power-down order; setup() brings things up in reverse.
[[noreturn]] static void shutdownDevice(uint32_t wakeAfterSec)
{
    audio_deinit();    // MAX98357 off (SPK_EN low), I2S stopped
    displaySleep();    // LCD is on the always-on rail; needs SPI, so before SD
    storageEnd();      // SD CS / MISO high-Z
    powerPeriphOff();  // peripheral DCDC off
    powerSleep(wakeAfterSec);  // MCU_SLEEP_REQ high
}

void setup()
{
    Serial0.begin(115200);

    // Power control comes first: after a wake the MCU only stays powered while
    // the wake INT is asserted, until powerEarlyInit() drops SLEEP_REQ.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    ioexpInit();
    powerEarlyInit(rtc);
    btnInit();

    power_wake_t wake = powerGetWakeReason();
    Serial0.printf("[power] wake: %s\n", powerWakeName(wake));

    // Wake order is the reverse of shutdownDevice(): SLEEP_REQ low (done in
    // powerEarlyInit), peripheral DCDC on, SD, display, MAX98357 last.
    // The battery is read with the peripheral rail up, as it was validated.
    powerPeriphOn();
    usbPresent = USB_POWER_NO_SLEEP && powerUsbPresent();
    Serial0.printf("[power] USB %s\n", usbPresent ? "present, sleep disabled" : "absent");
    // COLD_BOOT also covers ESP.restart() / panic / WDT (SLEEP_REQ is clear then
    // too); only a real power-up (battery inserted or power switch) means the
    // cells may have been replaced.
    powerBatteryLevelInit(wake == POWER_WAKE_COLD_BOOT && esp_reset_reason() == ESP_RST_POWERON);
    bool batteryLow = !usbPresent && bootBatteryLow();
    // Woken by the RTC only: battery fine -> straight back to sleep without
    // bringing up BLE, the display or SD. Battery low -> boot on and show the
    // warning like any other low-battery boot.
    if (wake == POWER_WAKE_TIMER && !batteryLow && !usbPresent)
    {
        powerPeriphOff();
        powerSleep(TIMER_WAKE_INTERVAL_S);
    }

    NimBLEDevice::init("con-venience");
    NimBLEDevice::setMTU(517);

    fsmInit();

    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
    delay(10);
    sdSensed = powerSdPresent();
    sdReady = sdSensed && storageInit();
    Serial0.printf("[sd] card %s, %s\n", sdSensed ? "present" : "absent",
                   sdReady ? "mounted" : "NOT mounted");
    if (sdReady)
    {
        char selfId[UUID_LEN];
        storageEnsureSelfId(selfId);
        needProvision = !storageLoadSelf(self);
        Serial0.printf("self: name=%s res=%d links=%d id=%s provision=%d\n", self.name,
                       self.avatarResolution, self.linkCount, self.uuid, needProvision);
        contactCount = min(storageCountContacts(), CONTACTS_MAX);
        Serial0.printf("contactCount: %d\n", contactCount);
        for (int i = 0; i < contactCount; i++)
        {
            storageLoadContactName(i, contactNames[i], NAME_LEN);
            Serial0.printf("loaded name %d: %s\n", i, contactNames[i]);
        }
    }

    displayInit();

    Serial0.printf("BLE MAC: %s\n", NimBLEDevice::getAddress().toString().c_str());

    if (rtc.begin() == PCF_OK)
    {
        pcf_time_t t;
        bool       trusted = false;
        if (rtc.readTrusted(t, trusted) == PCF_OK)
        {
            timeValid = trusted;
            Serial0.printf("[rtc] present, now = %04u-%02u-%02u %02u:%02u:%02u  trusted=%s\n",
                           t.year, t.month, t.day, t.hours, t.minutes, t.seconds,
                           trusted ? "yes" : "NO(needs sync)");
        }
        else
        {
            timeValid = false;
            Serial0.println("[rtc] present but read failed -- time UNRELIABLE");
        }
    }
    else
    {
        timeValid = false;
        Serial0.println("[rtc] not found -- wall clock UNRELIABLE, continuing anyway");
    }

    audio_init();
    audio_setWaveform(AUDIO_WAVE_SQUARE);

    static const audio_note_t startupJingle[] = {
        {262, 140},
        {294, 140},
        {330, 400},
    };

    // audio_playSequence(startupJingle, sizeof(startupJingle) / sizeof(startupJingle[0]), 15);

    Serial0.println("audio ready");
    acomInit();

    if (!sdReady)
    {
        dispatchEvent(EVENT_NO_SD_CARD);
    }
    // After the SD check, so low battery wins over the SD prompt.
    if (batteryLow)
    {
        dispatchEvent(EVENT_BATTERY_LOW);
    }

    Serial0.println("con-venience ready");
    Serial0.printf("Initial state: %s\n", stateName(fsmGetState()));
}

void loop()
{
    static bool provisionChecked = false;
    if (!provisionChecked)
    {
        provisionChecked = true;
        if (needProvision)
        {
            dispatchEvent(EVENT_ENTER_SETTINGS);
        }
    }

    btn_events_t btnEvents = btnPoll();

    event_t event;
    bool    hasEvent = false;

    if (btnEvents.up == BTN_CLICK)
    {
        event = EVENT_UP_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.up == BTN_LONG_PRESS)
    {
        event = EVENT_UP_LONG_PRESS;
        hasEvent = true;
    }
    else if (btnEvents.down == BTN_CLICK)
    {
        event = EVENT_DOWN_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.down == BTN_LONG_PRESS)
    {
        event = EVENT_DOWN_LONG_PRESS;
        hasEvent = true;
    }
    else if (btnEvents.pair == BTN_CLICK)
    {
        event = EVENT_PAIRING_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.pair == BTN_LONG_PRESS)
    {
        event = EVENT_PAIRING_LONG_PRESS;
        hasEvent = true;
    }
    else if (btnEvents.left == BTN_CLICK)
    {
        event = EVENT_LEFT_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.left == BTN_LONG_PRESS)
    {
        event = EVENT_LEFT_LONG_PRESS;
        hasEvent = true;
    }
    else if (btnEvents.right == BTN_CLICK)
    {
        event = EVENT_RIGHT_CLICK;
        hasEvent = true;
    }
    else if (btnEvents.right == BTN_LONG_PRESS)
    {
        event = EVENT_RIGHT_LONG_PRESS;
        hasEvent = true;
    }

    static uint32_t lastActivityMs = 0;
    static uint32_t stateSinceMs = 0;
    static state_t  lastState = STATE_IDLE;
    if (fsmGetState() != lastState)
    {
        stateSinceMs = millis();
        lastState = fsmGetState();
        lastActivityMs = stateSinceMs;
    }
    static uint32_t lastUsbPollMs = 0;
    if (millis() - lastUsbPollMs >= USB_POLL_MS)
    {
        lastUsbPollMs = millis();
        bool now = USB_POWER_NO_SLEEP && powerUsbPresent();
        if (now != usbPresent)
        {
            usbPresent = now;
            Serial0.printf("[power] USB %s\n", usbPresent ? "present, sleep disabled" : "removed");
        }

        // No-SD prompt: a card going in (absent -> present edge) reboots into the
        // normal boot path. Edge only, so a card that fails to mount does not loop.
        if (fsmGetState() == STATE_NO_SD_CARD)
        {
            bool sd = (ioexpReadReg(MCP_GPIOB) >> PIN_MCP_SD_SENSE) & 1;
            if (sd && !sdSensed)
            {
                Serial0.println("[sd] card inserted, rebooting");
                delay(SD_INSERT_SETTLE_MS);
                ESP.restart();
            }
            sdSensed = sd;
        }
    }
    // Held on USB, or auto sleep switched off in settings (demo mode): the idle
    // timer starts counting only once that ends. Low battery still shuts down.
    if (hasEvent || usbPresent || !fsmGetAutoSleep())
    {
        lastActivityMs = millis();
    }
    if (millis() - lastActivityMs > IDLE_SHUTDOWN_MS)
    {
        dispatchEvent(EVENT_OVERTIME_SHUTDOWN);
    }
    if (fsmGetState() == STATE_STANDBY)
    {
        shutdownDevice(TIMER_WAKE_INTERVAL_S);
    }
    // Low battery: the FSM ignores all events in this state, so a button wake
    // only lights the warning up again; no feature can be entered.
    if (fsmGetState() == STATE_LOW_BATTERY && !usbPresent
        && millis() - stateSinceMs > LOW_BATTERY_SHUTDOWN_MS)
    {
        shutdownDevice(0);
    }

    if (hasEvent)
    {
        state_t before = fsmGetState();
        dispatchEvent(event);

        if (fsmGetState() == STATE_MENU)
        {
            int         sel = fsmGetMenuSelection();
            const char *name = sel == 0 ? "My Profile" : sel == 1 ? "Friends" : "Settings";
            Serial0.printf("[MENU]  selection: %s\n", name);
        }

        if (event == EVENT_PAIRING_CLICK && before == STATE_IDLE)
        {
            idleShowQR = !idleShowQR;
        }
    }

    if (bleResultReady)
    {
        bleResultReady = false;
        // A result arriving after we left the BLE flow (e.g. connect timeout) is
        // dropped: the user already saw it fail, so don't save a contact silently.
        if (!isBleActiveState(fsmGetState()))
        {
            Serial0.println("BLE result ignored: no longer in BLE state");
        }
        else if (bleResultSuccess)
        {
            char peerUuid[UUID_LEN];
            bool ok = packingUnpack(bleRxBuf, bleRxLen, peerUuid);
            Serial0.printf("unpack: %s\n", ok ? "OK" : "FAIL");
            if (ok)
            {
                // The contact card shows currentContact: point it at the peer just
                // received, not whatever the friends list index last loaded. The
                // list reloads its own entry when it is opened again.
                bool loaded = storageLoadContactByUuid(peerUuid, currentContact);
                Serial0.printf("loadContact %s: %s\n", peerUuid, loaded ? "OK" : "FAIL");
                lastContactIndex = -1;
                contactCount = min(storageCountContacts(), CONTACTS_MAX);
                for (int i = 0; i < contactCount; i++)
                {
                    storageLoadContactName(i, contactNames[i], NAME_LEN);
                }
            }
            fsmHandleEvent(EVENT_BLE_SUCCESS);
        }
        else
        {
            fsmHandleEvent(EVENT_BLE_FAILURE);
        }
    }

    if (fsmGetState() == STATE_PAIRING)
    {
        acomTick();

        uint8_t peerMac[6];
        if (acomHasMac(peerMac, nullptr))
        {
            dispatchEvent(EVENT_ACOM_SUCCESS);
        }
        else if (acomFailed())
        {
            acomStart();
        }
        else if (millis() - fsmGetStateEnterTime() > PAIRING_TIMEOUT_MS)
        {
            dispatchEvent(EVENT_PAIRING_OVERTIME);
        }
    }

    // Only the not-yet-connected phase times out; once a link is up the exchange
    // ends on its own (completion, or disconnect via BLE supervision timeout).
    if (fsmGetState() == STATE_BLE_CONNECTING && !bleIsConnected()
        && millis() - fsmGetStateEnterTime() > BLE_CONNECT_TIMEOUT_MS)
    {
        dispatchEvent(EVENT_BLE_FAILURE);
    }

    if (fsmGetState() == STATE_CONTACT_CARD)
    {
        if (millis() - fsmGetStateEnterTime() > CARD_DISPLAY_MS)
        {
            dispatchEvent(EVENT_CARD_OVERTIME);
        }
    }

    settingsService();
    clockService();
    // loopBatteryMonitorTask();  // debug
    batteryService();

    int currentIndex = fsmGetContactIndex();
    // The contact card holds the freshly paired peer; don't overwrite it from the list.
    if (contactCount > 0 && currentIndex != lastContactIndex
        && fsmGetState() != STATE_CONTACT_CARD)
    {
        bool ok = storageLoadContact(currentIndex, currentContact);
        Serial0.printf("loadContact %d: %s\n", currentIndex, ok ? "OK" : "FAIL");
        lastContactIndex = currentIndex;
    }

    int  transferPercent = 0;
    bool transferIndeterminate = true;
    {
        state_t bleState = fsmGetState();
        if (bleState == STATE_BLE_CONNECTING || bleState == STATE_BLE_TRANSFER)
        {
            uint16_t   pdone = 0;
            uint16_t   ptotal = 0;
            ble_prog_t ps = bleGetProgress(&pdone, &ptotal);

            if (ps == BLE_PROG_ACTIVE && ptotal > 0)
            {
                transferPercent = (int)((uint32_t)pdone * 100 / ptotal);
                if (transferPercent > 100)
                    transferPercent = 100;
                transferIndeterminate = false;
            }

            // First honest progress reading promotes CONNECTING -> TRANSFER.
            if (bleState == STATE_BLE_CONNECTING && ps == BLE_PROG_ACTIVE)
            {
                dispatchEvent(EVENT_BLE_TRANSFER_START);
            }
        }
    }

    const Contact &profileContact = fsmIsViewingSelf() ? self : currentContact;
    displayRender(fsmGetState(), self, currentContact, contactNames, contactCount,
                  fsmGetContactIndex(), fsmGetMenuSelection(), idleShowQR, profileContact,
                  fsmGetLinkIndex(), transferPercent, transferIndeterminate,
                  fsmGetSettingsSelection(), fsmGetAutoSleep(), fsmGetResetSelection(), clockView,
                  powerBatteryLevel(), powerLastBatteryMv());
}