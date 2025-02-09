// Use the MCP2515 CAN bus controller to send CAN frames to the dashboard to display our own custom information

#ifndef _DISPLAY_INFO_ON_DASHBOARD
#define _DISPLAY_INFO_ON_DASHBOARD

#include "AA_MCP2515.h"
#include "AsyncTimer.h"
#include "Version.h"

// CAN frames include 8 bytes of data. We have a total of 24 characters on the dashboard, therefore the characters will be sent
// using multiple CAN frames. The data for this specific CAN ID uses the first two bytes to encode the total number of frames
// and which frame is being sent. This leaves us with 6 bytes to set characters. But, these are UTF (not ASCII) characters,
// so each character uses two bytes. Therefore we can send only three characters per frame.
const uint8_t NumCharsInText = 24;
const uint8_t NumUTFCharsPerFrame = 3;
const uint8_t NumFramesToDisplayText = NumCharsInText / NumUTFCharsPerFrame;

// There is an interesting problem to know how frequently CAN frames with the updated information should be sent. If it's too slow,
// then the original text from the infotainment system will briefly overwrite our custom text and the text will flicker. If our
// information includes something like boost pressure that changes very quickly, we want to update the data quickly enough to not see
// outdated information, e.g. don't take half a second to show the current boost pressure. But, we can't do it too quickly, since this is
// on the low speed CAN bus, and we should probably not constantly spam the bus with our own custom frames. I settled on seeing full updated
// information 5 times a second. Since we need 8 CAN frames to update all the information in 200ms, we need to send one CAN frame every 25ms
// This doesn't completely fix the problem, but it's infrequent enough that it doesn't bother me.
const uint32_t TimeToDisplayText = 200;
const uint32_t DelayTimeBetweenFrames = TimeToDisplayText / NumFramesToDisplayText;

enum InfoToDisplay
{
  infoCurrentInfoWithEngineTemp,    // While driving, show turbo boost pressure, current gear and engine temperature
  infoCurrentInfoWithEngineOilTemp, // While driving, show turbo boost pressure, current gear and engine oil temperature
  infoCurrentInfoWithBattery,       // While driving, show turbo boost pressure, current gear and battery voltage
  infoCurrentInfoWithSquadra,       // While driving, show turbo boost pressure, current gear and that Squadra performance tune is enabled
  infoMaxBoost,                     // When car is idling, show information about when maximum turbo boost was obtained
  infoWarningLowBattery,            // When car is idling, show warning when car battery is low
  infoWarningColdEngine             // Don't drive too hard when engine is cold. This warning isn't for me, but for my son when he's driving my car :-)
};

AsyncTimer timerShowNameAndVersion(10000);            // When car is turned on, show name and version for 10 seconds
AsyncTimer timerWaitBeforeShowingInfoWhileIdle(2000); // Some info show only when car is at ~idle. We don't want to immediately show those, but rather wait 2 seconds
AsyncTimer timerToggleCurrentInfo(6000);              // Every 6 seconds toggle info like engine temp, engine oil temp, battery V, etc.

// Every 6 seconds, toggle from infoCurrentInfoWithEngineTemp .. infoCurrentInfoWithBattery
uint8_t currentInfoIndex = infoCurrentInfoWithEngineTemp;
uint8_t maxCurrentInfoIndex = infoCurrentInfoWithBattery;

// g_CurrentCarData is populated by the SN65HVD230 transceiver on another ESP32-S3 core. Since the data needs to be thread safe, we keep a
// local copy of the data on this thread, copying it safely using g_SemaphoreCarData
CarData carData;

// We need to keep track of the max turbo boost pressure, at what engine RPM and in which gear that happended
float   turboBoostPsi = 0.0f;
float   maxBoostPsi   = 0.0f;
int32_t maxBoostRPM   = 0;
int32_t maxBoostGear  = 0;

// Don't want to drive the car too hard while the engine is still cold, so keep track of the max RPM while engine is still cold. We'll use 3000 RPM as a safe RPM
int32_t maxColdRPM    = 0;
const int32_t ColdEngineSafeRPM = 3000;
const int32_t SquadraSafeOilTemperature = 70; // Squadra is only fully enabled when engine oil reaches 70*C, so use that as a safety temp for when engine is still cold

