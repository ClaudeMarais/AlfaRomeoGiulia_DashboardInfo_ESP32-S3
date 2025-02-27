// Use the MCP2515 CAN bus controller to send CAN frames to the dashboard to display our own custom information

#ifndef _DISPLAY_INFO_ON_DASHBOARD
#define _DISPLAY_INFO_ON_DASHBOARD

#include "AA_MCP2515.h"
#include "AsyncTimer.h"
#include "Version.h"
#include "ProcessCarData.h"

// CAN frames include 8 bytes of data. We have a total of 24 characters on the dashboard, therefore the characters will be sent
// using multiple CAN frames. The data for this specific CAN ID uses the first two bytes to encode the total number of frames
// and which frame is being sent. This leaves us with 6 bytes to set characters. But, these are UTF (not ASCII) characters,
// so each character uses two bytes. Therefore we can send only three characters per frame.
const uint8_t NumCharsInText = 24;
const uint8_t NumUTFCharsPerFrame = 3;
const uint8_t NumFramesToDisplayText = NumCharsInText / NumUTFCharsPerFrame;

// Update information 5 times a second. Since we need 8 CAN frames to update all the information in 200ms, we need to send one CAN frame every 25ms
const uint32_t TimeToDisplayText = 1000 / 5;
const uint32_t DelayTimeBetweenFrames = TimeToDisplayText / NumFramesToDisplayText;

// Keep track of whenever a CAN frame is observed that was sent to display text on the dashboard. For example, the radio can sometimes send
// information about what's playing on the radio, e.g. every 2.5 seconds. These frames will interfere with the sequence of custom frames we
// want to send ourselves, resulting in either flickering of text or the display freezing for a few seconds. By knowing when such frames are
// sent, we can reset the sending of our frames.
volatile bool bIncomingRadioFrame = false;

enum InfoToDisplay
{
  infoDrivingInfoWithEngineTemp,    // While driving, show turbo boost pressure, current gear and engine temperature
  infoDrivingInfoWithEngineOilTemp, // While driving, show turbo boost pressure, current gear and engine oil temperature
  infoDrivingInfoWithBattery,       // While driving, show turbo boost pressure, current gear and battery voltage
  infoDrivingInfoWithSquadra,       // While driving, show turbo boost pressure, current gear and that Squadra performance tune is enabled
  infoMaxBoost,                     // When car is idling, show information about when maximum turbo boost was obtained
  infoTurboCooldownTimer,           // After a spirited drive, show a timer to cooldown the turbo before switching off the car
  infoWarningLowBattery,            // When car is idling, show warning when car battery is low
  infoWarningColdEngine,            // Don't drive too hard when engine is cold. This warning isn't for me, but for my son when he's driving my car :-)
  NumInfoMessages                   // Total number of info messages
};

bool bIsInfoActive[InfoToDisplay::NumInfoMessages];   // Multiple message could be active at a time, so keep track of which one to display

AsyncTimer timerShowNameAndVersion(10000);            // When car is turned on, show name and version for 10 seconds
AsyncTimer timerWaitBeforeShowingInfoWhileIdle(2000); // Some info show only when car is at ~idle. We don't want to immediately show those, but rather wait 2 seconds
AsyncTimer timerToggleInfoWhileDriving(3000);         // Every 3 seconds toggle info while driving, e.g. like engine temp, engine oil temp, battery V, etc.
AsyncTimer timerToggleInfoWhileIdling(5000);          // Every 5 seconds toggle info while idlings, e.g. max boost, warnings, etc.

// While driving, every 3 seconds toggle from [infoDrivingInfoWithEngineTemp .. infoDrivingInfoWithBattery]
uint8_t infoIndexWhileDriving = infoDrivingInfoWithEngineTemp;
const uint8_t MaxInfoIndexWhileDriving = infoDrivingInfoWithBattery;

// While idling, every 5 seconds toggle from [infoMaxBoost .. infoWarningColdEngine]
uint8_t infoIndexWhileIdling = infoMaxBoost;
const uint8_t MinInfoIndexWhileIdling = infoMaxBoost;

// Setup for the MCP2515 CAN controller connected to the low speed CAN bus, which uses 125Kbps
const CANBitrate::Config CAN_BITRATE = CANBitrate::Config_8MHz_125kbps;
const uint8_t CAN_PIN_CS = SS;
const int8_t CAN_PIN_INT = D6;
CANConfig config(CAN_BITRATE, CAN_PIN_CS, CAN_PIN_INT);
CANController CAN(config);

