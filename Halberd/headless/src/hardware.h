#pragma once
#include "scanner.h"
#include "network.h"
#include "main.h"
#include <RTClib.h>
#include <FS.h>
#include <SD.h>

#ifndef COUNTRY
#define COUNTRY "US"
#endif
#ifndef MESH_RX_PIN
#define MESH_RX_PIN 4    // TO MESH PIN 9/19 T114/V3
#endif
#ifndef MESH_TX_PIN
#define MESH_TX_PIN 5    // TO MESH PIN 10/20 T114/V3
#endif
#ifndef VIBRATION_PIN
#define VIBRATION_PIN 2  // TO SW-420 D0
#endif

// SD Card (SPI)
#define SD_CS_PIN   1    // CS on D0
#define SD_CLK_PIN  7    // CLK on D8
#define SD_MISO_PIN 8    // MISO on D9
#define SD_MOSI_PIN 9    // MOSI on D10

// GPS (UART) — v4 only. On v5 these pins are repurposed as the C5 link UART
// (see below); the GPS is reached indirectly via the C5. Stage 3 of the
// feat/c5-firmware branch will remove the GPS owner from the S3.
#define GPS_RX_PIN 44   // GPS RX
#define GPS_TX_PIN 43   // GPS TX

// C5 link UART (v5). Same physical pins as the v4 GPS UART above — the v5
// carrier reuses them. Names are written from the S3's perspective:
//   C5_LINK_TX_PIN = S3 transmits → C5 RX  (net C5_LINK_TX on the schematic)
//   C5_LINK_RX_PIN = S3 receives ← C5 TX  (net C5_LINK_RX on the schematic)
// See hw/pcb/docs/schematic.md for the netlist and hw/pcb/docs/kicad-tutorial-v5.md
// Appendix B for the C5-side pinout.
#define C5_LINK_TX_PIN 43
#define C5_LINK_RX_PIN 44

// RTC + UPS (shared I2C bus)
#define RTC_SDA_PIN 3    // RTC SDA (also UPS INA219 SDA via J_UPS.3)
#define RTC_SCL_PIN 6    // RTC SCL (also UPS INA219 SCL via J_UPS.4)

// Waveshare UPS Module 3S onboard INA219. A0 strap pulled high on
// Waveshare's PCB shifts the chip from default 0x40 → 0x41.
#define INA219_I2C_ADDR 0x41

// Configuration constants
#define CONFIG_FILE "/config.json"
#define MAX_CONFIG_SIZE 4096

class SafeSD {
private:
    static uint32_t lastCheckTime;
    static bool lastCheckResult;
    static const uint32_t CHECK_INTERVAL_MS = 1000;
    static bool checkAvailability();

public:
    static bool isAvailable();
    static fs::File open(const char* path, const char* mode = FILE_READ);
    static bool exists(const char* path);
    static bool remove(const char* path);
    static bool mkdir(const char* path);
    static bool rmdir(const char* path);
    static size_t write(fs::File& file, const uint8_t* data, size_t len);
    static size_t read(fs::File& file, uint8_t* data, size_t len);
    static bool flush(fs::File& file);
    static void forceRecheck();
};

// RTC Status
extern RTC_DS3231 rtc;
extern bool rtcAvailable;
extern bool rtcSynced;
extern time_t lastRTCSync;
extern String rtcTimeString;
extern SemaphoreHandle_t rtcMutex;

bool waitForInitialConfig();
void initializeRTC();
void syncRTCFromGPS();
void updateRTCTime();
String getRTCTimeString();
String getFormattedTimestamp();
time_t getRTCEpoch();
uint32_t getEventTimestamp();
bool setRTCTime(int year, int month, int day, int hour, int minute, int second);
bool setRTCTimeFromEpoch(time_t epoch);

// UPS battery monitor — Waveshare 3S Li-ion pack via INA219 on the same
// S3 I2C bus as the DS3231. Calibration constants ported from the
// known-good gotailme reader (internal/battery/{ina219.go,monitor.go}).
// All access serialised through rtcMutex because the bus is shared.
extern bool  inaAvailable;       // true once initializeINA219() handshake succeeds
extern float inaLastVoltage;     // bus volts (battery pack voltage, 0..16 V range)
extern float inaLastCurrentMa;   // milliamps, signed (+ = charging, - = discharging)
extern float inaLastPct;         // 0..100 mapped from 9.0..12.6 V (3S Li-ion)
extern bool  inaLastCharging;    // mirror of (inaLastCurrentMa > 0)

void   initializeINA219();
bool   readINA219();             // refreshes ina* globals; returns true on success
String getBatteryStatusString(); // "11.92V 81% CHG" / "--" when unavailable

// Sensors and GPS.
//
// On v5 the GPS module lives on the C5; the S3 receives parsed fixes over
// the c5_link UART (see Halberd/shared/link_protocol.h struct link_gps_fix).
// These globals are the post-handover replacement for the v4 TinyGPSPlus
// object accessors — every field that used to come from `gps.<thing>` is
// now mirrored as a typed global, updated by the c5_link GPS_FIX handler.
extern bool sdAvailable;
extern bool gpsValid;
extern float gpsLat, gpsLon;
extern SemaphoreHandle_t gpsMutex;
extern String lastGPSData;
extern uint8_t gpsSatellites;
extern float   gpsHDOP;          // 99.9 when no fix
extern int     gpsAltitudeM;
extern uint16_t gpsYear;
extern uint8_t  gpsMonth, gpsDay, gpsHour, gpsMinute, gpsSecond, gpsCentisecond;
extern bool gpsDateValid;
extern bool gpsTimeValid;
extern uint32_t gpsLastFixMs;    // millis() of most recent GPS_FIX frame, 0 if none
extern volatile bool vibrationDetected;
extern unsigned long lastVibrationTime;
extern unsigned long lastVibrationAlert;

void initializeHardware();
void initializeVibrationSensor();
void initializeSD();
void initializeGPS();

void updateSetupModeStatus();
void checkAndSendVibrationAlert();
String getGPSData();
void updateGPSLocation();
void sendStartupStatus();
void sendGPSLockStatus(bool locked);
void parseChannelsCSV(const String &csv);
void saveTargetsList(const String &txt);
void saveConfiguration();
void loadConfiguration();
void syncSettingsToNVS();
void logToSD(const String &data);
void logEventToSD(const char* path, const String& jsonLine);

// Tamper Detection System
extern bool tamperEraseActive;
extern uint32_t tamperSequenceStart;
extern String tamperAuthToken;
extern bool autoEraseEnabled;
extern uint32_t autoEraseDelay;
extern uint32_t autoEraseCooldown;
extern uint32_t vibrationsRequired;
extern uint32_t detectionWindow;
extern uint32_t setupDelay;
extern uint32_t setupStartTime;
extern bool inSetupMode;
extern String eraseStatus;
extern bool eraseInProgress;

// Battery Saver Mode
extern bool batterySaverEnabled;
extern uint32_t batterySaverHeartbeatInterval;
extern uint32_t lastBatterySaverHeartbeat;

bool initiateTamperErase();
void cancelTamperErase();
bool checkTamperTimeout();
bool performSecureWipe();
void deleteAllFiles(const String &dirname);
bool executeSecureErase(const String &reason);
String generateEraseToken();
bool validateEraseToken(const String &token);

// Battery Saver Functions
void enterBatterySaver(uint32_t heartbeatIntervalMs = 300000);
void exitBatterySaver();
void sendBatterySaverHeartbeat();
String getBatterySaverStatus();