// Setup for the MCP2515 CAN controller connected to the low speed CAN bus, which uses 125Kbps
const CANBitrate::Config CAN_BITRATE = CANBitrate::Config_8MHz_125kbps;
const uint8_t CAN_PIN_CS = SS;
const int8_t CAN_PIN_INT = D6;
CANConfig config(CAN_BITRATE, CAN_PIN_CS, CAN_PIN_INT);
CANController CAN(config);

// This will be called from the main setup() function, which will get called each time the device wakes up from deep sleep
void SetupDisplayInfoOnDashboard()
{
  DebugPrintln("SetupDisplayInfoOnDashboard()");

  while (CAN.begin(CANController::Mode::Normal) != CANController::OK)
  {
    DebugPrintln("MCP2515 CAN controller failed");
    delay(1000);
  }

  DebugPrintln("MCP2515 CAN controller initialized");
}

// When the car is not turned on, we want to put the device into a low power mode
void SleepMCP2515()
{
  CAN.setMode(CANController::Mode::Sleep);
}

void SendCANMessage(uint32_t canID, uint8_t* pData, uint8_t dlc = 8)
{
  CANFrame canFrame(canID, pData, dlc);
  CAN.write(canFrame);
}

// This will clear the text on the dashboard, but it's not required to send every time we want to update text
void ClearDashboardText()
{
  uint8_t canData[8] = { 0x00, 0x11, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00 };
  SendCANMessage(CAN_Id::DashboardText, canData);
}

// Send one CAN frame to set three of the UTF characters in the text
void SetDashboardTextCharacters(uint8_t numFrames, uint8_t currentFrame, char* text)
{
  // The dashboard messages can be group by Radio, Media, Bluetooth, Phone and Navigation. It looks like some groups have higher
  // priority than others when it comes to showing something on the dashboard. For example, when using the Radio FM channel for
  // custom messages, sometimes the Radio FM will send its own messages and cause the custom message to flicker. But, if you listen
  // to Radio AM and you use Radio FM for custom messages, then there is no flickering, until you listen to AM. It's similar to groups,
  // e.g. Phone messages seem to be higher priority than Radio messages, therefore Radio messages won't interfere with Phone messages
  // or Navigation messages. Below are some message "infoCode" values which I identified. I chose to use the Media Center USB channel,
  // since I never use USB and there is no radio text interference when using this channel. Unfortunately, it does show a small USB icon
  // next to the custom text, but I can get used to the icon, I don't know that I can get used to some flickering.

  // 0x00 - ?         - Occasional short flicker from radio station info interfering with custom text
  // 0x02 - FM radio  - Occasional short flicker from radio station info interfering with custom text
  // 0x03 - AM radio  - Occasional short flicker from radio station info interfering with custom text
  // 0x05 - Aux       - Occasional long flicker from radio station info interfering with custom text
  // 0x06 - USB left  - No flicker, but shows USB icon
  // 0x07 - USB right - No flicker, but shows USB icon
  // 0x08 - USB front - No flicker, but shows USB icon
  // 0x09 - Bluetooth - Occasional long flicker from radio station info interfering with custom text

  const uint8_t infoCode = 0x08;        // No flickering, we'll just accept that it shows a USB icon
  const uint8_t indexOfLastFrame = numFrames - 1;
  const uint8_t utfCharStartIndex = 2;  // First UTF character is in canData[2]

  uint8_t canData[8] = { 0 };

  // Num frames - 1, byte[0] bit[7..3]
  canData[0] = (indexOfLastFrame << 3) & 0b11111000;

  // InfoCode, byte[1] bit[5..0]
  canData[1] = infoCode & 0b00111111;

  // Current frame, byte[0] bit[2..0] and byte[1] bit[7..6]
  canData[0] |= (currentFrame >> 2) & 0b00000111;
  canData[1] |= (currentFrame << 6) & 0b11000000;

  // 3 UTF characters, byte[2..3], byte[4..5], byte[6..7]
  for (int i = 0; i < NumUTFCharsPerFrame; i++)
  {
    // UTF uses two bytes per chatacter. But, we only have simple text, so the first byte is always 0
    const uint8_t canDataIndex = i * 2;
    canData[utfCharStartIndex + canDataIndex] = 0;
    canData[utfCharStartIndex + canDataIndex + 1] = text[i];
  }

  SendCANMessage(CAN_Id::DashboardText, canData);
}

