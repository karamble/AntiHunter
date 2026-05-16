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

// RTC (I2C)
#define RTC_SDA_PIN 3    // RTC SDA
#define RTC_SCL_PIN 6    // RTC SCL

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


void initializeRTC();
void syncRTCFromGPS();
void updateRTCTime();
String getRTCTimeString();
String getFormattedTimestamp();
time_t getRTCEpoch();
uint32_t getEventTimestamp();
bool setRTCTime(int year, int month, int day, int hour, int minute, int second);
bool setRTCTimeFromEpoch(time_t epoch);

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

bool waitForInitialConfig();
void updateSetupModeStatus();
void checkAndSendVibrationAlert();
String getDiagnostics();
String getGPSData();
void updateGPSLocation();
void sendStartupStatus();
void sendGPSLockStatus(bool locked);
void parseChannelsCSV(const String &csv);
void saveTargetsList(const String &txt);
extern unsigned long lastSaveTime;
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
void broadcastToTerminal(const String &message);

// Battery Saver Functions
void enterBatterySaver(uint32_t heartbeatIntervalMs = 300000);
void exitBatterySaver();
void sendBatterySaverHeartbeat();
String getBatterySaverStatus();