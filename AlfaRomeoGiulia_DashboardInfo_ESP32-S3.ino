// An Arduino project to display useful information like turbo boost pressure, current gear, performance tune status, etc., directly on
// the dashboard of an Alfa Romeo Giulia. The information is displayed in the location on the instrument cluster where the infotainment
// information is normally displayed, e.g. displaying the radio station name.
//
//  Example Messages:
//
// |-----------------------------|--------------------------------------------------------------------------------------------------------------------|
// | Message                     | Description                                                                                                        |
// |-----------------------------|--------------------------------------------------------------------------------------------------------------------|
// | 23 psi  D2  Eng 200*F       | While driving, show turbo boost pressure, current gear and engine temp                                             |
// | 23 psi  D2  Oil 200*F       | While driving, show turbo boost pressure, current gear and engine oil temp                                         |
// | 23 psi  D2  Bat 12.6V       | While driving, show turbo boost pressure, current gear and battery voltage                                         |
// | 23 psi  D2  Squadra         | If in Dynamic drive mode and oil temp is above 70*C, the Squadra performance tune is fully enabled                 |
// | Max 23 psi @ 5200 rpm D2    | When car is just idling, e.g. at red traffic light, show interesting information when max turbo boost was measured |
// | Battery is low! 12.2V       | A warning message when the car battery is lower than 12.4V                                                         |
// | Careful, engine is cold     | A warning message when engine speed is higher than 3000 RPM while the engine oil temperature is below 70*C         |
// |-----------------------------|--------------------------------------------------------------------------------------------------------------------|
//
// Car data like RPM and boost pressure can be retrieved from the high speed CAN bus, but sending text to the dashboard uses the low speed CAN bus.
// Since one CAN controller can only communicate on one CAN bus, we require two CAN controllers. The ESP32-S3 has a built-in CAN controller, so we
// connect a SN65HVD230 CAN transceiver to the ESP32-S3 which communicates on the high speed bus. A separate MCP2515 CAN bus controller with its own
// TJA1050 transceiver is used to communicate on the low speed CAN bus.
//
// The ESP32-S3 has two cores, so it's convenient to continuously collect car data on one core from the high speed CAN bus, while at the same time send
// information to the dashboard on the low speed CAN bus using the other ESP32-S3 core.
//
// NOTE: It's fun to tinker with your car, but there is always a chance to mess things up. I won't be liable if for some reason you damage your car.
//
// NOTE: The CAN IDs and PIDs used in this project specifically works with a 2019 Alfa Romeo Giulia 2.0L (Petrol).
//       It's highly unlikely that the same PIDs will work with another car, you'll have to research what PIDs work with your own car.
//
// A big thank you to the Alfisti community for reverse enginering some of these PIDs, especially https://github.com/gaucho1978/BACCAble
//
// Some tips:
//
// 1) Consider connecting your car to a battery charger while experimenting. It's highly likely that you'll spend several hours in your car while the battery is being drained.
// 2) Diagrams of OBD2 pins are normally shown for the female connector in your car. Don't forget that those pins are in swapped/mirrored positions on the male connector.
// 3) The OBD2 connector has an "always on" 12V pin. Make sure the wire connecting to that pin on your male connector isn't exposed so that it cannot touch other wires!
// 4) I tried multiple pins on the ESP32-S3 to connect to the SN65HVD230, but only D4/D5 worked for me. Coincidentally these are also the SDA/SCL pins.
// 5) Check if your car has an OBD2 Security Gateway (SGW). If so, you need to install a SGW Bypass module before you to send/receive OBD2 frames to your car.

// Hardware:
//    XIAO ESP32-S3
//    12V to 5V Voltage regulator
//    SN65HVD230 CAN bus tranceiver
//    MCP2515 CAN bus controller
//
// Arduino library used:
//    ESP32-TWAI-CAN: https://github.com/handmade0octopus/ESP32-TWAI-CAN
//    MCP2515: https://github.com/codeljo/AA_MCP2515

// The final build should have this commented out
// When this is defined, Serial output will happen, otherwise not
//#define DEBUG 1

// The final build should have this commented out
// It's sometimes easier to debug without the device going into sleep mode regularly
//#define DISABLE_POWER_SAVING_CHECKS 1

// This define allows you to see a custom message when the Squadra performance tune is fully active
// If you don't have the Squadra tune, comment this out
#define SHOW_SQUADRA_MESSAGE 1

#include "Shared.h"
#include "AsyncTimer.h"
#include "CollectCarData.h"
#include "DisplayInfoOnDashboard.h"
#include "HandleLowPowerState.h"

void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
  delay(200);

  Serial.printf("\n\n********* Display info on dashboard of Alfa Romeo Giulia using ESP32-S3 *********\n\n");
#endif

  // It's a good idea to reboot the device into a clean state and just start fresh from the setup() function, especially since we're working with multiple threads
  if (g_bInDeepSleep)
  {
    g_bInDeepSleep = false;
    DebugPrintln("Woke up from deep sleep");
    DebugPrintln("Rebooting device into clean state");

#ifdef DEBUG
    Serial.flush();
#endif

    ESP.restart();
  }

  // If the car is turned on we'll continue with the rest of the setup. If it's not, wait a while to see if it turns on, othrewise go into deep sleep
  WaitForCarToTurnOn();

  // Switch onboard LED on during setup
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  SetupCollectCarData();
  SetupDisplayInfoOnDashboard();

  // Car data will be collected on ESP32-S3 core 1 and used on core 0, so we need to use a semaphore to make sure data is handled safely between the cores
  g_SemaphoreCarData = xSemaphoreCreateBinary();
  xSemaphoreGive(g_SemaphoreCarData);

  // Create task that will display info on dashboard using ESP32-S3 core 0
  xTaskCreatePinnedToCore(DisplayInfoOnDashboard, nullptr, 1024 * 64, nullptr, 1, &g_TaskDisplayInfoOnDashboard, 0);

  // Turn onboard LED off if successfully initialized
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop()
{
  CollectCarData();
  CheckIfCarIsStillOn();
}
