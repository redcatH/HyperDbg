/**
 * @file kd.cpp
 * @author Sina Karvandi (sina@rayanfam.com)
 * @brief routines to kernel debugging
 * @details
 * @version 0.1
 * @date 2020-12-22
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#include "pch.h"

//
// Global Variables
//
extern HANDLE g_SerialListeningThreadHandle;
extern HANDLE g_SerialRemoteComPortHandle;
extern HANDLE g_DebuggeeStopCommandEventHandle;
extern HANDLE
    g_SyncronizationObjectsHandleTable[DEBUGGER_MAXIMUM_SYNCRONIZATION_OBJECTS];
extern BOOLEAN g_IsConnectedToHyperDbgLocally;
extern OVERLAPPED g_OverlappedIoStructureForReadDebugger;
extern OVERLAPPED g_OverlappedIoStructureForWriteDebugger;
extern BOOLEAN g_IsSerialConnectedToRemoteDebuggee;
extern BOOLEAN g_IsSerialConnectedToRemoteDebugger;
extern BOOLEAN g_IsDebuggerConntectedToNamedPipe;
extern BOOLEAN g_IsDebuggeeRunning;
extern BOOLEAN g_IsDebuggerModulesLoaded;
extern BYTE g_EndOfBufferCheck[4];

/**
 * @brief compares the buffer with a string
 *
 * @param CurrentLoopIndex Number of previously read bytes
 * @param Buffer
 * @return BOOLEAN
 */
BOOLEAN KdCheckForTheEndOfTheBuffer(PUINT32 CurrentLoopIndex, BYTE *Buffer) {

  UINT32 ActualBufferLength;

  ActualBufferLength = *CurrentLoopIndex;

  //
  // End of buffer is 4 character long
  //
  if (*CurrentLoopIndex <= 3) {
    return FALSE;
  }

  if (Buffer[ActualBufferLength] == SERIAL_END_OF_BUFFER_CHAR_4 &&
      Buffer[ActualBufferLength - 1] == SERIAL_END_OF_BUFFER_CHAR_3 &&
      Buffer[ActualBufferLength - 2] == SERIAL_END_OF_BUFFER_CHAR_2 &&
      Buffer[ActualBufferLength - 3] == SERIAL_END_OF_BUFFER_CHAR_1) {

    //
    // Clear the end character
    //
    Buffer[ActualBufferLength - 3] = NULL;
    Buffer[ActualBufferLength - 2] = NULL;
    Buffer[ActualBufferLength - 1] = NULL;
    Buffer[ActualBufferLength] = NULL;

    //
    // Set the new length
    //
    *CurrentLoopIndex = ActualBufferLength - 3;

    return TRUE;
  }
  return FALSE;
}

/**
 * @brief compares the buffer with a string
 *
 * @param Buffer
 * @param CompareBuffer
 * @return BOOLEAN
 */
