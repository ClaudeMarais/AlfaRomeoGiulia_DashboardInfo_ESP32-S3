// Calculate car data values from OBD2 data

#ifndef _OBD2_CALCULATIONS
#define _OBD2_CALCULATIONS

// --------------------------------------------------------
// ******** Engine RPM ************************************
// --------------------------------------------------------

static int32_t g_EngineRPM = 0;

int32_t CalcEngineRPM(const uint8_t* pData)
{
  uint8_t A = pData[4];
  uint8_t B = pData[5];
  g_EngineRPM = ((int32_t(A) * 256) + int32_t(B)) / 4;
  return g_EngineRPM;
}

// Engine RPM can also be calcuated from a non-OBD2 CAN frame. This is much quicker (every ~50ms), since it's not required to send an
// OBD2 request and then wait for the information to be returned.
int32_t CalcEngineRPM_FromBroadcastedFrame(const uint8_t* pData)
{
  // Engine rpm is in byte 0 and 1 (the least significant 2 bits of byte 1 are not related to rpm speed, and should not be used)
  uint8_t A = pData[0];
  uint8_t B = pData[1];
  g_EngineRPM = (A * 256 + (B & ~0x3)) / 4;
  return g_EngineRPM;
}

void PrintEngineRPM()
{
  Serial.printf("Engine RPM = %d\n", g_EngineRPM);
}

// --------------------------------------------------------
// ******** Currently Engaged Gear ************************
// --------------------------------------------------------

static int32_t g_Gear = 0;    // 0 = Neutral, -1 = Reverse

int32_t CalcGear(const uint8_t* pData)
{
  const uint8_t neutral = 0;
  const uint8_t reverse = 0x10;

  uint8_t A = pData[4];
  g_Gear = A;

  // Return -1 for reverse
  if (g_Gear == reverse)
  {
    g_Gear = -1;
  }

  return g_Gear;
}

// Current gear can also be calcuated from a non-OBD2 CAN frame. This is much quicker (every ~50ms), since it's not required to send an
// OBD2 request and then wait for the information to be returned.
int32_t CalcGear_FromBroadcastedFrame(const uint8_t* pData)
{
  // Gear status is on byte 0 from bit 7 to 4 (0x0=neutral, 0x1 to 0x6=gear 1 to 6, 0x07=reverse gear, 0x8 to 0xA=gear 7 to 9, 0xF=neutral in Park)
  g_Gear = pData[0] >> 4;

  if (g_Gear == 0x07)
  {
    g_Gear = -1;  // Reverse
  }
  else if (g_Gear >= 0x08 && g_Gear <= 0x0A)
  {
    g_Gear--;
  }
  else if (g_Gear == 0x0F)  // Neutral while in Park
  {
    g_Gear = 0;
  }

  return g_Gear;
}

void PrintGear()
{
  char gearStr[32] = "Neutral";

  if (g_Gear == -1)
  {
    sprintf(gearStr, "Reverse");
  }
  else if (g_Gear > 0)
  {
    sprintf(gearStr, "%d", g_Gear);
  }

  Serial.printf("Current Engaged Gear = %s\n", gearStr);
}

// --------------------------------------------------------
// ******** Engine Temperature ****************************
// --------------------------------------------------------

static int32_t g_EngineTemp = 0;    // Celcius

int32_t CalcEngineTemp(const uint8_t* pData)
{
  uint8_t A = pData[4];
  g_EngineTemp = A - 40;
  return g_EngineTemp;
}

void PrintEngineTemp()
{
  int32_t farh = (float(g_EngineTemp) * 9.0f / 5.0f) + 32.0f + 0.5f;
  Serial.printf("Engine Temperature = %d*C (%d*F)\n", g_EngineTemp, farh);
}

// --------------------------------------------------------
// ******** Engine Oil Temperature ************************
// --------------------------------------------------------

static int32_t g_EngineOilTemp = 0;   // Celcius

int32_t CalcEngineOilTemp(const uint8_t* pData)
{
  uint8_t B = pData[5];
  g_EngineOilTemp = B;
  return g_EngineOilTemp;
}

void PrintEngineOilTemp()
{
  int32_t farh = (float(g_EngineOilTemp) * 9.0f / 5.0f) + 32.0f + 0.5f;
  Serial.printf("Engine Oil Temperature = %d*C (%d*F)\n", g_EngineOilTemp, farh);
}

// --------------------------------------------------------
// ******** Exhaust Gas Temperature ***********************
// --------------------------------------------------------

static int32_t g_ExhaustGasTemp = 0;   // Celcius

int32_t CalcExhaustGasTemp(const uint8_t* pData)
{
  uint8_t A = pData[4];
  g_ExhaustGasTemp = (A * 5) - 50;
  return g_ExhaustGasTemp;
}

void PrintExhaustGasTemp()
{
  int32_t farh = (float(g_ExhaustGasTemp) * 9.0f / 5.0f) + 32.0f + 0.5f;
  Serial.printf("Exhaust Gas Temperature = %d*C (%d*F)\n", g_ExhaustGasTemp, farh);
}

// --------------------------------------------------------
// ******** Battery IBS ***********************************
// --------------------------------------------------------

