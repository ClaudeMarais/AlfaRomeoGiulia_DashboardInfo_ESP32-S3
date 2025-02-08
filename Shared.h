#ifndef _SHARED
#define _SHARED

#ifdef DEBUG
#define DebugPrintf(...) Serial.printf(__VA_ARGS__)
#define DebugPrintln(...) Serial.println(__VA_ARGS__)
#else
#define DebugPrintln(...)
#define DebugPrintf(...)
#endif

// Car data needed for the information we want to display on the dashboard
struct CarData
{
  int32_t EngineRPM;
  int32_t Gear;
  int32_t EngineTemp;
  int32_t EngineOilTemp;
  int32_t AtmosphericPressure;
  int32_t BoostPressure;        // Absolute boost pressure from the sensor
  float Battery;
  uint8_t DriveMode;            // DNA selector
  bool bCarTurnedOn;
};

// This data is shared between two ESP32-S3 cores
CarData g_CurrentCarData { 0 };
SemaphoreHandle_t g_SemaphoreCarData;
TaskHandle_t g_TaskDisplayInfoOnDashboard = nullptr;

// CAN IDs of CAN frames that are continously broadcasted which carries encoded information without the need to send an OBD2 request
enum CAN_Id
{
  DriveMode       = 0x384,
  GearInfo        = 0x2EF,
  EngineRPM       = 0x0FC,
  Boost           = 0x2EF,
  DashboardText   = 0x090
};

// It's a good idea to reboot the device after waking up from deep sleep to ensure that everything is in a clean state
RTC_DATA_ATTR bool g_bInDeepSleep = false;

// Pins for SN65HVD230 - After some failed attempts with the ESP32-S3, it seems that pins TX/RX and D0/D1 don't send/receive data to/from
// SN65HVD230 correctly, but D4/D5 does. (which coincidentally is also SDA/SCL)
const uint8_t TXPin = D5;
const uint8_t RXPin = D4;

#endif