BOOLEAN KdCompareBufferWithString(CHAR *Buffer, const CHAR *CompareBuffer) {

  int Result;

  Result = strcmp(Buffer, CompareBuffer);

  if (Result == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * @brief Interpret the packets from debuggee in the case of paused
 *
 * @return VOID
 */
VOID KdInterpretPausedDebuggee() {

  //
  // Wait for handshake to complete or in other words
  // get the receive packet
  //
  WaitForSingleObject(
      g_SyncronizationObjectsHandleTable
          [DEBUGGER_SYNCRONIZATION_OBJECT_PAUSED_DEBUGGEE_DETAILS],
      INFINITE);
}

/**
 * @brief Sends a continue or 'g' command packet to the debuggee
 *
 * @return BOOLEAN
 */
BOOLEAN KdSendContinuePacketToDebuggee() {

  //
  // Send 'g' as continue packet
  //
  if (!KdCommandPacketToDebuggee(
          DEBUGGER_REMOTE_PACKET_TYPE_DEBUGGER_TO_DEBUGGEE_EXECUTE_ON_VMX_ROOT,
          DEBUGGER_REMOTE_PACKET_REQUESTED_ACTION_ON_VMX_ROOT_MODE_CONTINUE)) {
    return FALSE;
  }

  return TRUE;
}

/**
 * @brief Sends p (step out) and t (step in) packet to the debuggee
 *
 * @return BOOLEAN
 */
BOOLEAN
KdSendStepPacketToDebuggee(DEBUGGER_REMOTE_STEPPING_REQUEST StepRequestType) {

  //
  // Send step packet to the serial
  //
  if (!KdCommandPacketToDebuggee(
          DEBUGGER_REMOTE_PACKET_TYPE_DEBUGGER_TO_DEBUGGEE_EXECUTE_ON_VMX_ROOT,
          DEBUGGER_REMOTE_PACKET_REQUESTED_ACTION_ON_VMX_ROOT_MODE_STEP)) {
    return FALSE;
  }

  //
  // Handle the paused state (show rip, instructions, etc.)
  //
  KdInterpretPausedDebuggee();

  return TRUE;
}

/**
 * @brief Sends a PAUSE packet to the debuggee
 *
 * @return BOOLEAN
 */
BOOLEAN KdSendPausePacketToDebuggee() {

  CHAR BufferToReceive[64] = {0};
  UINT32 LengthReceived = 0;

  //
  // Send pause packet to debuggee
  //
  if (!KdCommandPacketToDebuggee(
          DEBUGGER_REMOTE_PACKET_TYPE_DEBUGGER_TO_DEBUGGEE_EXECUTE_ON_USER_MODE,
          DEBUGGER_REMOTE_PACKET_REQUESTED_ACTION_ON_USER_MODE_PAUSE)) {
    return FALSE;
  }

  //
  // Handle the paused state (show rip, instructions, etc.)
  //
  KdInterpretPausedDebuggee();

  return TRUE;
}

/**
 * @brief Get Windows name, version and build to send to debuggger
 *
 * @param BufferToSave
 *
 * @return BOOLEAN
 */
BOOLEAN KdGetWindowVersion(CHAR *BufferToSave) {

  HKeyHolder currentVersion;
  DWORD valueType;
  CHAR bufferResult[MAXIMUM_CHARACTER_FOR_OS_NAME] = {0};
  BYTE bufferProductName[MAXIMUM_CHARACTER_FOR_OS_NAME] = {0};
  BYTE bufferCurrentBuild[MAXIMUM_CHARACTER_FOR_OS_NAME] = {0};
  BYTE bufferDisplayVersion[MAXIMUM_CHARACTER_FOR_OS_NAME] = {0};
  DWORD bufferSize = MAXIMUM_CHARACTER_FOR_OS_NAME;

  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", 0,
                    KEY_QUERY_VALUE, &currentVersion) != ERROR_SUCCESS) {
    // return FALSE;
  }

  if (RegQueryValueExA(currentVersion, "ProductName", nullptr, &valueType,
                       bufferProductName, &bufferSize) != ERROR_SUCCESS) {
    // return FALSE;
  }

  if (valueType != REG_SZ) {

    //  return FALSE;
  }

  bufferSize = MAXIMUM_CHARACTER_FOR_OS_NAME;

  if (RegQueryValueExA(currentVersion, "DisplayVersion", nullptr, &valueType,
                       bufferDisplayVersion, &bufferSize) != ERROR_SUCCESS) {
    // return FALSE;
  }

  if (valueType != REG_SZ) {

    //  return FALSE;
  }
  bufferSize = MAXIMUM_CHARACTER_FOR_OS_NAME;

  if (RegQueryValueExA(currentVersion, "CurrentBuild", nullptr, &valueType,
                       bufferCurrentBuild, &bufferSize) != ERROR_SUCCESS) {
    //  return FALSE;
  }

  if (valueType != REG_SZ) {
    // return FALSE;
  }

  sprintf_s(bufferResult, "%s %s %s (OS Build %s)", bufferProductName,
            IsWindowsServer() ? "- Server" : "- Client", bufferDisplayVersion,
            bufferCurrentBuild);

  memcpy(BufferToSave, bufferResult, MAXIMUM_CHARACTER_FOR_OS_NAME);

  return TRUE;
}

/**
 * @brief Receive packet from the debugger
 *
 * @param BufferToSave
 * @param LengthReceived
 *
 * @return BOOLEAN
 */
