// Continously collect car data on one of the ESP32-S3 cores. This uses the built-in CAN controller of the ESP32-S3 together with a
// connected SN65HVD230 CAN bus transceiver. ESP32 TWAI (Two-Wire Automotive Interface) is used to communicate with the CAN bus.

#ifndef _COLLECT_CAR_DATA
#define _COLLECT_CAR_DATA

#include <ESP32-TWAI-CAN.hpp>   // TWAI = Two-Wire Automotive Interface
#include "OBD2Calculations.h"   // Callback functions for OBD2 PIDs
#include "OBD2Utils.h"          // Misc helper functions for OBD2

// Define our OBD2 PIDs for Alfa Romeo Giulia
PID PIDs[] = { { "Boost Pressure",        CarModule::ECM, OBD2Service::ManufacturerSpecific, 0x195a, &CalcBoostPressure,        PrintBoostPressure },
               { "Engine Temp",           CarModule::ECM, OBD2Service::ManufacturerSpecific, 0x1003, &CalcEngineTemp,           PrintEngineTemp },
               { "Engine Oil Temp",       CarModule::ECM, OBD2Service::ManufacturerSpecific, 0x1302, &CalcEngineOilTemp,        PrintEngineOilTemp },
               { "Exhaust Gas Temp",      CarModule::ECM, OBD2Service::ManufacturerSpecific, 0x18ba, &CalcExhaustGasTemp,       PrintExhaustGasTemp },
               { "Atmospheric Pressure",  CarModule::ECM, OBD2Service::ManufacturerSpecific, 0x1956, &CalcAtmosphericPressure,  PrintAtmosphericPressure },
               { "Ignition Key Position", CarModule::BCM, OBD2Service::ManufacturerSpecific, 0x0131, &CalcIgnitionKeyPosition,  PrintIgnitionKeyPosition },
               { "Battery",               CarModule::ECM, OBD2Service::ManufacturerSpecific, 0x1004, &CalcBattery,              PrintBattery } };

// Index into the above PIDs[] declaration
enum PIDIndex
{
  BoostPressure,
  EngineTemp,
  EngineOilTemp,
  ExhaustGasTemp,
  AtmosphericPressure,
  IgnitionKeyPosition,
  Battery,
  NumPIDs
};

PID* pBoostPressure       = &PIDs[PIDIndex::BoostPressure];
PID* pEngineTemp          = &PIDs[PIDIndex::EngineTemp];
PID* pEngineOilTemp       = &PIDs[PIDIndex::EngineOilTemp];
PID* pExhaustGasTemp      = &PIDs[PIDIndex::ExhaustGasTemp];
PID* pAtmosphericPressure = &PIDs[PIDIndex::AtmosphericPressure];
PID* pIgnitionKeyPosition = &PIDs[PIDIndex::IgnitionKeyPosition];
PID* pBattery             = &PIDs[PIDIndex::Battery];

AsyncTimer timerHighFrequency(200);       // Collect boost, etc. at high frequency, 5 times per second
AsyncTimer timerLowFrequency(1000);       // Collect ignition key position, etc. only once a second
AsyncTimer timerVeryLowFrequency(10000);  // Collect oil temp, atmospheric pressure, battery, etc. only every 10 seconds

// Configuration to set SN65HVD230 in low power Listen Only mode
twai_general_config_t listenOnlyConfig = TWAI_GENERAL_CONFIG_DEFAULT(gpio_num_t(TXPin), gpio_num_t(RXPin), TWAI_MODE_LISTEN_ONLY);

// Switch SN65HVD230 to low power Listen Only mode
void ListenOnlyMode_SN65HVD230()
{
  ESP32Can.begin(TWAI_SPEED_500KBPS, TXPin, RXPin, 0, 1024, nullptr, &listenOnlyConfig);
}

// Switch SN65HVD230 to Normal mode
void NormalMode_SN65HVD230()
{
  // NOTE about queue sizes:
  // There will be a multitude of non-OBD2 CAN frames observed over the high speed CAN bus. Normally we'd setup a hardware filter to
  // receive only the small set of OBD2 frames in the received messages queue. But, since we also need to read some of the non-OBD2 frames, we
  // can't setup a hardware filter, therefore need to make sure the size of the queue receiving frames is large enough. If not, the read queue will
  // fill up faster than what we can process. Once the queue is full, new frames will be missed. I realized this only after some frustration
  // when using a size of 16, which worked perfectly fine when using a hardware filter. Larger queue sizes do use more memory. Allocating one
  // CAN frame uses sizeof(twai_message_t) which 13 bytes. Read and write queue sizes of 256 and 1024 means (256 + 1024) * 13 = 16KB memory. That's
  // a lot for a microcontroller. Luckily the ESP32-S3 has 512KB of memory.

  ESP32Can.begin(TWAI_SPEED_500KBPS, TXPin, RXPin, 256, 1024);
}