// Send multiple CAN frames to display all the text
void SetDashboardText(char* text, uint32_t timeToDisplayText)
{
  for (int currentFrame = 0; currentFrame < NumFramesToDisplayText; currentFrame++)
  {
    uint8_t characterStartPosition = currentFrame * NumUTFCharsPerFrame;
    SetDashboardTextCharacters(NumFramesToDisplayText, currentFrame, text + characterStartPosition);
    delay(DelayTimeBetweenFrames);
  }
}

// Convert numerical gear numbers to easy to read letters
void GenerateGearText(int32_t gear, char* gearText)
{
  char gearLetter = { (gear == -1) ? 'R' : (gear == 0) ? 'N' : 'D' };

  if ((gear > 0) && (gear <= 8))
  {
    sprintf(gearText, "%c%d", gearLetter, gear);
  }
  else
  {
    sprintf(gearText, "%c ", gearLetter);
  }
}

// Given the current car data, generate the full text to be displayed
void GenerateText(char* text)
{
  if (!timerShowNameAndVersion.RanOut())
  {
    sprintf(text, "    %s   v%1.1f", g_ProjectName, g_Version);
    return;
  }

  if (timerToggleCurrentInfo.RanOut())
  {
    currentInfoIndex++;
    if (currentInfoIndex > maxCurrentInfoIndex)
    {
      currentInfoIndex = 0;
    }

    timerToggleCurrentInfo.Start();
  }

  InfoToDisplay infoToDisplay = infoCurrentInfoWithEngineTemp;

  char gearText[8] = { 0 };

  // Choose what info to show while driving

#ifdef SHOW_SQUADRA_MESSAGE
  if (carData.DriveMode == DNASelector::D &&                // Squadra tune is only enabled in Dynamic drive mode
      carData.EngineOilTemp >= SquadraSafeOilTemperature)   // and only fully enabled when engine oil reaches 70*C (158*F)
  {
    infoToDisplay = InfoToDisplay::infoCurrentInfoWithSquadra;
  }
  else
#endif
  {
    if (carData.EngineOilTemp < SquadraSafeOilTemperature &&
        carData.EngineRPM > ColdEngineSafeRPM)
    {
      // Reached high RPM while engine is still cold
      infoToDisplay = InfoToDisplay::infoWarningColdEngine;
    }
    else
    {
      infoToDisplay = (InfoToDisplay)currentInfoIndex;
    }
  }

  // Check to see if car is kind of idling and show other information
  if (carData.EngineRPM < 1000 ||   // Engine is barely above idle
      carData.Gear == -1)           // Car is in Reverse
  {
    if (!timerWaitBeforeShowingInfoWhileIdle.IsActive())
    {
      timerWaitBeforeShowingInfoWhileIdle.Start();
    }

    if (timerWaitBeforeShowingInfoWhileIdle.RanOut())
    {
      // It's not interesting to display boost psi that is less than 1.0f
      if (maxBoostPsi > 1.0f)
      {
        // Show info about max boost
        infoToDisplay = InfoToDisplay::infoMaxBoost;
      }

      // If there are any warning messages, show those instead of interesting max boost info
      if (carData.Battery > 0.0f &&
          carData.Battery < 12.4f )
      {
        // Car battery voltage is low
        infoToDisplay = InfoToDisplay::infoWarningLowBattery;
      }
      else if (carData.EngineOilTemp < SquadraSafeOilTemperature &&
               maxColdRPM > ColdEngineSafeRPM)
      {
        // Reached high RPM while engine is still cold
        infoToDisplay = InfoToDisplay::infoWarningColdEngine;
      }
    }
  }
  else
  {
    timerWaitBeforeShowingInfoWhileIdle.Stop();
  }

  switch (infoToDisplay)
  {    
    case InfoToDisplay::infoCurrentInfoWithEngineTemp:
    {
      // Example:   " 23 psi   D1   Eng 200*F"
      GenerateGearText(carData.Gear, gearText); // Current gear
      float farh = (carData.EngineTemp * 9.5f / 5.0f) + 32.0f;
      sprintf(text, " %2d psi   %s   Eng %3d*F", int32_t(turboBoostPsi + 0.5f), gearText, int32_t(farh + 0.5f));
      break;
    }

    case InfoToDisplay::infoCurrentInfoWithEngineOilTemp:
    {
      // Example:   " 23 psi   D1   Oil 200*F"
      GenerateGearText(carData.Gear, gearText); // Current gear
      float farh = (carData.EngineOilTemp * 9.5f / 5.0f) + 32.0f;
      sprintf(text, " %2d psi   %s   Oil %3d*F", int32_t(turboBoostPsi + 0.5f), gearText, int32_t(farh + 0.5f));
      break;
    }

    case InfoToDisplay::infoCurrentInfoWithBattery:
    {
      // Example:   " 23 psi   D1   Bat 12.6V"
      GenerateGearText(carData.Gear, gearText); // Current gear
      sprintf(text, " %2d psi   %s   Bat %2.1fV", int32_t(turboBoostPsi + 0.5f), gearText, carData.Battery);
      break;
    }

    case InfoToDisplay::infoCurrentInfoWithSquadra:
    { 
      // Example:   " 23 psi   D1   Squadra  "
      GenerateGearText(carData.Gear, gearText); // Current gear
      sprintf(text, " %2d psi   %s   Squadra  ", int32_t(turboBoostPsi + 0.5f), gearText);
      break;
    }

    case InfoToDisplay::infoMaxBoost:
    {
      // Example:   "Max 23 psi @ 5555 rpm D2"
      GenerateGearText(maxBoostGear, gearText); // Gear when max boost pressure was measured
      sprintf(text, "Max %2d psi @ %4d rpm", int32_t(maxBoostPsi + 0.5f), maxBoostRPM);
      sprintf(text, "%s %s", text, gearText);
      break;
    }

    case InfoToDisplay::infoWarningLowBattery:
    {
      // Example:   " Battery is low!  12.2V "
      sprintf(text, " Battery is low!  %2.1fV ", carData.Battery);
      break;
    }

    case InfoToDisplay::infoWarningColdEngine:
    {
      sprintf(text, " Careful, engine is cold");
      break;
    }

    default:
      ClearDashboardText();
  }

  //DebugPrintln(text);
}