BOOLEAN KdReceivePacketFromDebuggee(CHAR *BufferToSave,
                                    UINT32 *LengthReceived) {

  BOOL Status;           /* Status */
  char ReadData = NULL;  /* temperory Character */
  DWORD NoBytesRead = 0; /* Bytes read by ReadFile() */
  UINT32 Loop = 0;

  //
  // Read data and store in a buffer
  //
  do {
    if (g_IsSerialConnectedToRemoteDebugger) {

      //
      // It's a debuggee
      //
      Status = ReadFile(g_SerialRemoteComPortHandle, &ReadData,
                        sizeof(ReadData), &NoBytesRead, NULL);

    } else {

      //
      // It's a debugger
      //

      //
      // Try to read one byte in overlapped I/O (in debugger)
      //
      if (!ReadFile(g_SerialRemoteComPortHandle, &ReadData, sizeof(ReadData),
                    NULL, &g_OverlappedIoStructureForReadDebugger)) {

        DWORD e = GetLastError();

        if (e != ERROR_IO_PENDING) {
          return FALSE;
        }
      }

      //
      // Wait till one packet becomes available
      //
      WaitForSingleObject(g_OverlappedIoStructureForReadDebugger.hEvent,
                          INFINITE);

      //
      // Get the result
      //
      GetOverlappedResult(g_SerialRemoteComPortHandle,
                          &g_OverlappedIoStructureForReadDebugger, &NoBytesRead,
                          FALSE);

      //
      // Reset event for next try
      //
      ResetEvent(g_OverlappedIoStructureForReadDebugger.hEvent);
    }
    BufferToSave[Loop] = ReadData;

    if (KdCheckForTheEndOfTheBuffer(&Loop, (BYTE *)BufferToSave)) {
      break;
    }

    ++Loop;

  } while (NoBytesRead > 0);

  //
  // Set the length
  //
  *LengthReceived = Loop;

  return TRUE;
}

/**
 * @brief Sends a special packet to the debuggee
 *
 * @param Buffer
 * @param Length
 * @return BOOLEAN
 */
BOOLEAN KdSendPacketToDebuggee(const CHAR *Buffer, UINT32 Length,
                               BOOLEAN SendEndOfBuffer) {

  BOOL Status;
  DWORD BytesWritten = 0;
  DWORD LastErrorCode = 0;

  //
  // Check if the remote code's handle found or not
  //
  if (g_SerialRemoteComPortHandle == NULL) {
    ShowMessages("err, handle to remote debuggee's com port is not found.\n");
    return FALSE;
  }

  if (g_IsSerialConnectedToRemoteDebugger) {

    //
    // It's for a debuggee
    //
    Status = WriteFile(g_SerialRemoteComPortHandle, // Handle to the Serialport
                       Buffer,        // Data to be written to the port
                       Length,        // No of bytes to write into the port
                       &BytesWritten, // No of bytes written to the port
                       NULL);

    if (Status == FALSE) {
      ShowMessages("err, fail to write to com port or named pipe (error %x).\n",
                   GetLastError());
      return FALSE;
    }

    //
    // Check if message delivered successfully
    //
    if (BytesWritten != Length) {
      return FALSE;
    }
  } else {
    //
    // It's a debugger
    //

    if (WriteFile(g_SerialRemoteComPortHandle, Buffer, Length, NULL,
                  &g_OverlappedIoStructureForWriteDebugger)) {
      //
      // Write Completed
      //
      goto Out;
    }

    LastErrorCode = GetLastError();
    if (LastErrorCode != ERROR_IO_PENDING) {
      //
      // Error
      //
      ShowMessages("err, on sending serial packets (0x%x)", LastErrorCode);
      return FALSE;
    }

    //
    // Wait until write completed
    //
    if (WaitForSingleObject(g_OverlappedIoStructureForWriteDebugger.hEvent,
                            INFINITE) != WAIT_OBJECT_0) {
      ShowMessages("err, on sending serial packets (signal error)");
      return FALSE;
    }

    //
    // Reset event
    //
    ResetEvent(g_OverlappedIoStructureForWriteDebugger.hEvent);
  }

Out:
  if (SendEndOfBuffer) {

    //
    // Send End of Buffer Packet
    //
    if (!KdSendPacketToDebuggee((const CHAR *)g_EndOfBufferCheck,
                                sizeof(g_EndOfBufferCheck), FALSE)) {
      return FALSE;
    }
  }

  //
  // All the bytes are sent
  //
  return TRUE;
}

/**
 * @brief Sends a HyperDbg packet to the debuggee
 *
 * @param RequestedAction
 * @return BOOLEAN
 */
