/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Win32 Kernel Library
 * FILE:            lib/kernel32/file/npipe.c
 * PURPOSE:         Named Pipe Functions
 * PROGRAMMER:      Alex Ionescu (alex@relsoft.net)
 *                  Ariadne ( ariadne@xs4all.nl)
 */

/* INCLUDES *******************************************************************/

#include <k32.h>
#define NDEBUG
#include <debug.h>
DEBUG_CHANNEL(kernel32file);

//#define USING_PROPER_NPFS_WAIT_SEMANTICS

/* GLOBALS ********************************************************************/

LONG ProcessPipeId;

/* FUNCTIONS ******************************************************************/

/*
 * @implemented
 */
BOOL
WINAPI
CreatePipe(PHANDLE hReadPipe,
           PHANDLE hWritePipe,
           LPSECURITY_ATTRIBUTES lpPipeAttributes,
           DWORD nSize)
{
    WCHAR Buffer[64];
    UNICODE_STRING PipeName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK StatusBlock;
    LARGE_INTEGER DefaultTimeout;
    NTSTATUS Status;
    HANDLE ReadPipeHandle;
    HANDLE WritePipeHandle;
    LONG PipeId;
    ULONG Attributes;
    PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;

    /* Set the timeout to 120 seconds */
    DefaultTimeout.QuadPart = -1200000000;

    /* Use default buffer size if desired */
    if (!nSize) nSize = 0x1000;

    /* Increase the Pipe ID */
    PipeId = InterlockedIncrement(&ProcessPipeId);

    /* Create the pipe name */
    swprintf(Buffer,
             L"\\Device\\NamedPipe\\Win32Pipes.%08x.%08x",
             NtCurrentTeb()->ClientId.UniqueProcess,
             PipeId);
    RtlInitUnicodeString(&PipeName, Buffer);

    /* Always use case insensitive */
    Attributes = OBJ_CASE_INSENSITIVE;

    /* Check if we got attributes */
    if (lpPipeAttributes)
    {
        /* Use the attributes' SD instead */
        SecurityDescriptor = lpPipeAttributes->lpSecurityDescriptor;

        /* Set OBJ_INHERIT if requested */
        if (lpPipeAttributes->bInheritHandle) Attributes |= OBJ_INHERIT;
    }

    /* Initialize the attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               &PipeName,
                               Attributes,
                               NULL,
                               SecurityDescriptor);

    /* Create the named pipe */
    Status = NtCreateNamedPipeFile(&ReadPipeHandle,
                                   GENERIC_READ |FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
                                   &ObjectAttributes,
                                   &StatusBlock,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   FILE_CREATE,
                                   FILE_SYNCHRONOUS_IO_NONALERT,
                                   FILE_PIPE_BYTE_STREAM_TYPE,
                                   FILE_PIPE_BYTE_STREAM_MODE,
                                   FILE_PIPE_QUEUE_OPERATION,
                                   1,
                                   nSize,
                                   nSize,
                                   &DefaultTimeout);
    if (!NT_SUCCESS(Status))
    {
        /* Convert error and fail */
        WARN("Status: %lx\n", Status);
        BaseSetLastNTError(Status);
        return FALSE;
    }

    /* Now try opening it for write access */
    Status = NtOpenFile(&WritePipeHandle,
                        FILE_GENERIC_WRITE,
                        &ObjectAttributes,
                        &StatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status))
    {
        /* Convert error and fail */
        WARN("Status: %lx\n", Status);
        NtClose(ReadPipeHandle);
        BaseSetLastNTError(Status);
        return FALSE;
    }

    /* Return both handles */
    *hReadPipe = ReadPipeHandle;
    *hWritePipe = WritePipeHandle;
    return TRUE;
}

/*
 * @implemented
 */
HANDLE
WINAPI
CreateNamedPipeA(LPCSTR lpName,
                 DWORD dwOpenMode,
                 DWORD dwPipeMode,
                 DWORD nMaxInstances,
                 DWORD nOutBufferSize,
                 DWORD nInBufferSize,
                 DWORD nDefaultTimeOut,
                 LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    /* Call the W(ide) function */
    ConvertWin32AnsiChangeApiToUnicodeApi(CreateNamedPipe,
                                          lpName,
                                          dwOpenMode,
                                          dwPipeMode,
                                          nMaxInstances,
                                          nOutBufferSize,
                                          nInBufferSize,
                                          nDefaultTimeOut,
                                          lpSecurityAttributes);
}


/*
 * @implemented
 */
