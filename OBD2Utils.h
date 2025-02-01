// A collection of simple OBD2 helper defines and functions

#ifndef _OBD2_UTILS
#define _OBD2_UTILS

// CAN Modes for OBD2 Services
enum OBD2Service
{
  CurrentData           = 0x01,
  TroubleCodes          = 0x03,
  VehicleInfo           = 0x09,
  ManufacturerSpecific  = 0x22
};

// CAN IDs of car modules. These are extended OBD2 CAN IDs and for car modules they are all in the format 0x18DA__F1, and when received, 0x18DAF__
enum CarModule
{
  All = 0x18DB33F1,   // Used to send a message to all car modules
  ECM = 0x18DA10F1,   // Engine Control Module
  TCM = 0x18DA18F1,   // Transmision Control Module
  BCM = 0x18DA40F1    // Body Control Module
};

// Struct to easily define PIDs
struct PID
{
  char        Name[64];
  CarModule   Module;
  OBD2Service Service;
  uint16_t    PID;
  int32_t (*CalculateValue)(const uint8_t* pData);
  void (*PrintInformation)(void);
};

// Most of the PIDs for this car are two bytes and sometimes we need to work with one byte at a time
#define FIRST_BYTE(TwoByteNumber)   (TwoByteNumber >> 8)
#define SECOND_BYTE(TwoByteNumber)  (TwoByteNumber & 0x00FF)

// Send a request for OBD2 data
void SendOBD2Request(uint32_t carModule, uint32_t service, uint16_t pid)
{
  const uint8_t unused = 0xAA;

  CanFrame canFrame = { 0 };

  canFrame.identifier = carModule;
  canFrame.extd = (carModule > 0xFFF);      // Standard CAN IDs are in the range 0x7E8-0x7EF
  canFrame.data_length_code = 8;            // OBD2 always has 8 bytes in a CAN frame
  canFrame.data[0] = (pid > 0xFF) ? 3 : 2;  // If pid is 1 byte, then payload is 2 (1 byte for DLC and 1 byte for pid), otherwise payload is 3 (1 byte for DLC and 2 bytes for pid)
  canFrame.data[1] = service;
  canFrame.data[2] = (pid > 0xFF) ? FIRST_BYTE(pid) : pid;      // If the pid is 2 bytes, use the most signigicant byte as the 1st byte in the data
  canFrame.data[3] = (pid > 0xFF) ? SECOND_BYTE(pid) : unused;  // If the pid is 2 bytes, use the least signigicant byte as the 2nd byte in the data
  canFrame.data[4] = unused;
  canFrame.data[5] = unused;
  canFrame.data[6] = unused;
  canFrame.data[7] = unused;

  ESP32Can.writeFrame(canFrame);

  // Print frame data when debugging
  //PrintOBD2Frame(canFrame, false);

  delay(10);  // Add a short delay between sending frames
}

// Send a request for OBD2 data
void SendOBD2Request(CarModule carModule, OBD2Service service, uint16_t pid)
{
  SendOBD2Request(uint32_t(carModule), uint32_t(service), pid);
}

// Send a request for OBD2 data
void SendOBD2Request(PID* pid)
{
  SendOBD2Request(pid->Module, pid->Service, pid->PID);
}

// Find the PID in the data of an OBD2 frame. OBD2 uses a 2-byte PID for Extended CAN frames, but 1 byte for Standard CAN frames
uint16_t GetPID(const CanFrame& frame)
{
  uint8_t pidLengthInBytes = frame.extd ? 2 : 1;
  return (pidLengthInBytes == 1) ? frame.data[2] : ((frame.data[2] << 8) | frame.data[3]);
}

// Find the Service in the data of an OBD2 frame
uint16_t GetService(const CanFrame& frame, const bool receivedFrame)
{
  uint8_t service = frame.data[1];

  // CAN will always add 0x40 to the Service in received frames, e.g. if requesting service 0x22, 0x62 will show up in the received frame, not 0x22
  return receivedFrame ? (service - 0x40) : service;
}

// Determine if a CAN ID is from a valid OBD2 car module
bool IsValidCarModule(uint32_t canID)
{
  bool isValidStandard = (canID >= 0x700) && (canID <= 0x7FF);            // Valid Standard CAN IDs for OBD2 are in the range [0x7E8-0x7EF]
  bool isValidExtended = (canID >= 0x18000000) && (canID <= 0x18FFFFFF);  // Valid Extended CAN IDs for OBD2 are in the range [0x18DAF100 .. 0x18DAF1FF]
  return (isValidStandard || isValidExtended);
}

// It's useful for debugging to print the raw data of an OBD2 frame
void PrintOBD2Frame(CanFrame& frame, const bool receivedFrame)
{
  auto pid = GetPID(frame);
  auto service = GetService(frame, receivedFrame);
  
  DebugPrintf(receivedFrame ? "Received:    " : "Sent    :    ");
  DebugPrintf("Raw Data = %#04x ", frame.identifier);
  
  for (int i = 0; i < 8; i++)   // CAN frames are always 8 bytes
  {
    DebugPrintf("%#04x ", frame.data[i]);
  }

  // Print extracted info
  DebugPrintf("   Car Module = %#04x    Service = %#04x    PID = %#04x    Data Length = %d", frame.identifier, service, pid, frame.data_length_code);

  DebugPrintln();      
}

#endif