BOOLEAN KdCommandPacketToDebuggee(
    DEBUGGER_REMOTE_PACKET_TYPE PacketType,
    DEBUGGER_REMOTE_PACKET_REQUESTED_ACTION RequestedAction) {

  DEBUGGER_REMOTE_PACKET Packet = {0};

  //
  // Make the packet's structure
  //
  Packet.Indicator = INDICATOR_OF_HYPERDBG_PACKER;
  Packet.TypeOfThePacket = PacketType;

  //
  // Set the requested action
  //
  Packet.RequestedActionOfThePacket = RequestedAction;

  if (!KdSendPacketToDebuggee((const CHAR *)&Packet,
                              sizeof(DEBUGGER_REMOTE_PACKET), TRUE)) {
    return FALSE;
  }
  return TRUE;
}

/**
 * @brief check if the debuggee needs to be paused
 *
 * @return VOID
 */
VOID KdBreakControlCheckAndPauseDebugger() {

  //
  // Check if debuggee is running, otherwise the user
  // pressed ctrl+c multiple times
  //
  if (g_IsDebuggeeRunning) {

    //
    // Send the pause request to the remote computer
    //
    if (!KdSendPausePacketToDebuggee()) {
      ShowMessages("err, unable to pause the debuggee\n");
    }

    //
    // Signal the event
    //
    SetEvent(g_SyncronizationObjectsHandleTable
                 [DEBUGGER_SYNCRONIZATION_OBJECT_IS_DEBUGGER_RUNNING]);
  }
}

/**
 * @brief check if the debuggee needs to be continued
 *
 * @return VOID
 */
VOID KdBreakControlCheckAndContinueDebugger() {

  //
  // Check if debuggee is paused or not
  //
  if (!g_IsDebuggeeRunning) {

    //
    // Send the continue request to the remote computer
    //
    if (KdSendContinuePacketToDebuggee()) {

      //
      // Set the debuggee to show that it's  running
      //
      g_IsDebuggeeRunning = TRUE;

      //
      // Halt the UI
      //
      KdTheRemoteSystemIsRunning();

    } else {
      ShowMessages("err, unable to continue the debuggee\n");
    }
  }
}

/**
 * @brief wait for a event to be triggered and if the debuggee
 * is running it just halts the system
 *
 * @return VOID
 */
VOID KdTheRemoteSystemIsRunning() {

  //
  // Indicate that the debuggee is running
  //
  ShowMessages("Debuggee is running...\n");

  //
  // Wait until the users press CTRL+C
  //
  WaitForSingleObject(g_SyncronizationObjectsHandleTable
                          [DEBUGGER_SYNCRONIZATION_OBJECT_IS_DEBUGGER_RUNNING],
                      INFINITE);
}

/**
 * @brief Prepare serial to connect to the remote server
 * @details wait to connect to debuggee (this is debugger)
 *
 * @param SerialHandle
 * @return BOOLEAN
 */
BOOLEAN KdPrepareSerialConnectionToRemoteSystem(HANDLE SerialHandle,
                                                BOOLEAN IsNamedPipe) {

StartAgain:

  BOOL Status;         /* Status */
  DWORD EventMask = 0; /* Event mask to trigger */

  //
  // Show an indication to connect the debugger
  //
  ShowMessages("Waiting for debuggee to connect ...\n");

  if (!IsNamedPipe) {

    //
    // Setting Receive Mask
    //
    Status = SetCommMask(SerialHandle, EV_RXCHAR);
    if (Status == FALSE) {
      ShowMessages("err, in setting CommMask\n");
      return FALSE;
    }

    //
    // Setting WaitComm() Event
    //
    Status = WaitCommEvent(SerialHandle, &EventMask,
                           NULL); /* Wait for the character to be received */

    if (Status == FALSE) {
      ShowMessages("err,in setting WaitCommEvent()\n");
      return FALSE;
    }
  }

  //
  // Initialize the handle table
  //
  for (size_t i = 0; i < DEBUGGER_MAXIMUM_SYNCRONIZATION_OBJECTS; i++) {
    g_SyncronizationObjectsHandleTable[i] =
        CreateEvent(NULL, FALSE, FALSE, NULL);
  }

  //
  // Create the listening thread in debugger
  //

  g_SerialListeningThreadHandle =
      CreateThread(NULL, 0, ListeningSerialPauseDebuggerThread, NULL, 0, NULL);

  //
  // Wait for the 'Start' packet on the listener side
  //
  WaitForSingleObject(
      g_SyncronizationObjectsHandleTable
          [DEBUGGER_SYNCRONIZATION_OBJECT_STARTED_PACKET_RECEIVED],
      INFINITE);

  //
  // Connected to the debuggee
  //
  g_IsSerialConnectedToRemoteDebuggee = TRUE;

  //
  // And debuggee is running
  //
  g_IsDebuggeeRunning = TRUE;

  //
  // Save the handler
  //
  g_SerialRemoteComPortHandle = SerialHandle;

  //
  // Is serial handle for a named pipe
  //
  g_IsDebuggerConntectedToNamedPipe = IsNamedPipe;

  //
  // Register the CTRL+C and CTRL+BREAK Signals handler
  //
  if (!SetConsoleCtrlHandler(BreakController, TRUE)) {
    ShowMessages(
        "Error in registering CTRL+C and CTRL+BREAK Signals handler\n");
    return FALSE;
  }

  //
  // Wait for event on this thread
  //
  KdTheRemoteSystemIsRunning();

  return TRUE;
}