HANDLE
WINAPI
CreateNamedPipeW(LPCWSTR lpName,
                 DWORD dwOpenMode,
                 DWORD dwPipeMode,
                 DWORD nMaxInstances,
                 DWORD nOutBufferSize,
                 DWORD nInBufferSize,
                 DWORD nDefaultTimeOut,
                 LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    UNICODE_STRING NamedPipeName;
    BOOL Result;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE PipeHandle;
    ACCESS_MASK DesiredAccess;
    ULONG CreateOptions = 0;
    ULONG WriteModeMessage;
    ULONG ReadModeMessage;
    ULONG NonBlocking;
    IO_STATUS_BLOCK Iosb;
    ULONG ShareAccess = 0, Attributes;
    LARGE_INTEGER DefaultTimeOut;
    PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;

    /* Check for valid instances */
    if (nMaxInstances == 0 || nMaxInstances > PIPE_UNLIMITED_INSTANCES)
    {
        /* Fail */
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    /* Convert to NT syntax */
    if (nMaxInstances == PIPE_UNLIMITED_INSTANCES)
        nMaxInstances = -1;

    /* Convert the name */
    Result = RtlDosPathNameToNtPathName_U(lpName,
                                           &NamedPipeName,
                                           NULL,
                                           NULL);
    if (!Result)
    {
        /* Conversion failed */
        SetLastError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }

    TRACE("Pipe name: %wZ\n", &NamedPipeName);
    TRACE("Pipe name: %S\n", NamedPipeName.Buffer);

    /* Always case insensitive, check if we got extra attributes */
    Attributes = OBJ_CASE_INSENSITIVE;
    if(lpSecurityAttributes)
    {
        /* We did; get the security descriptor */
        SecurityDescriptor = lpSecurityAttributes->lpSecurityDescriptor;

        /* And check if this is pipe's handle will beinheritable */
        if (lpSecurityAttributes->bInheritHandle)
            Attributes |= OBJ_INHERIT;
    }

    /* Now we can initialize the object attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               &NamedPipeName,
                               Attributes,
                               NULL,
                               SecurityDescriptor);

    /* Setup the default Desired Access */
    DesiredAccess = SYNCHRONIZE | (dwOpenMode & (WRITE_DAC |
                                                 WRITE_OWNER |
                                                 ACCESS_SYSTEM_SECURITY));

    /* Convert to NT Create Flags */
    if (dwOpenMode & FILE_FLAG_WRITE_THROUGH)
    {
        CreateOptions |= FILE_WRITE_THROUGH;
    }

    if (!(dwOpenMode & FILE_FLAG_OVERLAPPED))
    {
        CreateOptions |= FILE_SYNCHRONOUS_IO_NONALERT;
    }

    /* Handle all open modes */
    if (dwOpenMode & PIPE_ACCESS_OUTBOUND)
    {
        ShareAccess |= FILE_SHARE_READ;
        DesiredAccess |= GENERIC_WRITE;
    }

    if (dwOpenMode & PIPE_ACCESS_INBOUND)
    {
        ShareAccess |= FILE_SHARE_WRITE;
        DesiredAccess |= GENERIC_READ;
    }

    /* Handle the type flags */
    if (dwPipeMode & PIPE_TYPE_MESSAGE)
    {
        WriteModeMessage = FILE_PIPE_MESSAGE_TYPE;
    }
    else
    {
        WriteModeMessage = FILE_PIPE_BYTE_STREAM_TYPE;
    }

    /* Handle the mode flags */
    if (dwPipeMode & PIPE_READMODE_MESSAGE)
    {
        ReadModeMessage = FILE_PIPE_MESSAGE_MODE;
    }
    else
    {
        ReadModeMessage = FILE_PIPE_BYTE_STREAM_MODE;
    }

    /* Handle the blocking mode */
    if (dwPipeMode & PIPE_NOWAIT)
    {
        NonBlocking = FILE_PIPE_COMPLETE_OPERATION;
    }
    else
    {
        NonBlocking = FILE_PIPE_QUEUE_OPERATION;
    }

    /* Check if we have a timeout */
    if (nDefaultTimeOut)
    {
        /* Convert the time to NT format */
        DefaultTimeOut.QuadPart = UInt32x32To64(nDefaultTimeOut, -10000);
    }
    else
    {
        /* Use default timeout of 50 ms */
        DefaultTimeOut.QuadPart = -500000;
    }

    /* Now create the pipe */
    Status = NtCreateNamedPipeFile(&PipeHandle,
                                   DesiredAccess,
                                   &ObjectAttributes,
                                   &Iosb,
                                   ShareAccess,
                                   FILE_OPEN_IF,
                                   CreateOptions,
                                   WriteModeMessage,
                                   ReadModeMessage,
                                   NonBlocking,
                                   nMaxInstances,
                                   nInBufferSize,
                                   nOutBufferSize,
                                   &DefaultTimeOut);

    /* Normalize special error codes */
    if ((Status == STATUS_INVALID_DEVICE_REQUEST) ||
        (Status == STATUS_NOT_SUPPORTED))
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }

    /* Free the name */
    RtlFreeHeap(RtlGetProcessHeap(),
                0,
                NamedPipeName.Buffer);

    /* Check status */
    if (!NT_SUCCESS(Status))
    {
        /* Failed to create it */
        WARN("NtCreateNamedPipe failed (Status %x)!\n", Status);
        BaseSetLastNTError (Status);
        return INVALID_HANDLE_VALUE;
    }

    /* Return the handle */
    return PipeHandle;
}


