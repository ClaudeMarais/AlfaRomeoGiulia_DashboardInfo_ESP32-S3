// The OBD2 connector has an always-on 12V pin where the device will be powered from. Since it's always on, it will unnecessary draw power when the car
// is turned off. One option would be to just always unplug the device when not driving, another might be to add a button to the device to manually
// switch it on/off. In this project, we simply detect if the car is turned on and if not, we put the device into deep sleep mode to heavily reduce
// the amount of power used. We'll check for 5 seconds if the car is on, then sleep for 12 seconds, etc. While trying to see if the car turns on,
// the device will draw ~40mA/190mW, but only ~1mA/1mW while in deep sleep.

#ifndef _HANDLE_LOW_POWER_STATE
#define _HANDLE_LOW_POWER_STATE

// If the car is powered off, put the device into deep sleep mode and wake it up after 12 seconds to check if the car was turned on again.
// Why 12 seconds? From experimentation, it looks like anything shorter than 10 seconds can potentially keep the car in an "active" state,
// e.g. light around volume knob and on electronic brake button stays on, and non-OBD2 CAN frames continue to be broadcasted.
const uint64_t DeepSleepTime = 12 * 1000000ULL;       // 12 seconds at ~1mA/1mW

// When the device is awake, wait for 5 seconds trying to see if the car turns on, and then go to deep sleep for 12 seconds again.
AsyncTimer timerWaitBeforeGoingIntoDeepSleep(5000);   // 5 seconds at ~40mA/190mW

void DeepSleep()
{
  DebugPrintln("Going into deep sleep");

  // SN65HVD230 might be in Normal mode, so switch it to low power Listen Only mode during deep sleep
  ListenOnlyMode_SN65HVD230();

  // Stop the thread that's updating the dashboard display
  if (g_TaskDisplayInfoOnDashboard)
  {
    // At this point g_CurrentCarData.bCarTurnedOn should be false, which will make sure that no CAN frames are sent to the
    // dashboard from the other thread. But, just in case that's not the case, let's force it to be false, since we don't want
    // to be in the middle of sending a CAN frame while shutting down the other thread
    xSemaphoreTake(g_SemaphoreCarData, portMAX_DELAY);
    g_CurrentCarData.bCarTurnedOn = false;
    xSemaphoreGive(g_SemaphoreCarData);
    delay(500);

    vTaskSuspend(g_TaskDisplayInfoOnDashboard);

    // Put MCP2515 into low power sleep mode
    SleepMCP2515();

    // Delete thread that's updating the dashboard display
    vTaskDelete(g_TaskDisplayInfoOnDashboard);
    g_TaskDisplayInfoOnDashboard = nullptr;
  }

#ifdef DEBUG
  Serial.flush();
#endif

  // Go into deep sleep
  g_bInDeepSleep = true;
  esp_sleep_enable_timer_wakeup(DeepSleepTime);
  esp_deep_sleep_start();
}

// Check if there are any CAN frames on the high speed CAN bus
bool ReceviedAnyCANFrame()
{
  CanFrame receivedCANFrame;

  // Switch SN65HVD230 to low power Listen Only mode
  ListenOnlyMode_SN65HVD230();

  // The low speed CAN bus sometimes has traffic even while the car is switched off. But, the high speed CAN bus only has traffic when the car ignition is on.
  // Therefore, we can use Listen Only mode to check if we recognize a CAN Id that indicates the car is turned on
  while (timerWaitBeforeGoingIntoDeepSleep.IsActive() &&
         !timerWaitBeforeGoingIntoDeepSleep.RanOut())
  {
    while (ESP32Can.readFrame(receivedCANFrame))
    {
      auto canID = receivedCANFrame.identifier;

      // From experimentation, the drive mode (DNA) CAN frame is sent frequently when car is on
      if (canID == CAN_Id::DriveMode &&
          receivedCANFrame.data_length_code == 8)
      {
        // We found a valid frame, so we assume the car is turned on, but can't be 100% sure
        return true;
      }
    }

    delay(1000);
  }

  // We didn't see any valid CAN frames, so we know for sure the car is turned off
  return false;
}

// Check if the car ignition is on. For this we require to set the SN65HVD230 into Normal mode so that we can send an OBD2 request
bool CarIgnitionOn()
{
  if (g_IgnitionKeyPosition != IgnitionKeyPosition::Off)
  {
    return true;
  }

  CanFrame receivedCANFrame;

  // The ignition is off, but it might be that there wasn't a recent OBD2 request sent to update the status, so let's send one now to verify
  NormalMode_SN65HVD230();
  SendOBD2Request(pIgnitionKeyPosition);

  while (timerWaitBeforeGoingIntoDeepSleep.IsActive() &&
         !timerWaitBeforeGoingIntoDeepSleep.RanOut())
  {
    while (ESP32Can.readFrame(receivedCANFrame))
    {
      auto canID = receivedCANFrame.identifier;
      if (IsValidCarModule(canID))
      {
        auto pid = GetPID(receivedCANFrame);
        if (pid == pIgnitionKeyPosition->PID)
        {
          pIgnitionKeyPosition->CalculateValue(receivedCANFrame.data);
          return (g_IgnitionKeyPosition != IgnitionKeyPosition::Off);
        }
      }
    }
    
    delay(500);
  }

  return false;
}

// Wait for car to turn on
void WaitForCarToTurnOn()
{
  #ifdef DISABLE_POWER_SAVING_CHECKS
  return;
  #endif

  DebugPrintln("Waiting for car to turn on");

  timerWaitBeforeGoingIntoDeepSleep.Start();

  // Using low power state
  if (!ReceviedAnyCANFrame())
  {
    DebugPrintln("WaitForCarToTurnOn: Car is turned OFF, no CAN frames received");
    DeepSleep();
  }
  
  DebugPrintln("WaitForCarToTurnOn: Received a CAN frame, it's possible that car is turned on");

  // Using normal power state
  if (!CarIgnitionOn())
  {
    DebugPrintln("WaitForCarToTurnOn: Car is turned OFF, the ignition is off");
    DeepSleep();
  }

  timerWaitBeforeGoingIntoDeepSleep.Stop();
  DebugPrintln("WaitForCarToTurnOn: Car is turned ON, the ignition is on");
}

// An OBD2 PID is used to determine the car's Ignition Key Position
void CheckIfCarIsStillOn()
{
  #ifdef DISABLE_POWER_SAVING_CHECKS
  return;
  #endif

  timerWaitBeforeGoingIntoDeepSleep.Start();

  if (!CarIgnitionOn())
  {
    DebugPrintln("CheckIfCarIsStillOn: Car is turned OFF, the ignition is off");
    DeepSleep();
  }

  timerWaitBeforeGoingIntoDeepSleep.Stop();
  //DebugPrintln("CheckIfCarIsStillOn: Car is turned ON, ignition is on");
}

#endif