/**
 * @brief Prepare and initialize COM port
 *
 * @param PortName
 * @param Baudrate
 * @param Port
 * @param IsPreparing
 * @param IsNamedPipe
 * @return BOOLEAN
 */
BOOLEAN KdPrepareAndConnectDebugPort(const char *PortName, DWORD Baudrate,
                                     UINT32 Port, BOOLEAN IsPreparing,
                                     BOOLEAN IsNamedPipe) {

  HANDLE Comm;                 /* Handle to the Serial port */
  BOOL Status;                 /* Status */
  DCB SerialParams = {0};      /* Initializing DCB structure */
  COMMTIMEOUTS Timeouts = {0}; /* Initializing timeouts structure */
  char PortNo[20] = {0};       /* contain friendly name */
  BOOLEAN StatusIoctl;
  ULONG ReturnedLength;
  PDEBUGGER_PREPARE_DEBUGGEE DebuggeeRequest;

  //
  // Check if the debugger or debuggee is already active
  //
  if (IsConnectedToAnyInstanceOfDebuggerOrDebuggee()) {

    return FALSE;
  }

  if (IsPreparing && IsNamedPipe) {
    ShowMessages("err, cannot used named pipe for debuggee");
    return FALSE;
  }

  if (!IsNamedPipe) {

    //
    // It's a serial
    //

    //
    // Append name to make a Windows understandable format
    //
    sprintf_s(PortNo, 20, "\\\\.\\%s", PortName);

    //
    // Open the serial com port (if it's the debugger (not debuggee)) then
    // open with Overlapped I/O
    //

    if (IsPreparing) {
      //
      // It's debuggee (Non-overlapped I/O)
      //
      Comm = CreateFile(PortNo,                       // Friendly name
                        GENERIC_READ | GENERIC_WRITE, // Read/Write Access
                        0,             // No Sharing, ports cant be shared
                        NULL,          // No Security
                        OPEN_EXISTING, // Open existing port only
                        0,             // Non Overlapped I/O
                        NULL);         // Null for Comm Devices
    } else {

      //
      // It's debugger (Overlapped I/O)
      //
      Comm = CreateFile(PortNo,                       // Friendly name
                        GENERIC_READ | GENERIC_WRITE, // Read/Write Access
                        0,             // No Sharing, ports cant be shared
                        NULL,          // No Security
                        OPEN_EXISTING, // Open existing port only
                        FILE_FLAG_OVERLAPPED, // Overlapped I/O
                        NULL);                // Null for Comm Devices

      //
      // Create event for overlapped I/O (Read)
      //
      g_OverlappedIoStructureForReadDebugger.hEvent =
          CreateEvent(NULL, TRUE, FALSE, NULL);

      //
      // Create event for overlapped I/O (Write)
      //
      g_OverlappedIoStructureForWriteDebugger.hEvent =
          CreateEvent(NULL, TRUE, FALSE, NULL);
    }

    if (Comm == INVALID_HANDLE_VALUE) {
      ShowMessages("err, port can't be opened.\n");
      return FALSE;
    }

    //
    // Purge the serial port
    //
    PurgeComm(Comm,
              PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);

    //
    // Setting the Parameters for the SerialPort
    //
    SerialParams.DCBlength = sizeof(SerialParams);

    //
    // retreives the current settings
    //
    Status = GetCommState(Comm, &SerialParams);

    if (Status == FALSE) {
      ShowMessages("err, to Get the COM state\n");
      return FALSE;
    }

    SerialParams.BaudRate =
        Baudrate;              // BaudRate = 9600 (Based on user selection)
    SerialParams.ByteSize = 8; // ByteSize = 8
    SerialParams.StopBits = ONESTOPBIT; // StopBits = 1
    SerialParams.Parity = NOPARITY;     // Parity = None
    Status = SetCommState(Comm, &SerialParams);

    if (Status == FALSE) {
      ShowMessages("err, to Setting DCB Structure\n");
      return FALSE;
    }

    //
    // Setting Timeouts (not use it anymore as we use special signature for
    // ending buffer)
    // (no need anymore as we use end buffer detection mechanism)
    //

    /*
    Timeouts.ReadIntervalTimeout = 50;
    Timeouts.ReadTotalTimeoutConstant = 50;
    Timeouts.ReadTotalTimeoutMultiplier = 10;
    Timeouts.WriteTotalTimeoutConstant = 50;
    Timeouts.WriteTotalTimeoutMultiplier = 10;

    if (SetCommTimeouts(Comm, &Timeouts) == FALSE) {
      ShowMessages("err, to Setting Time outs %d.\n", GetLastError());
      return FALSE;
    }
    */

  } else {
    //
    // It's a namedpipe
    //
    Comm = NamedPipeClientCreatePipeOverlappedIo(PortName);

    if (!Comm) {

      //
      // Unable to create handle
      //
      ShowMessages("Is virtual machine running ?!\n");
      return FALSE;
    }
  }

  if (IsPreparing) {

    //
    // It's a debuggee request
    //

    //
    // First, connect to local machine and we load the VMM module as it's a
    // module that is responsible for working on debugger
    //
    g_IsConnectedToHyperDbgLocally = TRUE;

    //
    // Load the VMM
    //
    if (!CommandLoadVmmModule()) {
      ShowMessages("Failed to install or load the driver\n");
      return FALSE;
    }

    //
    // Check if driver is loaded or not, in the case
    // of connecting to a remote machine as debuggee
    //
    if (!g_DeviceHandle) {
      ShowMessages(
          "Handle not found, probably the driver is not loaded. Did you "
          "use 'load' command?\n");
      return FALSE;
    }

    //
    // Allocate buffer
    //
    DebuggeeRequest =
        (PDEBUGGER_PREPARE_DEBUGGEE)malloc(SIZEOF_DEBUGGER_PREPARE_DEBUGGEE);

    if (DebuggeeRequest == NULL) {
      return FALSE;
    }

    RtlZeroMemory(DebuggeeRequest, SIZEOF_DEBUGGER_PREPARE_DEBUGGEE);

    //
    // Prepare the details structure
    //
    DebuggeeRequest->PortAddress = Port;
    DebuggeeRequest->Baudrate = Baudrate;

    //
    // Set the debuggee name, version, and build number
    //
    if (!KdGetWindowVersion(DebuggeeRequest->OsName)) {

      //
      // It's not an error if it returned null
      //
      /*return FALSE;*/
    }

    //
    // Send the request to the kernel
    //
    StatusIoctl =
        DeviceIoControl(g_DeviceHandle,         // Handle to device
                        IOCTL_PREPARE_DEBUGGEE, // IO Control code
                        DebuggeeRequest,        // Input Buffer to driver.
                        SIZEOF_DEBUGGER_PREPARE_DEBUGGEE, // Input buffer
                                                          // length
                        DebuggeeRequest, // Output Buffer from driver.
                        SIZEOF_DEBUGGER_PREPARE_DEBUGGEE, // Length of output
                                                          // buffer in bytes.
                        &ReturnedLength, // Bytes placed in buffer.
                        NULL             // synchronous call
        );

    if (!StatusIoctl) {
      ShowMessages("ioctl failed with code 0x%x\n", GetLastError());

      //
      // Free the buffer
      //
      free(DebuggeeRequest);

      return FALSE;
    }

    if (DebuggeeRequest->Result == DEBUGEER_OPERATION_WAS_SUCCESSFULL) {
      ShowMessages("The operation was successful\n");

    } else {
      ShowErrorMessage(DebuggeeRequest->Result);
      //
      // Free the buffer
      //
      free(DebuggeeRequest);

      return FALSE;
    }

    //
    // Indicate that we connected to the debugger
    //
    g_IsSerialConnectedToRemoteDebugger = TRUE;

    //
    // Set handle to serial device
    //
    g_SerialRemoteComPortHandle = Comm;

    //
    // Create a thread to listen for pauses from the remote debugger
    //
    g_SerialListeningThreadHandle = CreateThread(
        NULL, 0, ListeningSerialPauseDebuggeeThread, NULL, 0, NULL);

    //
    // Free the buffer
    //
    free(DebuggeeRequest);

    //
    // Wait here so the user can't give new commands
    // Create an event (manually. no signal)
    //
    g_DebuggeeStopCommandEventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);

    //
    // Now we should wait on this state until the user closes the connection to
    // debuggee from debugger
    //
    WaitForSingleObject(g_DebuggeeStopCommandEventHandle, INFINITE);

    //
    // Close the event's handle
    //
    CloseHandle(g_DebuggeeStopCommandEventHandle);
    g_DebuggeeStopCommandEventHandle = NULL;

    //
    // Finish it here
    //
    return TRUE;

  } else {

    //
    // Save the handler
    //
    g_SerialRemoteComPortHandle = Comm;

    //
    // If we are here, then it's a debugger (not debuggee)
    // let's prepare the debuggee
    //
    KdPrepareSerialConnectionToRemoteSystem(Comm, IsNamedPipe);
  }

  //
  // everything was done
  //
  return TRUE;
}