/*
 * @implemented
 */
BOOL
WINAPI
WaitNamedPipeA(LPCSTR lpNamedPipeName,
               DWORD nTimeOut)
{
    BOOL r;
    UNICODE_STRING NameU;

    /* Convert the name to Unicode */
    Basep8BitStringToDynamicUnicodeString(&NameU, lpNamedPipeName);

    /* Call the Unicode API */
    r = WaitNamedPipeW(NameU.Buffer, nTimeOut);

    /* Free the Unicode string */
    RtlFreeUnicodeString(&NameU);

    /* Return result */
    return r;
}


/*
 * When NPFS will work properly, use this code instead. It is compatible with
 * Microsoft's NPFS.SYS. The main difference is that:
 *      - This code actually respects the timeout instead of ignoring it!
 *      - This code validates and creates the proper names for both UNC and local pipes
 *      - On NT, you open the *root* pipe directory (either \DosDevices\Pipe or
 *        \DosDevices\Unc\Server\Pipe) and then send the pipe to wait on in the
 *        FILE_PIPE_WAIT_FOR_BUFFER structure.
 */
#ifdef USING_PROPER_NPFS_WAIT_SEMANTICS
/*
 * @implemented
 */
BOOL
WINAPI
WaitNamedPipeW(LPCWSTR lpNamedPipeName,
               DWORD nTimeOut)
{
    UNICODE_STRING NamedPipeName, NewName, DevicePath, PipePrefix;
    ULONG NameLength;
    ULONG i;
    PWCHAR p;
    ULONG Type;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG WaitPipeInfoSize;
    PFILE_PIPE_WAIT_FOR_BUFFER WaitPipeInfo;

    /* Start by making a unicode string of the name */
    TRACE("Sent path: %S\n", lpNamedPipeName);
    RtlCreateUnicodeString(&NamedPipeName, lpNamedPipeName);
    NameLength = NamedPipeName.Length / sizeof(WCHAR);

    /* All slashes must become backslashes */
    for (i = 0; i < NameLength; i++)
    {
        /* Check and convert */
        if (NamedPipeName.Buffer[i] == L'/') NamedPipeName.Buffer[i] = L'\\';
    }

    /* Find the path type of the name we were given */
    NewName = NamedPipeName;
    Type = RtlDetermineDosPathNameType_U(lpNamedPipeName);

    /* Check if this was a device path, ie : "\\.\pipe\name" */
    if (Type == RtlPathTypeLocalDevice)
    {
        /* Make sure it's a valid prefix */
        RtlInitUnicodeString(&PipePrefix, L"\\\\.\\pipe\\");
        RtlPrefixString((PANSI_STRING)&PipePrefix, (PANSI_STRING)&NewName, TRUE);

        /* Move past it */
        NewName.Buffer += 9;
        NewName.Length -= 9 * sizeof(WCHAR);

        /* Initialize the Dos Devices name */
        TRACE("NewName: %wZ\n", &NewName);
        RtlInitUnicodeString(&DevicePath, L"\\DosDevices\\pipe\\");
    }
    else if (Type == RtlPathTypeRootLocalDevice)
    {
        /* The path is \\server\\pipe\name; find the pipename itself */
        p = &NewName.Buffer[2];

        /* First loop to get past the server name */
        do
        {
            /* Check if this is a backslash */
            if (*p == L'\\') break;

            /* Check next */
            p++;
        } while (*p);

        /* Now make sure the full name contains "pipe\" */
        if ((*p) && !(_wcsnicmp(p + 1, L"pipe\\", sizeof("pipe\\"))))
        {
            /* Get to the pipe name itself now */
            p += sizeof("pipe\\") - 1;
        }
        else
        {
            /* The name is invalid */
            WARN("Invalid name!\n");
            BaseSetLastNTError(STATUS_OBJECT_PATH_SYNTAX_BAD);
            return FALSE;
        }

        /* FIXME: Open \DosDevices\Unc\Server\Pipe\Name */
    }
    else
    {
        WARN("Invalid path type\n");
        BaseSetLastNTError(STATUS_OBJECT_PATH_SYNTAX_BAD);
        return FALSE;
    }

    /* Now calculate the total length of the structure and allocate it */
    WaitPipeInfoSize = FIELD_OFFSET(FILE_PIPE_WAIT_FOR_BUFFER, Name[0]) +
                       NewName.Length;
    WaitPipeInfo = RtlAllocateHeap(RtlGetProcessHeap(), 0, WaitPipeInfoSize);
    if (WaitPipeInfo == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    /* Initialize the object attributes */
    TRACE("Opening: %wZ\n", &DevicePath);
    InitializeObjectAttributes(&ObjectAttributes,
                               &DevicePath,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    /* Open the path */
    Status = NtOpenFile(&FileHandle,
                        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        /* Fail; couldn't open */
        WARN("Status: %lx\n", Status);
        BaseSetLastNTError(Status);
        RtlFreeUnicodeString(&NamedPipeName);
        RtlFreeHeap(RtlGetProcessHeap(), 0, WaitPipeInfo);
        return FALSE;
    }

    /* Check what timeout we got */
    if (nTimeOut == NMPWAIT_USE_DEFAULT_WAIT)
    {
        /* Don't use a timeout */
        WaitPipeInfo->TimeoutSpecified = FALSE;
    }
    else
    {
        /* Check if we should wait forever */
        if (nTimeOut == NMPWAIT_WAIT_FOREVER)
        {
            /* Set the max */
            WaitPipeInfo->Timeout.LowPart = 0;
            WaitPipeInfo->Timeout.HighPart = 0x80000000;
        }
        else
        {
            /* Convert to NT format */
            WaitPipeInfo->Timeout.QuadPart = UInt32x32To64(-10000, nTimeOut);
        }

        /* In both cases, we do have a timeout */
        WaitPipeInfo->TimeoutSpecified = TRUE;
    }

    /* Set the length and copy the name */
    WaitPipeInfo->NameLength = NewName.Length;
    RtlCopyMemory(WaitPipeInfo->Name, NewName.Buffer, NewName.Length);

    /* Get rid of the full name */
    RtlFreeUnicodeString(&NamedPipeName);

    /* Let NPFS know of our request */
    Status = NtFsControlFile(FileHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             FSCTL_PIPE_WAIT,
                             WaitPipeInfo,
                             WaitPipeInfoSize,
                             NULL,
                             0);

    /* Free our pipe info data and close the handle */
    RtlFreeHeap(RtlGetProcessHeap(), 0, WaitPipeInfo);
    NtClose(FileHandle);

    /* Check the status */
    if (!NT_SUCCESS(Status))
    {
        /* Failure to wait on the pipe */
        WARN("Status: %lx\n", Status);
        BaseSetLastNTError (Status);
        return FALSE;
     }

    /* Success */
    return TRUE;
}
#else
/*
 * @implemented
 */
BOOL
WINAPI
WaitNamedPipeW(LPCWSTR lpNamedPipeName,
               DWORD nTimeOut)
{
    UNICODE_STRING NamedPipeName;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_PIPE_WAIT_FOR_BUFFER WaitPipe;
    HANDLE FileHandle;
    IO_STATUS_BLOCK Iosb;

    if (RtlDosPathNameToNtPathName_U(lpNamedPipeName,
                                     &NamedPipeName,
                                     NULL,
                                     NULL) == FALSE)
    {
        return FALSE;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &NamedPipeName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &ObjectAttributes,
                        &Iosb,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        RtlFreeUnicodeString(&NamedPipeName);
        return FALSE;
    }

    /* Check what timeout we got */
    if (nTimeOut == NMPWAIT_WAIT_FOREVER)
    {
        /* Don't use a timeout */
        WaitPipe.TimeoutSpecified = FALSE;
    }
    else
    {
        /* Check if default */
        if (nTimeOut == NMPWAIT_USE_DEFAULT_WAIT)
        {
            /* Set it to 0 */
            WaitPipe.Timeout.LowPart = 0;
            WaitPipe.Timeout.HighPart = 0;
        }
        else
        {
            /* Convert to NT format */
            WaitPipe.Timeout.QuadPart = UInt32x32To64(-10000, nTimeOut);
        }

        /* In both cases, we do have a timeout */
        WaitPipe.TimeoutSpecified = TRUE;
    }

    Status = NtFsControlFile(FileHandle,
                             NULL,
                             NULL,
                             NULL,
                             &Iosb,
                             FSCTL_PIPE_WAIT,
                             &WaitPipe,
                             sizeof(WaitPipe),
                             NULL,
                             0);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        RtlFreeUnicodeString(&NamedPipeName);
        return FALSE;
    }

    RtlFreeUnicodeString(&NamedPipeName);
    return TRUE;
}
#endif


/*
 * @implemented
 */
BOOL
WINAPI
ConnectNamedPipe(IN HANDLE hNamedPipe,
                 IN LPOVERLAPPED lpOverlapped)
{
    NTSTATUS Status;

    if (lpOverlapped != NULL)
    {
        PVOID ApcContext;

        lpOverlapped->Internal = STATUS_PENDING;
        ApcContext = (((ULONG_PTR)lpOverlapped->hEvent & 0x1) ? NULL : lpOverlapped);

        Status = NtFsControlFile(hNamedPipe,
                                 lpOverlapped->hEvent,
                                 NULL,
                                 ApcContext,
                                 (PIO_STATUS_BLOCK)lpOverlapped,
                                 FSCTL_PIPE_LISTEN,
                                 NULL,
                                 0,
                                 NULL,
                                 0);

        /* return FALSE in case of failure and pending operations! */
        if (!NT_SUCCESS(Status) || Status == STATUS_PENDING)
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
    }
    else
    {
        IO_STATUS_BLOCK Iosb;

        Status = NtFsControlFile(hNamedPipe,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &Iosb,
                                 FSCTL_PIPE_LISTEN,
                                 NULL,
                                 0,
                                 NULL,
                                 0);

        /* wait in case operation is pending */
        if (Status == STATUS_PENDING)
        {
             Status = NtWaitForSingleObject(hNamedPipe,
                                            FALSE,
                                            NULL);
             if (NT_SUCCESS(Status))
             {
                 Status = Iosb.Status;
             }
        }

        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetNamedPipeHandleState(HANDLE hNamedPipe,
                        LPDWORD lpMode,
                        LPDWORD lpMaxCollectionCount,
                        LPDWORD lpCollectDataTimeout)
{
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    /* Check if the Mode is being changed */
    if (lpMode)
    {
        FILE_PIPE_INFORMATION Settings;

        /* Set the Completion Mode */
        Settings.CompletionMode = (*lpMode & PIPE_NOWAIT) ?
                                  FILE_PIPE_COMPLETE_OPERATION : FILE_PIPE_QUEUE_OPERATION;

        /* Set the Read Mode */
        Settings.ReadMode = (*lpMode & PIPE_READMODE_MESSAGE) ?
                            FILE_PIPE_MESSAGE_MODE: FILE_PIPE_BYTE_STREAM_MODE;

        /* Send the changes to the Driver */
        Status = NtSetInformationFile(hNamedPipe,
                                      &Iosb,
                                      &Settings,
                                      sizeof(FILE_PIPE_INFORMATION),
                                      FilePipeInformation);
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
    }

    /* Check if the Collection count or Timeout are being changed */
    if (lpMaxCollectionCount || lpCollectDataTimeout)
    {
        FILE_PIPE_REMOTE_INFORMATION RemoteSettings;

        /* Setting one without the other would delete it, so we read old one */
        if (!lpMaxCollectionCount || !lpCollectDataTimeout)
        {
            Status = NtQueryInformationFile(hNamedPipe,
                                            &Iosb,
                                            &RemoteSettings,
                                            sizeof(FILE_PIPE_REMOTE_INFORMATION),
                                            FilePipeRemoteInformation);
            if (!NT_SUCCESS(Status))
            {
                BaseSetLastNTError(Status);
                return FALSE;
            }
        }

        /* Now set the new settings */
        RemoteSettings.MaximumCollectionCount = (lpMaxCollectionCount) ?
                                                *lpMaxCollectionCount :
                                                RemoteSettings.MaximumCollectionCount;
        if (lpCollectDataTimeout)
        {
            /* Convert it to Quad */
            RemoteSettings.CollectDataTime.QuadPart =
                -(LONGLONG)UInt32x32To64(10000, *lpCollectDataTimeout);
        }

        /* Tell the driver to change them */
        Status = NtSetInformationFile(hNamedPipe,
                                      &Iosb,
                                      &RemoteSettings,
                                      sizeof(FILE_PIPE_REMOTE_INFORMATION),
                                      FilePipeRemoteInformation);
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
CallNamedPipeA(LPCSTR lpNamedPipeName,
               LPVOID lpInBuffer,
               DWORD nInBufferSize,
               LPVOID lpOutBuffer,
               DWORD nOutBufferSize,
               LPDWORD lpBytesRead,
               DWORD nTimeOut)
{
    PUNICODE_STRING PipeName = &NtCurrentTeb()->StaticUnicodeString;
    ANSI_STRING AnsiPipe;

    /* Initialize the string as ANSI_STRING and convert to Unicode */
    RtlInitAnsiString(&AnsiPipe, (LPSTR)lpNamedPipeName);
    RtlAnsiStringToUnicodeString(PipeName, &AnsiPipe, FALSE);

    /* Call the Unicode function */
    return CallNamedPipeW(PipeName->Buffer,
                          lpInBuffer,
                          nInBufferSize,
                          lpOutBuffer,
                          nOutBufferSize,
                          lpBytesRead,
                          nTimeOut);
}


/*
 * @implemented
 */
BOOL
WINAPI
CallNamedPipeW(LPCWSTR lpNamedPipeName,
               LPVOID lpInBuffer,
               DWORD nInBufferSize,
               LPVOID lpOutBuffer,
               DWORD nOutBufferSize,
               LPDWORD lpBytesRead,
               DWORD nTimeOut)
{
    HANDLE hPipe;
    BOOL bRetry = TRUE;
    BOOL bError;
    DWORD dwPipeMode;

    while (TRUE)
    {
        /* Try creating it */
        hPipe = CreateFileW(lpNamedPipeName,
                            GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

        /* Success, break out */
        if (hPipe != INVALID_HANDLE_VALUE)
            break;

        /* Already tried twice, give up */
        if (bRetry == FALSE)
            return FALSE;

        /* Wait on it */
        WaitNamedPipeW(lpNamedPipeName, nTimeOut);

        /* Get ready to try again */
        bRetry = FALSE;
    }

    /* Set the pipe mode */
    dwPipeMode = PIPE_READMODE_MESSAGE | PIPE_WAIT;
    bError = SetNamedPipeHandleState(hPipe, &dwPipeMode, NULL, NULL);
    if (!bError)
    {
        /* Couldn't change state, fail */
        CloseHandle(hPipe);
        return FALSE;
    }

    /* Do the transact */
    bError = TransactNamedPipe(hPipe,
                               lpInBuffer,
                               nInBufferSize,
                               lpOutBuffer,
                               nOutBufferSize,
                               lpBytesRead,
                               NULL);

    /* Close the handle */
    CloseHandle(hPipe);

    return bError;
}


/*
 * @implemented
 */
BOOL
WINAPI
DisconnectNamedPipe(HANDLE hNamedPipe)
{
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    /* Send the FSCTL to the driver */
    Status = NtFsControlFile(hNamedPipe,
                             NULL,
                             NULL,
                             NULL,
                             &Iosb,
                             FSCTL_PIPE_DISCONNECT,
                             NULL,
                             0,
                             NULL,
                             0);
    if (Status == STATUS_PENDING)
    {
        /* Wait on NPFS to finish and get updated status */
        Status = NtWaitForSingleObject(hNamedPipe, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = Iosb.Status;
    }

    /* Check for error */
    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
GetNamedPipeHandleStateW(HANDLE hNamedPipe,
                         LPDWORD lpState,
                         LPDWORD lpCurInstances,
                         LPDWORD lpMaxCollectionCount,
                         LPDWORD lpCollectDataTimeout,
                         LPWSTR lpUserName,
                         DWORD nMaxUserNameSize)
{
    IO_STATUS_BLOCK StatusBlock;
    NTSTATUS Status;

    if (lpState != NULL)
    {
        FILE_PIPE_INFORMATION PipeInfo;

        Status = NtQueryInformationFile(hNamedPipe,
                                        &StatusBlock,
                                        &PipeInfo,
                                        sizeof(FILE_PIPE_INFORMATION),
                                        FilePipeInformation);
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }

        *lpState = ((PipeInfo.CompletionMode != FILE_PIPE_QUEUE_OPERATION) ? PIPE_NOWAIT : PIPE_WAIT);
        *lpState |= ((PipeInfo.ReadMode != FILE_PIPE_BYTE_STREAM_MODE) ? PIPE_READMODE_MESSAGE : PIPE_READMODE_BYTE);
    }

    if(lpCurInstances != NULL)
    {
        FILE_PIPE_LOCAL_INFORMATION LocalInfo;

        Status = NtQueryInformationFile(hNamedPipe,
                                        &StatusBlock,
                                        &LocalInfo,
                                        sizeof(FILE_PIPE_LOCAL_INFORMATION),
                                        FilePipeLocalInformation);
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }

        *lpCurInstances = min(LocalInfo.CurrentInstances, PIPE_UNLIMITED_INSTANCES);
    }

    if (lpMaxCollectionCount != NULL || lpCollectDataTimeout != NULL)
    {
        FILE_PIPE_REMOTE_INFORMATION RemoteInfo;

        Status = NtQueryInformationFile(hNamedPipe,
                                        &StatusBlock,
                                        &RemoteInfo,
                                        sizeof(FILE_PIPE_REMOTE_INFORMATION),
                                        FilePipeRemoteInformation);
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }

        if (lpMaxCollectionCount != NULL)
        {
            *lpMaxCollectionCount = RemoteInfo.MaximumCollectionCount;
        }

        if(lpCollectDataTimeout != NULL)
        {
            /* FIXME */
           *lpCollectDataTimeout = 0;
        }
    }

    if (lpUserName != NULL)
    {
      /* FIXME - open the thread token, call ImpersonateNamedPipeClient() and
                 retreive the user name with GetUserName(), revert the impersonation
                 and finally restore the thread token */
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetNamedPipeHandleStateA(HANDLE hNamedPipe,
                         LPDWORD lpState,
                         LPDWORD lpCurInstances,
                         LPDWORD lpMaxCollectionCount,
                         LPDWORD lpCollectDataTimeout,
                         LPSTR lpUserName,
                         DWORD nMaxUserNameSize)
{
    UNICODE_STRING UserNameW = { 0, 0, NULL };
    ANSI_STRING UserNameA;
    BOOL Ret;

    if(lpUserName != NULL)
    {
        UserNameW.MaximumLength = (USHORT)nMaxUserNameSize * sizeof(WCHAR);
        UserNameW.Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, UserNameW.MaximumLength);
        if (UserNameW.Buffer == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        UserNameA.Buffer = lpUserName;
        UserNameA.Length = 0;
        UserNameA.MaximumLength = (USHORT)nMaxUserNameSize;
    }

    Ret = GetNamedPipeHandleStateW(hNamedPipe,
                                   lpState,
                                   lpCurInstances,
                                   lpMaxCollectionCount,
                                   lpCollectDataTimeout,
                                   UserNameW.Buffer,
                                   nMaxUserNameSize);
    if (Ret && lpUserName != NULL)
    {
        NTSTATUS Status;

        RtlInitUnicodeString(&UserNameW, UserNameW.Buffer);
        Status = RtlUnicodeStringToAnsiString(&UserNameA, &UserNameW, FALSE);
        if (!NT_SUCCESS(Status))
        {
            BaseSetLastNTError(Status);
            Ret = FALSE;
        }
    }

    if (UserNameW.Buffer != NULL)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, UserNameW.Buffer);
    }

    return Ret;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetNamedPipeInfo(HANDLE hNamedPipe,
                 LPDWORD lpFlags,
                 LPDWORD lpOutBufferSize,
                 LPDWORD lpInBufferSize,
                 LPDWORD lpMaxInstances)
{
    FILE_PIPE_LOCAL_INFORMATION PipeLocalInformation;
    IO_STATUS_BLOCK StatusBlock;
    NTSTATUS Status;

    Status = NtQueryInformationFile(hNamedPipe,
                                    &StatusBlock,
                                    &PipeLocalInformation,
                                    sizeof(FILE_PIPE_LOCAL_INFORMATION),
                                    FilePipeLocalInformation);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    if (lpFlags != NULL)
    {
        *lpFlags = (PipeLocalInformation.NamedPipeEnd == FILE_PIPE_SERVER_END) ? PIPE_SERVER_END : PIPE_CLIENT_END;
        *lpFlags |= (PipeLocalInformation.NamedPipeType == 1) ? PIPE_TYPE_MESSAGE : PIPE_TYPE_BYTE;
    }

    if (lpOutBufferSize != NULL)
        *lpOutBufferSize = PipeLocalInformation.OutboundQuota;

    if (lpInBufferSize != NULL)
        *lpInBufferSize = PipeLocalInformation.InboundQuota;

    if (lpMaxInstances != NULL)
    {
        if (PipeLocalInformation.MaximumInstances >= 255)
            *lpMaxInstances = PIPE_UNLIMITED_INSTANCES;
        else
            *lpMaxInstances = PipeLocalInformation.MaximumInstances;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
PeekNamedPipe(HANDLE hNamedPipe,
              LPVOID lpBuffer,
              DWORD nBufferSize,
              LPDWORD lpBytesRead,
              LPDWORD lpTotalBytesAvail,
              LPDWORD lpBytesLeftThisMessage)
{
    PFILE_PIPE_PEEK_BUFFER Buffer;
    IO_STATUS_BLOCK Iosb;
    ULONG BufferSize;
    ULONG BytesRead;
    NTSTATUS Status;

    /* Calculate the buffer space that we'll need and allocate it */
    BufferSize = FIELD_OFFSET(FILE_PIPE_PEEK_BUFFER, Data[nBufferSize]);
    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferSize);
    if (Buffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    /* Tell the driver to seek */
    Status = NtFsControlFile(hNamedPipe,
                             NULL,
                             NULL,
                             NULL,
                             &Iosb,
                             FSCTL_PIPE_PEEK,
                             NULL,
                             0,
                             Buffer,
                             BufferSize);
    if (Status == STATUS_PENDING)
    {
        /* Wait for npfs to be done, and update the status */
        Status = NtWaitForSingleObject(hNamedPipe, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = Iosb.Status;
    }

    /* Overflow is success for us */
    if (Status == STATUS_BUFFER_OVERFLOW)
        Status = STATUS_SUCCESS;

    /* If we failed */
    if (!NT_SUCCESS(Status))
    {
        /* Free the buffer and return failure */
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        BaseSetLastNTError(Status);
        return FALSE;
    }

    /* Check if caller requested bytes available */
    if (lpTotalBytesAvail)
    {
        /* Return bytes available */
        *lpTotalBytesAvail = Buffer->ReadDataAvailable;
    }

    /* Calculate the bytes returned, minus our structure overhead */
    BytesRead = (ULONG)(Iosb.Information -
                        FIELD_OFFSET(FILE_PIPE_PEEK_BUFFER, Data[0]));
    ASSERT(BytesRead <= nBufferSize);

    /* Check if caller requested bytes read */
    if (lpBytesRead)
    {
        /* Return the bytes read */
        *lpBytesRead = BytesRead;
    }

    /* Check if caller requested bytes left */
    if (lpBytesLeftThisMessage)
    {
        /* Calculate total minus what we returned and our structure overhead */
        *lpBytesLeftThisMessage = Buffer->MessageLength - BytesRead;
    }

    /* Check if the caller wanted to see the actual data */
    if (lpBuffer)
    {
        /* Give him what he wants */
        RtlCopyMemory(lpBuffer,
                      Buffer->Data,
                      BytesRead);
    }

    /* Free the buffer */
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
TransactNamedPipe(IN HANDLE hNamedPipe,
                  IN LPVOID lpInBuffer,
                  IN DWORD nInBufferSize,
                  OUT LPVOID lpOutBuffer,
                  IN DWORD nOutBufferSize,
                  OUT LPDWORD lpBytesRead  OPTIONAL,
                  IN LPOVERLAPPED lpOverlapped  OPTIONAL)
{
    NTSTATUS Status;

    if (lpBytesRead != NULL)
    {
        *lpBytesRead = 0;
    }

    if (lpOverlapped != NULL)
    {
        PVOID ApcContext;

        ApcContext = (((ULONG_PTR)lpOverlapped->hEvent & 0x1) ? NULL : lpOverlapped);
        lpOverlapped->Internal = STATUS_PENDING;

        Status = NtFsControlFile(hNamedPipe,
                                 lpOverlapped->hEvent,
                                 NULL,
                                 ApcContext,
                                 (PIO_STATUS_BLOCK)lpOverlapped,
                                 FSCTL_PIPE_TRANSCEIVE,
                                 lpInBuffer,
                                 nInBufferSize,
                                 lpOutBuffer,
                                 nOutBufferSize);
        if (!NT_SUCCESS(Status) || Status == STATUS_PENDING)
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }

        if (lpBytesRead != NULL)
        {
            *lpBytesRead = lpOverlapped->InternalHigh;
        }
    }
    else
    {
#if 0 /* We don't implement FSCTL_PIPE_TRANSCEIVE yet */
        IO_STATUS_BLOCK Iosb;

        Status = NtFsControlFile(hNamedPipe,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &Iosb,
                                 FSCTL_PIPE_TRANSCEIVE,
                                 lpInBuffer,
                                 nInBufferSize,
                                 lpOutBuffer,
                                 nOutBufferSize);
        if (Status == STATUS_PENDING)
        {
            Status = NtWaitForSingleObject(hNamedPipe,
                                           FALSE,
                                           NULL);
            if (NT_SUCCESS(Status))
                Status = Iosb.Status;
        }

        if (NT_SUCCESS(Status))
        {
            /* lpNumberOfBytesRead must not be NULL here, in fact Win doesn't
               check that case either and crashes (only after the operation
               completed) */
            *lpBytesRead = Iosb.Information;
        }
        else
        {
            BaseSetLastNTError(Status);
            return FALSE;
        }
#else /* Workaround while FSCTL_PIPE_TRANSCEIVE not available */
        DWORD nActualBytes;

        while (0 != nInBufferSize &&
               WriteFile(hNamedPipe, lpInBuffer, nInBufferSize, &nActualBytes,
                         NULL))
        {
             lpInBuffer = (LPVOID)((char *) lpInBuffer + nActualBytes);
             nInBufferSize -= nActualBytes;
        }

        if (0 != nInBufferSize)
        {
             /* Must have dropped out of the while 'cause WriteFile failed */
             return FALSE;
        }

        if (!ReadFile(hNamedPipe, lpOutBuffer, nOutBufferSize, &nActualBytes,
                      NULL))
        {
             return FALSE;
        }

        if (NULL != lpBytesRead)
        {
            *lpBytesRead = nActualBytes;
        }
#endif
    }

    return TRUE;
}

/* EOF */