// Interrupt service routine that will get called whenever a frame with a CAN ID for the dashboard is observed
void OnReceive(CANController&, CANFrame frame)
{
  bIncomingRadioFrame = true;
}

// This will be called from the main setup() function, which will get called each time the device wakes up from deep sleep
void SetupDisplayInfoOnDashboard()
{
  DebugPrintln("SetupDisplayInfoOnDashboard()");

  while (CAN.begin(CANController::Mode::Config) != CANController::OK)
  {
    DebugPrintln("MCP2515 CAN controller failed");
    delay(1000);
  }

  // We're only interested in observing frames from the CAN ID that contains info for the dashboard. Therefore, we setup a mask and filter
  // so that all other frames are filtered out by the hardware, i.e. no need to filter it out in the OnReceive function using if-statements
  // This is an 11-bit CAN ID so the mask is 0b011111111111
  CAN.setFiltersRxb0(DashboardText, 0x00, 0b011111111111, false);
  CAN.setFiltersRxb1(0x00, 0x00, 0x00, 0x00, 0b011111111111, false);
  CAN.setFilters(true);

  CAN.setMode(CANController::Mode::Normal);

  // Using an interrupt to notify us when a new frame from the radio was received is great, since you can immediately respond to it when that frame
  // is observed. Unfortunately using the interrupt sometimes reboots the device. Not sure if it's something specific with the MCP2515 I'm using.
  // Instead of using an interrupt, we'll just manually read frames.
  //CAN.setInterruptCallbacks(&OnReceive, nullptr);

  DebugPrintln("MCP2515 CAN controller initialized");

  // Set all messages inactive
  for (int i = 0; i < InfoToDisplay::NumInfoMessages; i++)
  {
    bIsInfoActive[i] = false;
  }

  // Start the count down timer that monitors turbo cooldown conditions
  timerTurboCooldown.Start();
  timerTurboCooldownMonitor.Start();
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

// The dashboard messages can be group by Radio, Media, Bluetooth, Phone and Navigation. It looks like some groups have higher
// priority than others when it comes to showing something on the dashboard. For example, when using the Radio FM channel for
// custom messages, sometimes the Radio FM will send its own messages and cause the custom message to flicker. But, if you listen
// to Radio AM and you use Radio FM for custom messages, then there is no flickering, until you listen to AM. It's similar to groups,
// e.g. Phone messages seem to be higher priority than Radio messages, therefore Radio messages won't interfere with Phone messages
// or Navigation messages. Below are some message "infoCode" values which I identified.

// 0x00 - 0x01 ?
// 0x02 - FM radio
// 0x03 - AM radio
// 0x05 - Aux
// 0x06 - USB left
// 0x07 - USB right
// 0x08 - USB front
// 0x09 - Bluetooth
const uint8_t InfoCode = 0x05;

// Send one CAN frame to set three of the UTF characters in the text
void SetDashboardTextCharacters(uint8_t numFrames, uint8_t currentFrame, char* text)
{
  const uint8_t indexOfLastFrame = numFrames - 1;
  const uint8_t utfCharStartIndex = 2;  // First UTF character is in canData[2]

  uint8_t canData[8] = { 0 };

  // Num frames - 1, byte[0] bit[7..3]
  canData[0] = (indexOfLastFrame << 3) & 0b11111000;

  // InfoCode, byte[1] bit[5..0]
  canData[1] = InfoCode & 0b00111111;

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

// Given a received radio frame, find the current frame
uint8_t GetCurrentRadioFrame(uint8_t* pData)
{
  // Current frame, byte[0] bit[2..0] and byte[1] bit[7..6]
  uint8_t highBits = (pData[0] & 0b00000111) << 2;
  uint8_t lowBits = (pData[1] & 0b11000000) >> 6;
  return highBits | lowBits;
}

// Given a received radio frame, find the total number of frames
uint8_t GetNumRadioFrames(uint8_t* pData)
{
  // Num frames - 1, byte[0] bit[7..3]
  return (pData[0] >> 3) + 1;
}

// Given a received radio frame, find the info code
uint8_t GetRadioInfoCode(uint8_t* pData)
{
  // InfoCode, byte[1] bit[5..0]
  return pData[1] & 0b00111111;
}

// Used to receive radio CAN frames
CANFrame rxFrame;

// Some interesting observations, maybe this is very specific to my car's infotainmaint/dashboard systems
// - Frames from the radio are sent to the dashboard at 33Hz, or one frame every 30ms
// - In most cases 8 text characters are sent to the dashboard, i.e. 3 frames of text
// - Some radio stations send new frames to the dashboard every 2.5 seconds, others only when you switch to that radio station
// - It seems there is some ACK that the infotainment system requires from the dashboard, otherwise it will resend all three radio frames again after 120ms
// - This means if you interrupt the radio frames in a certain way, you can easily flood the CAN bus, resulting in freezing either custom text or radio text on the dashboard
// - Different visual artifacts can be seen, and it's hard to overcome all of them:
//      - Split second flickering from radio station text when it briefly overwrites our own custom text
//      - Longer 1/4 to 1/2 second periods where the radio station's first 3 characters, or first frame, is shown
//      - Longer 1/4 to 1/2 second periods where the custom text is frozen, i.e. not updating at 5 times a second, meaning data like boost psi and current gear might be old
//      - Short periods, maybe 10th of second, where the dashboard text is either fully cleared or some portions are cleared

// Send multiple CAN frames to display all the text
void SetDashboardText(char* text, uint32_t timeToDisplayText)
{
  unsigned long delayTimeBetweenFrames = DelayTimeBetweenFrames;

  for (int currentFrame = 0; currentFrame < NumFramesToDisplayText; currentFrame++)
  {
    uint8_t characterStartPosition = currentFrame * NumUTFCharsPerFrame;
    SetDashboardTextCharacters(NumFramesToDisplayText, currentFrame, text + characterStartPosition);
    delay(delayTimeBetweenFrames);
   
    // Check if there was a radio frame. Since we setup a hardware filter, we know that the only frames received
    // would be from CAN_Id::DashboardText
    if (CAN.read(rxFrame) == CANController::IOResult::OK)
    {
      uint8_t radioData[8];
      rxFrame.getData(radioData, 8);
      auto numRadioFrames = GetNumRadioFrames(radioData);
      auto currentRadioFrame = GetCurrentRadioFrame(radioData);
      auto radioInfoCode = GetRadioInfoCode(radioData);
      DebugPrintf("Received radio frame: %d\n", currentRadioFrame);

      if (radioInfoCode == InfoCode)
      {
        continue;
      }

      // When we observe the 2nd radio frame, interrupt the radio frames with our own first frame
      if (currentRadioFrame == 1)
      {
        SetDashboardTextCharacters(NumFramesToDisplayText, 0, text);
      }

      // Wait until we see the last radio frame and restart our custom frames
      while (currentRadioFrame < (numRadioFrames - 1))
      {
        if (CAN.read(rxFrame) == CANController::IOResult::OK)
        {
          rxFrame.getData(radioData, 8);
          numRadioFrames = GetNumRadioFrames(radioData);
          currentRadioFrame = GetCurrentRadioFrame(radioData);
          DebugPrintf("Received radio frame: %d\n", currentRadioFrame);

          // When we observe the 2nd radio frame, interrupt the radio frames with our own first frame
          if (currentRadioFrame == 1)
          {
            SetDashboardTextCharacters(NumFramesToDisplayText, 0, text);
          }
        }
      }

      // Restart the loop and start displaying our custom frames from the start, and also shorten the delay time between custom frames
      currentFrame = -1;
      delayTimeBetweenFrames = DelayTimeBetweenFrames - 5;
    }
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
  // Show project name when the car turns on
  if (!timerShowNameAndVersion.RanOut())
  {
    sprintf(text, "    %s   v%1.1f", g_ProjectName, g_Version);
    return;
  }

  if (timerToggleInfoWhileDriving.RanOut())
  {
    timerToggleInfoWhileDriving.Start();

    infoIndexWhileDriving++;
    if (infoIndexWhileDriving > MaxInfoIndexWhileDriving)
    {
      infoIndexWhileDriving = 0;
    }
  }

  static bool bFoundActiveIdleMessage = false;

  InfoToDisplay infoToDisplay = infoDrivingInfoWithEngineTemp;

  char gearText[8] = { 0 };

  // Choose what info to show while driving

#ifdef SHOW_SQUADRA_MESSAGE
  if (IsSquadraEnabled())
  {
    infoToDisplay = InfoToDisplay::infoDrivingInfoWithSquadra;
  }
  else
#endif
  {
    if (IsEngineColdAndHighRPM())
    {
      infoToDisplay = InfoToDisplay::infoWarningColdEngine;
    }
    else
    {
      infoToDisplay = (InfoToDisplay)infoIndexWhileDriving;
    }
  }

  // Check to see if car is kind of idling and show "while idling" information
  if (IsCarIdlingOrInReverse())
  {
    // It's not interesting to display boost psi that is less than 1.0f
    if (IsBoostInfoInteresting())
    {
      // Show info about max boost
      bIsInfoActive[infoMaxBoost] = true;
    }

    if (IsBatteryLow())
    {
      bIsInfoActive[infoWarningLowBattery] = true;
    }

    if (IsEngineColdAndHighRPM())
    {
      bIsInfoActive[infoWarningColdEngine] = true;
    }

    if (IsTurboStillCoolingDown())
    {
      bIsInfoActive[infoTurboCooldownTimer] = true;
    }

    if (timerToggleInfoWhileIdling.RanOut())
    {
      timerToggleInfoWhileIdling.Start();

      const uint8_t numIdleMessages = NumInfoMessages - MinInfoIndexWhileIdling;
      for (uint8_t i = 0; i < numIdleMessages; i++)
      {
        infoIndexWhileIdling++;
        if (infoIndexWhileIdling >= NumInfoMessages)
        {
          infoIndexWhileIdling = MinInfoIndexWhileIdling;
        }

        if (bIsInfoActive[infoIndexWhileIdling])
        {
          bFoundActiveIdleMessage = true;
          break;
        }
      }

      // Set all messages inactive
      for (int i = 0; i < InfoToDisplay::NumInfoMessages; i++)
      {
        bIsInfoActive[i] = false;
      }
    }

    // Wait a little bit before switching to show "while idle" messages, just in case you're driving with very low revs which will
    // mean the messages can flicker when it quicky switches between "while idle" and "while driving"
    if (!timerWaitBeforeShowingInfoWhileIdle.IsActive())
    {
      timerWaitBeforeShowingInfoWhileIdle.Start();
    }

    if (timerWaitBeforeShowingInfoWhileIdle.RanOut())
    {
      if (bFoundActiveIdleMessage)
      {
        infoToDisplay = (InfoToDisplay)infoIndexWhileIdling;
      }
    }
  }
  else
  {
    bFoundActiveIdleMessage = false;
    timerWaitBeforeShowingInfoWhileIdle.Stop();
  }

  switch (infoToDisplay)
  {    
    case InfoToDisplay::infoDrivingInfoWithEngineTemp:
    {
      // Example:   " 23 psi   D1   Eng 200*F"
      GenerateGearText(carData.Gear, gearText); // Current gear
      float farh = (carData.EngineTemp * 9.5f / 5.0f) + 32.0f;
      sprintf(text, " %2d psi   %s   Eng %3d*F", int32_t(turboBoostPsi + 0.5f), gearText, int32_t(farh + 0.5f));
      break;
    }

    case InfoToDisplay::infoDrivingInfoWithEngineOilTemp:
    {
      // Example:   " 23 psi   D1   Oil 200*F"
      GenerateGearText(carData.Gear, gearText); // Current gear
      float farh = (carData.EngineOilTemp * 9.5f / 5.0f) + 32.0f;
      sprintf(text, " %2d psi   %s   Oil %3d*F", int32_t(turboBoostPsi + 0.5f), gearText, int32_t(farh + 0.5f));
      break;
    }

    case InfoToDisplay::infoDrivingInfoWithBattery:
    {
      // Example:   " 23 psi   D1   Bat 12.6V"
      GenerateGearText(carData.Gear, gearText); // Current gear
      sprintf(text, " %2d psi   %s   Bat %2.1fV", int32_t(turboBoostPsi + 0.5f), gearText, carData.Battery);
      break;
    }

    case InfoToDisplay::infoDrivingInfoWithSquadra:
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

    case InfoToDisplay::infoTurboCooldownTimer:
    {
      // Example:   "Turbo cooling down 1:12 "
      auto secondsLeft = GetTurboCooldownSeconds();
      if (secondsLeft > 0)
      {
        int32_t min = secondsLeft / 60;
        int32_t sec = secondsLeft - (min * 60);
        sprintf(text, "Turbo cooling down  %1d:%02d", min, sec);
      }
      else
      {
        sprintf(text, "    Turbo cooled down   ");
      }
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
  }

  //DebugPrintln(text);
}

// Main function of the thread task running on a seperate ESP32-S3 core
void DisplayInfoOnDashboard(void* params)
{
  DebugPrintf("Core %d: DisplayInfoOnDashboard()\n", xPortGetCoreID());

  timerToggleInfoWhileDriving.Start();
  timerToggleInfoWhileIdling.Start();

  // Just for safety, we make the text twice as long as we realy need
  char text[NumCharsInText * 2] = "Initializing .....";

  while (true)
  {
    CopyCarData();

    // Only send CAN from to the dashboard if the car is actually turned on. It looks like the act of sending frames to the dashboard
    // after the car turned off keeps the car in an "active" state, draining the battery.
    if (carData.bCarTurnedOn)
    {
      ProcessCarData();
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