/**
 * @brief Send close packet to the debuggee and debugger
 * @details This function close the connection in both debuggee and debugger
 * both of the debuggee and debugger can use this function
 *
 * @return VOID
 */
VOID KdCloseConnection() {

  //
  // Unload the VMM driver if it's debuggee
  //
  if (g_IsSerialConnectedToRemoteDebugger) {

    if (g_IsConnectedToHyperDbgLocally && g_IsDebuggerModulesLoaded) {

      HyperdbgUnload();

      //
      // Installing Driver
      //
      if (HyperdbgUninstallDriver()) {
        ShowMessages("Failed to uninstall driver\n");
      }
    }
  }

  //
  // Close handle and uninitialize everything
  //
  KdUninitializeConnection();
}

/**
 * @brief Uninitialize everything in both debuggee and debugger
 * @details This function close the connection in both debuggee and debugger
 * both of the debuggee and debugger can use this function
 *
 * @return VOID
 */
VOID KdUninitializeConnection() {

  //
  // Close the handles of the listening threads
  //
  if (g_SerialListeningThreadHandle != NULL) {
    CloseHandle(g_SerialListeningThreadHandle);
    g_SerialListeningThreadHandle = NULL;
  }

  if (g_OverlappedIoStructureForReadDebugger.hEvent != NULL) {
    CloseHandle(g_OverlappedIoStructureForReadDebugger.hEvent);
  }

  if (g_OverlappedIoStructureForWriteDebugger.hEvent != NULL) {
    CloseHandle(g_OverlappedIoStructureForWriteDebugger.hEvent);
  }

  if (g_IsSerialConnectedToRemoteDebuggee ||
      g_IsSerialConnectedToRemoteDebugger) {

    //
    // Close synchronization objects
    //
    for (size_t i = 0; i < DEBUGGER_MAXIMUM_SYNCRONIZATION_OBJECTS; i++) {

      if (g_SyncronizationObjectsHandleTable[i] != NULL) {
        CloseHandle(g_SyncronizationObjectsHandleTable[i]);
        g_SyncronizationObjectsHandleTable[i] = NULL;
      }
    }

    //
    // If connected to debugger
    //
    g_IsSerialConnectedToRemoteDebugger = FALSE;

    //
    // No longer connected to the debuggee
    //
    g_IsSerialConnectedToRemoteDebuggee = FALSE;

    //
    // And debuggee is not running
    //
    g_IsDebuggeeRunning = FALSE;

    //
    // Clear the handler
    //
    if (g_SerialRemoteComPortHandle != NULL) {
      CloseHandle(g_SerialRemoteComPortHandle);
      g_SerialRemoteComPortHandle = NULL;
    }

    //
    // Is serial handle for a named pipe
    //
    g_IsDebuggerConntectedToNamedPipe = FALSE;

    if (g_DebuggeeStopCommandEventHandle != NULL) {

      //
      // Signal the debuggee to get new commands
      //
      SetEvent(g_DebuggeeStopCommandEventHandle);
    }

  } else {
    ShowMessages("err, you're not connected to any debuggee.\n");
  }
}