// This will be called from the main setup() function, which will get called each time the device wakes up from deep sleep
void SetupCollectCarData()
{
  DebugPrintln("SetupCollectCarData()");

  memset(&g_CurrentCarData, 0, sizeof(g_CurrentCarData));

  NormalMode_SN65HVD230();

  // Start timers
  timerHighFrequency.Start();
  timerLowFrequency.Start();
  timerVeryLowFrequency.Start();

  // Send requests for low freqency now
  SendOBD2Request(pEngineOilTemp);
  SendOBD2Request(pExhaustGasTemp);
  SendOBD2Request(pAtmosphericPressure);
  SendOBD2Request(pIgnitionKeyPosition);
  SendOBD2Request(pBattery);
}

// Send all the OBD2 requests
void SendOBD2Requests()
{
  // This happens 5x per second
  if (timerHighFrequency.RanOut())
  {
    timerHighFrequency.Start();
    SendOBD2Request(pBoostPressure);
  }

  // This happens once per second
  if (timerLowFrequency.RanOut())
  {
    timerLowFrequency.Start();
    SendOBD2Request(pIgnitionKeyPosition);
    SendOBD2Request(pExhaustGasTemp);
  }

  // This happens every 10 seconds
  if (timerVeryLowFrequency.RanOut())
  {
    timerVeryLowFrequency.Start();
    SendOBD2Request(pEngineTemp);
    SendOBD2Request(pEngineOilTemp);
    SendOBD2Request(pAtmosphericPressure);
    SendOBD2Request(pBattery);
  }
}

// Listen for CAN frames and process them
void ProcessReceivedCANFrames()
{
  CanFrame receivedCANFrame;

  while (ESP32Can.readFrame(receivedCANFrame, 0))   // Read frames without blocking
  {
    auto canID = receivedCANFrame.identifier;

    if (IsValidCarModule(canID))
    {
      auto pid = GetPID(receivedCANFrame);

      for (int i = 0; i < NumPIDs; i++)
      {
        if (pid == PIDs[i].PID)
        {
          PIDs[i].CalculateValue(receivedCANFrame.data);
          //PIDs[i].PrintInformation();
          break;
        }
      }
    }
    else    // Process "custom" CAN frames that aren't specifically defined OBD2 frames
    {
      if (receivedCANFrame.data_length_code == 8)
      {
        if (canID == CAN_Id::DriveMode)
        {         
          CalcIDriveMode_FromBroadcastedFrame(receivedCANFrame.data);
        }

        if (canID == CAN_Id::GearInfo)
        {
          CalcGear_FromBroadcastedFrame(receivedCANFrame.data);
        }

        if (canID == CAN_Id::EngineRPM)
        {
          CalcEngineRPM_FromBroadcastedFrame(receivedCANFrame.data);
        }

        // This is quick to find, since we don't need to first do an OBD2 request. But, not accurate enough at high boost levels
        // if (canID == CAN_Id::Boost)
        // {
        //   CalcBoostPressure_FromBroadcastedFrame(receivedCANFrame.data);
        // }
      }
    }
  }

  // Update data to be shared with the other ESP32-S3 core
  xSemaphoreTake(g_SemaphoreCarData, portMAX_DELAY);
  g_CurrentCarData.Gear = g_Gear;
  g_CurrentCarData.EngineRPM = g_EngineRPM;
  g_CurrentCarData.EngineTemp = g_EngineTemp;
  g_CurrentCarData.EngineOilTemp = g_EngineOilTemp;
  g_CurrentCarData.ExhaustGasTemp = g_ExhaustGasTemp;
  g_CurrentCarData.AtmosphericPressure = g_AtmosphericPressure;
  g_CurrentCarData.BoostPressure = g_BoostPressure;
  g_CurrentCarData.DriveMode = g_DriveMode;
  g_CurrentCarData.Battery = g_Battery;
  g_CurrentCarData.bCarTurnedOn = (g_IgnitionKeyPosition != IgnitionKeyPosition::Off);
  xSemaphoreGive(g_SemaphoreCarData);
}

void CollectCarData()
{
  SendOBD2Requests();
  ProcessReceivedCANFrames();
}

#endif