# Display info on dashboard of Alfa Romeo Giulia using ESP32-S3
 
An Arduino project to display useful information like turbo boost pressure, current gear, performance tune status, etc., directly on the dashboard of an Alfa Romeo Giulia. The information is displayed in the location on the instrument cluster where the infotainment information is normally displayed, e.g. displaying the radio station name.


| Example Message                   | Description                                                                                                        |
|-----------------------------------|--------------------------------------------------------------------------------------------------------------------|
| 23 psi &nbsp; D2 &nbsp; Oil 200*F | While driving, show turbo boost pressure, current gear and oil temp                                                |
| 23 psi &nbsp; D2 &nbsp; Bat 12.6V | While driving, show turbo boost pressure, current gear and battery voltage                                         |
| 23 psi &nbsp; D2 &nbsp; Squadra   | If in Dynamic drive mode and oil temp is above 70*C, the Squadra performance tune is fully enabled                 |
| Max 23 psi @ 5200 rpm D2          | When car is just idling, e.g. at red traffic light, show interesting information when max turbo boost was measured |
| Battery is low! &nbsp;12.2V       | A warning message when the car battery is lower than 12.4V                                                         |
| Careful, engine is cold           | A warning message when engine speed is higher than 3000 RPM while the engine oil temperature is below 70*C         |

Car data like RPM and boost pressure can be retrieved from the high speed CAN bus, but sending text to the dashboard uses the low speed CAN bus. Since one CAN controller can only communicate on one CAN bus, we require two CAN controllers. The ESP32-S3 has a built-in CAN controller, so we connect a SN65HVD230 CAN transceiver to the ESP32-S3 which communicates on the high speed bus. A separate MCP2515 CAN bus controller with its own TJA1050 transceiver is used to communicate on the low speed CAN bus. The ESP32-S3 has two cores, so it's convenient to continuously collect car data on one core from the high speed CAN bus, while at the same time send information to the dashboard on the low speed CAN bus using the other ESP32-S3 core.

The OBD2 connector has an always-on 12V pin where the device will be powered from. Since it's always on, it will unnecessary draw power when the car is turned off. One option would be to just always unplug the device when not driving, another might be to add a button to the device to manually switch it on/off. In this project, we simply detect if the car is turned on and if not, we put the device into deep sleep mode to heavily reduce the amount of power used. We'll check for 5 seconds if the car is on, then sleep for 12 seconds, etc. While trying to see if the car turns on, the device will draw ~40mA/190mW, but only ~1mA/1mW while in deep sleep.

NOTE: It's fun to tinker with your car, but there is always a chance to mess things up. I won't be liable if for some reason you damage your car.

NOTE: The CAN IDs and PIDs used in this project specifically works with a 2019 Alfa Romeo Giulia 2.0L (Petrol). It's highly unlikely that the same PIDs will work with another car, you'll have to research what PIDs work with your own car.

A big thank you to the Alfisti community for reverse enginering some of these PIDs, especially Gaucho https://github.com/gaucho1978/BACCAble

Some tips:

 - Consider connecting your car to a battery charger while experimenting. It's highly likely that you'll spend several hours in your car while the battery is being drained.
 - Diagrams of OBD2 pins are normally shown for the female connector in your car. Don't forget that those pins are in swapped/mirrored positions on the male connector.
 - The OBD2 connector has an "always on" 12V pin. Make sure the wire connecting to that pin on your male connector isn't exposed so that it cannot touch other wires!
 - I tried multiple pins on the ESP32-S3 to connect to the SN65HVD230, but only D4/D5 worked for me. Coincidentally these are also the SDA/SCL pins.
 - Check if your car has an OBD2 Security Gateway (SGW). If so, you need to install a SGW Bypass module before you to send/receive OBD2 frames to your car.

Hardware:
 - XIAO ESP32-S3
 - 12V to 5V Voltage regulator
 - SN65HVD230 CAN bus tranceiver
 - MCP2515 CAN bus controller


Arduino libraries used:
 - ESP32-TWAI-CAN: https://github.com/handmade0octopus/ESP32-TWAI-CAN
 - MCP2515: https://github.com/codeljo/AA_MCP2515

![Intro](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/Intro.jpg?raw=true)

Example 1: Turbo boost pressure and currently engaged gear is displayed while driving. When car only idles, max boost pressure info (or warning messages) is displayed. Here it also indicates that the Squadra performance tune is fully enabled, since the engine oil is above 70*C and the car is in Dynamic drive mode.

![Demo_MaxBoostPsi](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/Demo_MaxBoostPsi_320p.gif?raw=true)

Example 2: When engine is still cold and RPM is above 3000, show a cautionary warning message while driving and when idling.

![Demo_ColdEngine](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/Demo_ColdEngine_320p.gif?raw=true)

Some example messages:

![Examples](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/Examples.jpg?raw=true)

![Design](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/Design.jpg?raw=true)

![Design2](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/Design2.jpg?raw=true)

![Components](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/Components.jpg?raw=true)

![OBD2_Pins](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/OBD2_Pins.jpg)?raw=true

![Power](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/Power.jpg?raw=true)

![SN65HVD230_Wire_Diagram](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/SN65HVD230_Wire_Diagram.jpg?raw=true)

![MCP2515_Wire_Diagram](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/MCP2515_Wire_Diagram.jpg?raw=true)

![Full_Wire_Diagram](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/Full_Wire_Diagram.jpg?raw=true)

![TerminationResistors](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/TerminationResistors.jpg?raw=true)

![CutSmaller](https://github.com/ClaudeMarais/AlfaRomeoGiulia_DashboardInfo_ESP32-S3/blob/main/Images/CutSmaller.jpg?raw=true)