static int32_t g_BatteryIBS = 0;    // %

int32_t CalcBatteryIBS(const uint8_t* pData)
{
  uint8_t A = pData[4];
  g_BatteryIBS = A;
  return g_BatteryIBS;
}

void PrintBatteryIBS()
{
  Serial.printf("Battery IBS = %d %%\n", g_BatteryIBS);
}

// --------------------------------------------------------
// ******** Battery ***************************************
// --------------------------------------------------------

static float g_Battery = 0.0f;    // Volts

int32_t CalcBattery(const uint8_t* pData)
{
  uint8_t B = pData[5];
  g_Battery = B / 10.0f;
  return g_Battery;
}

void PrintBattery()
{
  Serial.printf("Battery = %.1f Volts\n", g_Battery);
}

// --------------------------------------------------------
// ******** Atmospheric Pressure **************************
// --------------------------------------------------------

static int32_t g_AtmosphericPressure = 0;   // mbar

int32_t CalcAtmosphericPressure(const uint8_t* pData)
{
  uint8_t A = pData[4];
  uint8_t B = pData[5];
  g_AtmosphericPressure = (A * 256 + B);
  return g_AtmosphericPressure;
}

void PrintAtmosphericPressure()
{
  Serial.printf("Atmospheric Pressure = %d mbar\n", g_AtmosphericPressure);
}

// --------------------------------------------------------
// ******** Boost Pressure ********************************
// --------------------------------------------------------

static int32_t g_BoostPressure = 0;  // mbar

int32_t CalcBoostPressure(const uint8_t* pData)
{
  uint8_t A = pData[4];
  uint8_t B = pData[5];
  g_BoostPressure = (A * 256 + B);
  return g_BoostPressure;
}

// Boost pressure can also be calcuated from a non-OBD2 CAN frame. This is much quicker (every ~50ms), since it's not required to send an
// OBD2 request and then wait for the information to be returned. Unfortunately the precision isn't great at very high boost levels
int32_t CalcBoostPressure_FromBroadcastedFrame(const uint8_t* pData)
{
  // Boost pressure on byte 3 bit from 6 to 0 and byte 4 bit 7
  uint8_t A = pData[3] & 0b00111111;
  uint8_t B = pData[4] >> 7;
  g_BoostPressure = (A * 32) + (B * 16) + 1000;
  return g_BoostPressure;
}

void PrintBoostPressure()
{
  Serial.printf("Boost Pressure = %d mbar\n", g_BoostPressure);
}

// --------------------------------------------------------
// ******** External Temperature ***************************
// --------------------------------------------------------

static int32_t g_ExternalTemp = 0;    // Celcius

int32_t CalcExternalTemp(const uint8_t* pData)
{
  uint8_t A = pData[4];
  g_ExternalTemp = (A / 2) - 40;
  return g_ExternalTemp;
}

void PrintExternalTemp()
{
  int32_t farh = (float(g_ExternalTemp) * 9.0f / 5.0f) + 32.0f + 0.5f;
  Serial.printf("External Temperature = %d*C (%d*F)\n", g_ExternalTemp, farh);
}

// --------------------------------------------------------
// ******** Ignition Key Position *************************
// --------------------------------------------------------

enum IgnitionKeyPosition
{
  Off   = 0x00,   // Car is switched off
  On    = 0x04,   // Electronics are powered, but engine not turning
  Start = 0x14    // Engine turning
};

static int32_t g_IgnitionKeyPosition = IgnitionKeyPosition::Off;

int32_t CalcIgnitionKeyPosition(const uint8_t* pData)
{
  uint8_t A = pData[4];
  g_IgnitionKeyPosition = A;
  return g_IgnitionKeyPosition;
}

void PrintIgnitionKeyPosition()
{
  Serial.printf("Ignition Key Position = %s\n", (g_IgnitionKeyPosition == IgnitionKeyPosition::On) ? "On" : (g_IgnitionKeyPosition == IgnitionKeyPosition::Start) ? "Start" : "Off");
}

// --------------------------------------------------------
// ******** DNA Drive Mode ********************************
// --------------------------------------------------------

// The DNA selector for the selected drive mode
enum DNASelector
{
  D  = 0x09,  // Dynamic
  N  = 0x01,  // Natural
  A  = 0x11,  // Advanced efficiency
  R  = 0x31   // Race
};

static int32_t g_DriveMode = DNASelector::N;

// Drive mode (DNA) can also be retrieved from a non-OBD2 CAN frame. This is much quicker (~50ms), since it's not required to send an
// OBD2 request and then wait for the information to be returned.
int32_t CalcIDriveMode_FromBroadcastedFrame(const uint8_t* pData)
{
  g_DriveMode = pData[1];
  return g_DriveMode;
}

void PrintDriveMode()
{
  Serial.printf("DNA Selector: %#04x %s\n", g_DriveMode, (g_DriveMode == DNASelector::D) ? "D" :
                                                         (g_DriveMode == DNASelector::N) ? "N" :
                                                         (g_DriveMode == DNASelector::A) ? "A" :
                                                         (g_DriveMode == DNASelector::R) ? "R" : "ERROR: Unknown Drive Mode");
}

#endif