// Main function of the thread task running on a seperate ESP32-S3 core
void DisplayInfoOnDashboard(void* params)
{
  DebugPrintf("Core %d: DisplayInfoOnDashboard()\n", xPortGetCoreID());

  timerToggleCurrentInfo.Start();

  // Just for safety, we make the text twice as long as we realy need
  char text[NumCharsInText * 2] = "Initializing .....";

  while (true)
  {
    // Make a local copy of car data that was gathered on the other ESP32-S3 core
    xSemaphoreTake(g_SemaphoreCarData, portMAX_DELAY);
    memcpy(&carData, &g_CurrentCarData, sizeof(CarData));
    xSemaphoreGive(g_SemaphoreCarData);

    // Only send CAN from to the dashboard if the car is actually turned on. It looks like the act of sending frames to the dashboard
    // after the car turned off keeps the car in an "active" state, draining the battery.
    if (carData.bCarTurnedOn)
    {
      // Calculate the turbo boost pressure using atmospheric pressure and absolute boost pressure (1013 mbar is sea level)
      turboBoostPsi = _min(_max(0.0f, float(carData.BoostPressure - carData.AtmosphericPressure)) * 0.0145038f, 40.0f);

      if (turboBoostPsi > maxBoostPsi &&
          carData.AtmosphericPressure > 0)    // If we don't have valid data for atmospheric pressure, the max boost will be completely wrong
      {
        maxBoostRPM   = carData.EngineRPM;
        maxBoostGear  = carData.Gear;
        maxBoostPsi   = turboBoostPsi;
        DebugPrintf("\nMax turbo boost pressure = %.1f psi @ %d RPM in gear %d\n", maxBoostPsi, maxBoostRPM, maxBoostGear);
      }

      // Keep track of when RPMs are high when engine is still cold
      if (carData.EngineOilTemp < SquadraSafeOilTemperature &&
          carData.EngineRPM > maxColdRPM)
      {
        maxColdRPM = carData.EngineRPM;
      }
      
      GenerateText(text);
      SetDashboardText(text, TimeToDisplayText);
    }
    else
    {
      timerShowNameAndVersion.Start();
      delay(TimeToDisplayText);
    }
  }
}

#endif