// Data is collected on one thread, then displayed on another. But, before we know what to display, the data first needs to be processed.

#ifndef _PROCESS_CAR_DATA
#define _PROCESS_CAR_DATA

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
const int32_t SquadraSafeOilTemperature = 70;   // Squadra is only fully enabled when engine oil reaches 70*C, so use that as a safety temp for when engine is still cold
const int32_t TurboCooldownOilTemperature = 60; // If the engine oil is still relatively cold, it's highly likely that the turbo is still relatively cold too

// It's recommended to cool down your turbo based on driving habits. Unfortunately, we don't have a temperature sensor for the turbo. Therefore, we'll use the
// engine RPM combined with Exhaust Gas Temperature (EGT), which refers to the hot mixture of gases leaving the engine after combustion and then directly enteres
// the turbo. These two data points will control a countdown timer to estimate a safe turbo cooldown period.

struct TurboCooldownInfo
{
  int32_t EngineRPM;
  int32_t ExhaustGasTemp;
  unsigned long CooldownDuration;
};

// We define that the turbo is starting to cool down when RPM is below 2100 and ETG is below 1000F. Above that, we define some cooldown time for each defined "zone"
TurboCooldownInfo turboCooldownInfo[] = { { 4000, 816, 180 * 1000},   // Turbo very hot during spirited driving (above 816*C / 1400*F or 4000 RPM), cool down for 3 min
                                          { 3500, 703,  60 * 1000},   // Turbo getting very hot (above 703*C / 1300F or 3500 RPM), cool down for 60 sec
                                          { 2600, 649,  30 * 1000},   // Turbo hot (above 649*C / 1200F or 2500 RPM), cool down for 30 sec
                                          { 2100, 538,   0 * 1000} }; // Turbo warm (above 538*C / 1000F or 2100 RPM)
 

const int32_t MaxTurboCooldownDuration = turboCooldownInfo[0].CooldownDuration;
const float SpritedDrivingBoostPressure = 20.0f;    // If turbo boost pressure is higher than 20psi, we assume some spirited driving

AsyncTimer timerTurboCooldown(0);
AsyncTimer timerTurboCooldownMonitor(5 * 1000);   // Monitor turbo cooldown data every 5 seconds

// Monitor RPM and EGT during the 5 second period to determine cooldown duration
int32_t monitorMaxEngineRPM = 0;
int32_t monitorMaxExhaustGasTemp = 0;

// Make a local copy of car data that was gathered on the other ESP32-S3 core
void CopyCarData()
{
  xSemaphoreTake(g_SemaphoreCarData, portMAX_DELAY);
  memcpy(&carData, &g_CurrentCarData, sizeof(CarData));
  xSemaphoreGive(g_SemaphoreCarData);
}

// Squadra tune is only enabled in Dynamic drive mode and only fully enabled when engine oil reaches 70*C (158*F)
inline bool IsSquadraEnabled()
{
  return (carData.DriveMode == DNASelector::D &&
          carData.EngineOilTemp >= SquadraSafeOilTemperature);
}

inline bool IsEngineColdAndHighRPM()
{
  return (carData.EngineOilTemp < SquadraSafeOilTemperature &&
          carData.EngineRPM > ColdEngineSafeRPM);
}

inline bool IsCarIdlingOrInReverse()
{
  return (carData.EngineRPM < 1000 ||   // Engine is barely above idle
          carData.Gear == -1);
}

inline bool IsBatteryLow()
{
  return (carData.Battery > 0.0f &&
          carData.Battery < 12.4f);
}

inline bool IsBoostInfoInteresting()
{
  return (maxBoostPsi > 1.0f);
}

inline bool IsTurboStillCoolingDown()
{
  return !timerTurboCooldown.RanOut();
}

inline unsigned long GetTurboCooldownSeconds()
{
  return timerTurboCooldown.GetTimeLeft() / 1000;
}

// Need to process data before we can determine which messages to display
void ProcessCarData()
{
  if (timerTurboCooldown.GetTimeLeft() > MaxTurboCooldownDuration)
  {
    timerTurboCooldown.Start();
  }

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

  // Keep track of when RPM is high when engine is still cold
  if (carData.EngineOilTemp < SquadraSafeOilTemperature)
  {
    maxColdRPM = _max(maxColdRPM, carData.EngineRPM);
  }
  else
  {
    maxColdRPM = 0;   // Engine is warmed up, so reset this value
  }

  // Monitor data to determine turbo cooldown duration
  if (!timerTurboCooldownMonitor.RanOut())
  {
    monitorMaxEngineRPM = _max(monitorMaxEngineRPM, carData.EngineRPM);
    monitorMaxExhaustGasTemp = _max(monitorMaxExhaustGasTemp, carData.ExhaustGasTemp);
  }
  else
  {
    // Restart monitor
    timerTurboCooldownMonitor.Start();

    DebugPrintf("\nTurbo cooldown max: %d RPM    ETG %d*F\n", monitorMaxEngineRPM, int32_t((float(monitorMaxExhaustGasTemp) * 9.0f / 5.0f) + 32.0f + 0.5f));

    // Determine turbo cooldown duration
    unsigned long turboCooldownDuration = 0;

    const int arraySize = sizeof(turboCooldownInfo) / sizeof(turboCooldownInfo[0]);

    for (int i = 0; i < arraySize; i++)
    {
      if (monitorMaxEngineRPM > turboCooldownInfo[i].EngineRPM ||
          monitorMaxExhaustGasTemp > turboCooldownInfo[i].ExhaustGasTemp)
      {
        turboCooldownDuration = turboCooldownInfo[i].CooldownDuration;
        break;
      }
    }

    // If engine is still cold, then hopefully the turbo is too
    if (carData.EngineOilTemp < TurboCooldownOilTemperature)
    {
      turboCooldownDuration = 0;
    }

    // If boost is high, we assume spirited driving, independant of what RPM and EGT is
    if (turboBoostPsi > SpritedDrivingBoostPressure)
    {
      turboCooldownDuration = MaxTurboCooldownDuration;
    }

    if (turboCooldownDuration > timerTurboCooldown.GetTimeLeft())
    {
      timerTurboCooldown.Start(turboCooldownDuration);
      DebugPrintf("New turbo cooldown duration: %d\n", turboCooldownDuration);
    }

    monitorMaxEngineRPM = carData.EngineRPM;
    monitorMaxExhaustGasTemp = carData.ExhaustGasTemp;
  }
}

#endif