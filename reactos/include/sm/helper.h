#if !defined(INCLUDE_SM_HELPER_H)
#define INCLUDE_SM_HELPER_H

#if !defined(__SM_API_H)
#include <sm/api.h>
#endif

/* $Id$ */

/* smlib/connect.c */
NTSTATUS STDCALL
SmConnectApiPort (IN      PUNICODE_STRING  pSbApiPortName  OPTIONAL,
		  IN      HANDLE           hSbApiPort      OPTIONAL,
		  IN      WORD             wSubsystem      OPTIONAL, /* pe.h */
		  IN OUT  PHANDLE          phSmApiPort);
/* smlib/compses.c */
NTSTATUS STDCALL
SmCompleteSession (IN     HANDLE  hSmApiPort,
		   IN     HANDLE  hSbApiPort,
		   IN     HANDLE  hApiPort);
/* smlib/execpgm.c */
NTSTATUS STDCALL
SmExecuteProgram (IN     HANDLE           hSmApiPort,
		  IN     PUNICODE_STRING  Pgm
		  );
/* smdll/query.c */
NTSTATUS STDCALL
SmQueryInformation (IN      HANDLE                SmApiPort,
		    IN      SM_INFORMATION_CLASS  SmInformationClass,
		    IN OUT  PVOID                 Data,
		    IN      ULONG                 DataLength,
		    IN OUT  PULONG                ReturnedDataLength OPTIONAL);
/* smlib/lookupss.c */
NTSTATUS STDCALL
SmLookupSubsystem (IN     PWSTR   Name,
		   IN OUT PWSTR   Data,
		   IN OUT PULONG  DataLength,
		   IN OUT PULONG  DataType,
		   IN     PVOID   Environment OPTIONAL);
#endif /* ndef INCLUDE_SM_HELPER_H */
