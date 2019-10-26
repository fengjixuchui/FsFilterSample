/*++

Module Name:

    fsFilter.c

Abstract:

    This is the main module of the fsFilter miniFilter driver.

Environment:

    Kernel mode

--*/

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNING
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE_CORE
#include <fltKernel.h>
//#include <dontuse.h>
#include <suppress.h>
#include <Ntstrsafe.h>

#include "conf.h"
#include "dbgLog.h"

#include "mspyKern.h"
#include "swapBuffers.h"
#include "fsFilter.h"

#include <wchar.h>

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(INIT, ReadDriverParameters)
#pragma alloc_text(PAGE, WriteDriverParameters)
#pragma alloc_text(PAGE, Unload)
#pragma alloc_text(PAGE, CleanupVolumeContext)
#pragma alloc_text(PAGE, InstanceQueryTeardown)
#pragma alloc_text(PAGE, InstanceSetup)
#pragma alloc_text(PAGE, InstanceTeardownStart)
#pragma alloc_text(PAGE, InstanceTeardownComplete)
#endif


//////////////////////////////////////////////////////////////////////////////////////
//
//  Global variables
//
//////////////////////////////////////////////////////////////////////////////////////

PFLT_FILTER gFilterHandle;
ULONG_PTR OperationStatusCtx = 1;
#define PROTECTIONDIRNAME L"ProtectedDir"
const UNICODE_STRING DEFAULTPROTECTIONDIRNAME = RTL_CONSTANT_STRING(L"\\EncryptionMinifilterDir\\");
const UNICODE_STRING ProtectedFilExt = RTL_CONSTANT_STRING(L".txt");
UNICODE_STRING ProtectedDirName;
UNICODE_STRING registryPath;


/*************************************************************************
Pool Tags
*************************************************************************/

#define CONTEXT_TAG         'cBS_'
#define PRE_2_POST_TAG      'pBS_'
#define P_DIR_TAG			'RID_'
#define REG_TAG				'GER_'
#define DBG_TAG				'gbd_'


/*************************************************************************
Local structures
*************************************************************************/

//
//  This is a volume context, one of these are attached to each volume
//  we monitor.  This is used to get a "DOS" name for debug display.
//

typedef struct _VOLUME_CONTEXT {

	//
	//  Holds the name to display
	//

	UNICODE_STRING Name;

	//
	//  Holds the sector size for this volume.
	//

	ULONG SectorSize;

} VOLUME_CONTEXT, *PVOLUME_CONTEXT;

//
//  This is a context structure that is used to pass state from our
//  pre-operation callback to our post-operation callback.
//

typedef struct _PRE_2_POST_CONTEXT {
	//
	//  Pointer to our volume context structure.  We always get the context
	//  in the preOperation path because you can not safely get it at DPC
	//  level.  We then release it in the postOperation path.  It is safe
	//  to release contexts at DPC level.
	//
	PVOLUME_CONTEXT VolCtx;

	//PSTREAM_CONTEXT pStreamCtx;

	//
	//  Since the post-operation parameters always receive the "original"
	//  parameters passed to the operation, we need to pass our new destination
	//  buffer to our post operation routine so we can free it.
	//
	PVOID SwappedBuffer;

} PRE_2_POST_CONTEXT, *PPRE_2_POST_CONTEXT;

//
//  This is a lookAside list used to allocate our pre-2-post structure.
//

NPAGED_LOOKASIDE_LIST Pre2PostContextList;

//
//  operation registration
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {

	{ IRP_MJ_CREATE,
	0,
	PreOperation,
	PostOperation },

	{ IRP_MJ_READ,
	0,
	PreOperation,
	PostOperation },

	{ IRP_MJ_WRITE,
	0,
	PreOperation,
	PostOperation },

	{ IRP_MJ_DIRECTORY_CONTROL,
	0,
	PreOperation,
	PostOperation },

	{ IRP_MJ_SET_INFORMATION,
	0,
	PreOperation,
	PostOperation },

#if 0 // TODO - List all of the requests to filter.

    { IRP_MJ_CREATE_NAMED_PIPE,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_CLOSE,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_QUERY_INFORMATION,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_QUERY_EA,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_SET_EA,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_FLUSH_BUFFERS,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_QUERY_VOLUME_INFORMATION,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_SET_VOLUME_INFORMATION,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_FILE_SYSTEM_CONTROL,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_DEVICE_CONTROL,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_INTERNAL_DEVICE_CONTROL,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_SHUTDOWN,
      0,
      PreOperationNoPostOperation,
      NULL },                               //post operations not supported

    { IRP_MJ_LOCK_CONTROL,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_CLEANUP,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_CREATE_MAILSLOT,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_QUERY_SECURITY,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_SET_SECURITY,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_QUERY_QUOTA,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_SET_QUOTA,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_PNP,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_ACQUIRE_FOR_MOD_WRITE,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_RELEASE_FOR_MOD_WRITE,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_ACQUIRE_FOR_CC_FLUSH,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_RELEASE_FOR_CC_FLUSH,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_NETWORK_QUERY_OPEN,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_MDL_READ,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_MDL_READ_COMPLETE,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_PREPARE_MDL_WRITE,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_MDL_WRITE_COMPLETE,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_VOLUME_MOUNT,
      0,
      PreOperation,
      PostOperation },

    { IRP_MJ_VOLUME_DISMOUNT,
      0,
      PreOperation,
      PostOperation },

#endif // TODO

    { IRP_MJ_OPERATION_END }
};

//
//  Context definitions we currently care about.  Note that the system will
//  create a lookAside list for the volume context because an explicit size
//  of the context is specified.
//

CONST FLT_CONTEXT_REGISTRATION ContextNotifications[] = {

	{ FLT_VOLUME_CONTEXT,
	0,
	CleanupVolumeContext,
	sizeof(VOLUME_CONTEXT),
	CONTEXT_TAG },

{ FLT_CONTEXT_END }
};

//
//  This defines what we want to filter with FltMgr
//

CONST FLT_REGISTRATION FilterRegistration = {

    sizeof( FLT_REGISTRATION ),         //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags

	ContextNotifications,               //  Context
    Callbacks,                          //  Operation callbacks

    Unload,                           //  MiniFilterUnload

    InstanceSetup,                    //  InstanceSetup
    InstanceQueryTeardown,            //  InstanceQueryTeardown
    InstanceTeardownStart,            //  InstanceTeardownStart
    InstanceTeardownComplete,         //  InstanceTeardownComplete

    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent

};

/////////////////////////////////////////////////////////////////////
///
///                 Routines
///
//////////////////////////////////////////////////////////////////////


VOID
ReadDriverParameters(
	__in PUNICODE_STRING RegistryPath
)
/*++

Routine Description:

This routine tries to read the driver-specific parameters from
the registry.  These values will be found in the registry location
indicated by the RegistryPath passed in.

Arguments:

RegistryPath - the path key passed to the driver during driver entry.

Return Value:

None.

--*/
{
	UNICODE_STRING valueName;
	OBJECT_ATTRIBUTES attributes;
	HANDLE driverRegKey;
	NTSTATUS status;
	ULONG resultLength = 0;    
   	PKEY_VALUE_PARTIAL_INFORMATION pValuePartialInfo = NULL;
	WCHAR* buffer = NULL; 
	WCHAR* pDir = NULL;

	//SwapReadDriverParameters(RegistryPath);
	//SpyReadDriverParameters(RegistryPath);

	//
    //  Open the registry
    //

    InitializeObjectAttributes( &attributes,
                                RegistryPath,
                                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                NULL,
                                NULL );

    status = ZwOpenKey( &driverRegKey,
                        KEY_READ,
                        &attributes );

    if (!NT_SUCCESS( status )) {

        return;
    }

    //
    // Read the PROTECTIONDIRNAME entry from the registry
    //

    RtlInitUnicodeString( &valueName, PROTECTIONDIRNAME );

	status = ZwQueryValueKey( driverRegKey,
                              &valueName,
                              KeyValuePartialInformation,
                              pValuePartialInfo,
                              0,  
                              &resultLength );

	if(STATUS_OBJECT_NAME_NOT_FOUND == status || STATUS_INVALID_PARAMETER == status) return;

	if( STATUS_BUFFER_OVERFLOW == status || STATUS_BUFFER_TOO_SMALL == status ) {
		
		//
		//  Allocate nonPaged memory for the buffer we are swapping to.
		//  If we fail to get the memory, just don't swap buffers on this
		//  operation.
		//

		pValuePartialInfo = ExAllocatePoolWithTag(NonPagedPool, resultLength, DBG_TAG);
		if (pValuePartialInfo == NULL) {
			LOG_PRINT(LOGFL_ERRORS, 
				("fsFilter!ExAllocatePoolWithTag-protecteddirname: Failed to allocate %d bytes of memory\n", resultLength));
			return;
		}	
	}

	status = ZwQueryValueKey( driverRegKey,
                              &valueName,
                              KeyValuePartialInformation,
                              pValuePartialInfo,
                              resultLength,
                              &resultLength );	

    if (NT_SUCCESS( status )) {

        ASSERT( pValuePartialInfo->Type == REG_SZ );
		buffer = ExAllocatePoolWithTag(NonPagedPool, pValuePartialInfo->DataLength + sizeof(UNICODE_NULL), P_DIR_TAG);
		RtlCopyMemory(buffer, pValuePartialInfo->Data, pValuePartialInfo->DataLength);
		buffer[pValuePartialInfo->DataLength] = UNICODE_NULL;
		RtlInitUnicodeString(&ProtectedDirName, buffer);
    } else {
		buffer = ExAllocatePoolWithTag(NonPagedPool, DEFAULTPROTECTIONDIRNAME.Length + sizeof(UNICODE_NULL), P_DIR_TAG);
		RtlCopyMemory(buffer, DEFAULTPROTECTIONDIRNAME.Buffer, DEFAULTPROTECTIONDIRNAME.Length);
		buffer[DEFAULTPROTECTIONDIRNAME.Length] = UNICODE_NULL;
		RtlInitUnicodeString(&ProtectedDirName, buffer);
	}

	ExFreePoolWithTag(pValuePartialInfo, DBG_TAG);

	pDir = ExAllocatePoolWithTag(NonPagedPool, MAX_PATH, REG_TAG);
	ASSERT(RegistryPath->Length + sizeof(UNICODE_NULL) < MAX_PATH);
	RtlCopyMemory(pDir, RegistryPath->Buffer, RegistryPath->Length);
	pDir[RegistryPath->Length/2] = UNICODE_NULL;
	RtlInitUnicodeString(&registryPath, pDir);
	registryPath.Length = RegistryPath->Length;

	DbgPrint("\n ProtectedDirName is %s", ProtectedDirName.Buffer);
	
	return;
}


VOID
WriteDriverParameters()
/*++

Routine Description:

This routine tries to write the driver-specific parameters from
the registry.  These values will be found in the registry location
indicated by the RegistryPath passed in.

Arguments:

RegistryPath - the path key passed to the driver during driver entry.

Return Value:

None.

--*/
{
	UNICODE_STRING valueName;
	OBJECT_ATTRIBUTES attributes;
	HANDLE driverRegKey;
	NTSTATUS status;
	ULONG resultLength;
	UCHAR* buffer = NULL;
	PUNICODE_STRING RegistryPath = &registryPath;

	PAGED_CODE();

 	//
    //  Open the registry
    //


    InitializeObjectAttributes( &attributes,
                                RegistryPath,
                                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                NULL,
                                NULL );

    status = ZwOpenKey( &driverRegKey,
                        GENERIC_ALL,
                        &attributes );

    if (!NT_SUCCESS( status )) {

        return;
    }

    //
    // Read the PROTECTIONDIRNAME entry from the registry
    //

    RtlInitUnicodeString( &valueName, PROTECTIONDIRNAME );

	status = ZwQueryValueKey( driverRegKey,
                              &valueName,
                              KeyValuePartialInformation,
                              buffer,
                              0,  
                              &resultLength );

	if(STATUS_OBJECT_NAME_NOT_FOUND == status || STATUS_INVALID_PARAMETER == status) return;

    status = ZwSetValueKey(driverRegKey,&valueName,0,REG_SZ,ProtectedDirName.Buffer,ProtectedDirName.Length);

    if (!NT_SUCCESS(status)){ DbgPrint("\n Write protecteddirname value failed!\n"); };
    
	ZwClose(driverRegKey);
	
	return;
}

NTSTATUS
InstanceSetup (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_SETUP_FLAGS Flags,
    __in DEVICE_TYPE VolumeDeviceType,
    __in FLT_FILESYSTEM_TYPE VolumeFilesystemType
    )
/*++

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Flags describing the reason for this attach request.

Return Value:

    STATUS_SUCCESS - attach
    STATUS_FLT_DO_NOT_ATTACH - do not attach

--*/
{
	NTSTATUS status = STATUS_FLT_DO_NOT_ATTACH;
	PAGED_CODE();
	status = SwapInstanceSetup(FltObjects, Flags, VolumeDeviceType, VolumeFilesystemType);
	return status;
}


NTSTATUS
InstanceQueryTeardown (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    )
/*++

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Indicating where this detach request came from.

Return Value:

    Returns the status of this operation.

--*/
{
	NTSTATUS status = SwapInstanceQueryTeardown(FltObjects, Flags);
	PAGED_CODE();
	status = SpyQueryTeardown(FltObjects, Flags);
	// if(ProtectedDirName.Length > 50 )   //THIS MUST BE EXALLOCTED MEM.  50 is DEFAULTPROTECTEDDIRNAME LENGTH
	// {
	//  	ExFreePool(&ProtectedDirName.Buffer);
	// 	 ProtectedDirName.Buffer = NULL;
	// 	 ProtectedDirName.Length = 0;
	// }
    return status;
}


VOID
InstanceTeardownStart (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();
    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("!InstanceTeardownStart: Entered\n") );
}


VOID
InstanceTeardownComplete (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("!InstanceTeardownComplete: Entered\n") );
}


/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/

NTSTATUS
DriverEntry (
    __in PDRIVER_OBJECT DriverObject,
    __in PUNICODE_STRING RegistryPath
    )
/*++

Arguments:

    DriverObject - Pointer to driver object created by the system to
        represent this driver.

    RegistryPath - Unicode string identifying where the parameters for this
        driver are located in the registry.

Return Value:

    Routine can return non success error codes.

--*/
{
    NTSTATUS status;

	//
	//  Get debug trace flags
	//

	ReadDriverParameters(RegistryPath);

	if(ProtectedDirName.Buffer == NULL) return STATUS_UNSUCCESSFUL;

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("!DriverEntry: Entered\n") );

	//
	//  Init lookaside list used to allocate our context structure used to
	//  pass information from out preOperation callback to our postOperation
	//  callback.
	//
	ExInitializeNPagedLookasideList(&Pre2PostContextList, NULL, NULL, 0, sizeof(PRE_2_POST_CONTEXT), PRE_2_POST_TAG, 0);
	DbgPrint("Compile Date:%s\nCompile Time:%s\nEnter DriverEntry!\n", __DATE__, __TIME__);

    //
    //  Register with FltMgr to tell it our callback routines
    //

    status = FltRegisterFilter( DriverObject,
                                &FilterRegistration,
                                &gFilterHandle );

	NT_ASSERT(NT_SUCCESS(status));

    if (NT_SUCCESS( status )) {

	status = SwapDriverEntry(DriverObject, RegistryPath);
	status = SpyDriverEntry(DriverObject, RegistryPath);

        //
        //  Start filtering i/o
        //

        status = FltStartFiltering( gFilterHandle );

        if (!NT_SUCCESS( status )) {

            FltUnregisterFilter( gFilterHandle );
        }
    }

    return status;
}

NTSTATUS
Unload (
    __in FLT_FILTER_UNLOAD_FLAGS Flags
    )
/*++

Arguments:

    Flags - Indicating if this is a mandatory unload.

Return Value:

    Returns STATUS_SUCCESS.

--*/
{
	UNICODE_STRING valueName;
	OBJECT_ATTRIBUTES attributes;
	HANDLE driverRegKey;
	NTSTATUS status;	
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("!Unload: Entered\n") );

	SwapFilterUnload(Flags);
	SpyFilterUnload(Flags);

	//  Unregister from FLT mgr
	FltUnregisterFilter(gFilterHandle);


	//  Delete lookaside list
	ExDeleteNPagedLookasideList(&Pre2PostContextList);

	//Gobal values deletting.
	WriteDriverParameters();
	return STATUS_SUCCESS;
}

/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
PreOperation (
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __deref_out_opt PVOID *CompletionContext
    )
/*++

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    NTSTATUS status;
	PFLT_IO_PARAMETER_BLOCK iopb;
	UNICODE_STRING VolumName;
	FLT_PREOP_CALLBACK_STATUS retValue = FLT_PREOP_SUCCESS_NO_CALLBACK;	  //FLT_PREOP_SUCCESS_WITH_CALLBACK
	POBJECT_NAME_INFORMATION ObjectNameInf = NULL;
	iopb = Data->Iopb;

	if(!Data || !FltObjects || !FltObjects->FileObject)
	{
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}
    
	// if (KeGetCurrentIrql() == PASSIVE_LEVEL){ 
	// 	status = IoQueryFileDosDeviceName(iopb->TargetFileObject, &ObjectNameInf);
	// 	if (NT_SUCCESS(status)) {
	// 		RtlInitUnicodeString(&VolumName, ObjectNameInf->Name.Buffer);
	// 		VolumName.Length = ObjectNameInf->Name.Length;
	// 	}
	// 	ExFreePool(ObjectNameInf);
	// 	ObjectNameInf = NULL;
	// }


    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,	("!PreOperation: Entered\n") );
	//DbgPrint("\n MN=0x%08x IRP=0x%08x \n", iopb->MajorFunction, iopb->MinorFunction);

	if (IRP_MJ_CREATE == iopb->MajorFunction) {
		//retValue = PreCreate(Data, FltObjects, CompletionContext);
		retValue = FLT_PREOP_SUCCESS_NO_CALLBACK;
	}
	else if (IRP_MJ_READ == iopb->MajorFunction) {
		//retValue = PreReadBuffers(Data, FltObjects, CompletionContext);
		retValue = FLT_PREOP_SUCCESS_NO_CALLBACK;
	}
	else if(IRP_MJ_WRITE == iopb->MajorFunction) {
		retValue = PreWriteBuffers(Data, FltObjects, CompletionContext);
	}
	else if (IRP_MJ_SET_INFORMATION == iopb->MajorFunction) {
		retValue = PreSetInformation(Data, FltObjects, CompletionContext);
		//retValue = FLT_PREOP_SUCCESS_NO_CALLBACK;
	}
	else if(IRP_MJ_DIRECTORY_CONTROL == iopb->MajorFunction) {
		//retValue = PreDirCtrlBuffers(Data, FltObjects, CompletionContext);
		retValue = FLT_PREOP_SUCCESS_NO_CALLBACK;
	}
	

    //
    //  See if this is an operation we would like the operation status
    //  for.  If so request it.
    //
    //  NOTE: most filters do NOT need to do this.  You only need to make
    //        this call if, for example, you need to know if the oplock was
    //        actually granted.
    //

    if (DoRequestOperationStatus( Data )) {

        status = FltRequestOperationStatusCallback( Data,
                                                    OperationStatusCallback,
                                                    (PVOID)(++OperationStatusCtx) );
        if (!NT_SUCCESS(status)) {

            PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                          ("!PreOperation: FltRequestOperationStatusCallback Failed, status=%08x\n",
                           status) );
        }
		retValue = FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_WITH_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return retValue;
}



VOID
OperationStatusCallback (
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    __in NTSTATUS OperationStatus,
    __in PVOID RequesterContext
    )
/*++

Routine Description:

    This routine is called when the given operation returns from the call
    to IoCallDriver.  This is useful for operations where STATUS_PENDING
    means the operation was successfully queued.  This is useful for OpLocks
    and directory change notification operations.

    This callback is called in the context of the originating thread and will
    never be called at DPC level.  The file object has been correctly
    referenced so that you can access it.  It will be automatically
    dereferenced upon return.

    This is non-pageable because it could be called on the paging path

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    RequesterContext - The context for the completion routine for this
        operation.

    OperationStatus -

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("!OperationStatusCallback: Entered\n") );

    PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                  ("!OperationStatusCallback: Status=%08x ctx=%p IrpMj=%02x.%02x \"%s\"\n",
                   OperationStatus,
                   RequesterContext,
                   ParameterSnapshot->MajorFunction,
                   ParameterSnapshot->MinorFunction,
                   FltGetIrpName(ParameterSnapshot->MajorFunction)) );
}


FLT_POSTOP_CALLBACK_STATUS
PostOperation (
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in PVOID CompletionContext,
    __in FLT_POST_OPERATION_FLAGS Flags
    )
/*++

Routine Description:

    This routine is the post-operation completion routine for this
    miniFilter.

    This is non-pageable because it may be called at DPC level.

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The completion context set in the pre-operation routine.

    Flags - Denotes whether the completion is successful or is being drained.

Return Value:

    The return value is the status of the operation.

--*/
{
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;

	FLT_POSTOP_CALLBACK_STATUS retValue = FLT_POSTOP_FINISHED_PROCESSING;

	PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
		("!PostOperation: Entered\n"));

	DbgPrint("\n PostRead 0x%08x : 0x%08x", iopb->MinorFunction, iopb->IrpFlags);

	if (IRP_MJ_CREATE == iopb->MajorFunction) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	} else if (IRP_MJ_READ == iopb->MajorFunction) {
		//retValue = PostReadBuffers(Data, FltObjects, CompletionContext, Flags);
		return FLT_POSTOP_FINISHED_PROCESSING;
	} else if (IRP_MJ_WRITE == iopb->MajorFunction) {
		retValue = PostWriteBuffers(Data, FltObjects, CompletionContext, Flags);
	} else if (IRP_MJ_SET_INFORMATION == iopb->MajorFunction) {
		retValue = PostSetInformation(Data, FltObjects, CompletionContext, Flags);
		//return FLT_POSTOP_FINISHED_PROCESSING;
	} else if (IRP_MJ_DIRECTORY_CONTROL == iopb->MajorFunction) {
		//retValue = PostDirCtrlBuffers(Data, FltObjects, CompletionContext, Flags);
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	return retValue;
}


FLT_PREOP_CALLBACK_STATUS
PreOperationNoPostOperation (
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __deref_out_opt PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is a pre-operation dispatch routine for this miniFilter.

    This is non-pageable because it could be called on the paging path

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("!PreOperationNoPostOperation: Entered\n") );

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_NO_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}


BOOLEAN
DoRequestOperationStatus(
    __in PFLT_CALLBACK_DATA Data
    )
/*++

Routine Description:

    This identifies those operations we want the operation status for.  These
    are typically operations that return STATUS_PENDING as a normal completion
    status.

Arguments:

Return Value:

    TRUE - If we want the operation status
    FALSE - If we don't

--*/
{
    PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;

    //
    //  return boolean state based on which operations we are interested in
    //

    return (BOOLEAN)

            //
            //  Check for oplock operations
            //

             (((iopb->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL) &&
               ((iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_FILTER_OPLOCK)  ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_BATCH_OPLOCK)   ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_1) ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_2)))

              ||

              //
              //    Check for directy change notification
              //

              ((iopb->MajorFunction == IRP_MJ_DIRECTORY_CONTROL) &&
               (iopb->MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY))
             );
}


BOOLEAN
RtlFindSubString(__in PUNICODE_STRING String, __in PUNICODE_STRING SubString)
/*++

Routine Description:
This routine looks to see if SubString is a substring of String.

Arguments:
String - the string to search in
SubString - the substring to find in String

Return Value:
Returns TRUE if the substring is found in string and FALSE
otherwise.

--*/
{
	ULONG index;
	KSPIN_LOCK compareLock;
	//KIRQL oldIrql;
	BOOLEAN ret = FALSE;

    //KeAcquireSpinLock(&compareLock, &oldIrql);

	//
	// First, check to see if the strings are equal.
	//

	//if (RtlEqualUnicodeString(String, SubString, TRUE)) { ret = TRUE; goto __Finally; }

	//
	// String and SubString aren't equal, so now see if SubString
	// is in String any where.
	//
	//if(TRUE == FsRtlIsNameInExpression(&SubString, &String, FALSE, NULL))

	for (index = 0; index + SubString->Length/2 <= String->Length/2; index++) {

		if (_wcsnicmp(&String->Buffer[index], SubString->Buffer, SubString->Length/2) == 0) {

			//
			// SubString is found in String, so return TRUE.
			//
			ret = TRUE;
			break;
		}
	}

__Finally:

   // KeReleaseSpinLock(&compareLock, oldIrql);

	return ret;
} 


BOOLEAN IsProtectionFileByProtectedFilExt(PFLT_FILE_NAME_INFORMATION NameInfos)
{
	BOOLEAN bProtect = FALSE;

	// 判断
	if (TRUE == RtlFindSubString(&NameInfos->Name, &ProtectedFilExt))
	{
		bProtect = TRUE;
	}

	return bProtect;
}

BOOLEAN IsProtectionFileByProtectedDirName(PFLT_FILE_NAME_INFORMATION NameInfos)
{
	BOOLEAN bProtect = FALSE;

	// 判断
	if (TRUE == RtlFindSubString(&NameInfos->Name, &ProtectedDirName))
	{
		bProtect = TRUE;
	}

	return bProtect;
}

BOOLEAN IsProtectionFileByProtectedDirName1(PFLT_FILE_NAME_INFORMATION NameInfos)
{
	BOOLEAN bProtect = FALSE;

	UNICODE_STRING tmp;
	RtlInitUnicodeString(&tmp, L"\\doc\\");
	// 判断
	if (TRUE == RtlFindSubString(&NameInfos->Name, &tmp))
	{
		bProtect = TRUE;
	}

	return bProtect;
}


BOOLEAN IsProtectionFile(PFLT_FILE_NAME_INFORMATION NameInfos)
{
	BOOLEAN bProtect = FALSE;

	// bProtect = IsProtectionFileByFileNameExtion(NameInfos);     //按文件扩展名方式保护。

	bProtect = IsProtectionFileByProtectedDirName(NameInfos);

	return bProtect;
}


FLT_PREOP_CALLBACK_STATUS
PreReadBuffers(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__deref_out_opt PVOID *CompletionContext
)
/*++

Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - Receives the context that will be passed to the
post-operation callback.

Return Value:

FLT_PREOP_SUCCESS_WITH_CALLBACK - we want a postOpeation callback
FLT_PREOP_SUCCESS_NO_CALLBACK - we don't want a postOperation callback

--*/
{
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	NTSTATUS status;
	PFLT_FILE_NAME_INFORMATION FileNameInformation = NULL; 

	return FLT_PREOP_SUCCESS_NO_CALLBACK;

	if (iopb->IrpFlags & IRP_PAGING_IO) DbgPrint("\n PreRead IRP : 0x%08x ops IRP_PAGING_IO", iopb->IrpFlags);
	else { DbgPrint("\n NOT IRP_PAGING_IO"); return FLT_PREOP_SUCCESS_NO_CALLBACK; }		

	//if (IsProtectedDir(Data) == FALSE) return FLT_PREOP_SUCCESS_NO_CALLBACK;
	status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &FileNameInformation);
	if (NT_SUCCESS(status)) {
		status = FltParseFileNameInformation(FileNameInformation);
		if (NT_SUCCESS(status)) {
			if (TRUE == IsProtectionFile(FileNameInformation))
			{
				Data->IoStatus.Status = STATUS_ACCESS_DENIED;
				Data->IoStatus.Information = 0;
				FltReleaseFileNameInformation(FileNameInformation);
				return FLT_PREOP_COMPLETE;
			}
		}
		FltReleaseFileNameInformation(FileNameInformation);
	}

	if (FLT_IS_FASTIO_OPERATION(Data))  
	{
		DbgPrint("\n FAST IO OPERATION");
		return FLT_PREOP_DISALLOW_FASTIO;
	}

	//return SwapPreReadBuffers(Data, FltObjects, CompletionContext);
	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS
PreDirCtrlBuffers(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__deref_out_opt PVOID *CompletionContext
)
/*++
Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - Receives the context that will be passed to the
post-operation callback.

Return Value:

FLT_PREOP_SUCCESS_WITH_CALLBACK - we want a postOpeation callback
FLT_PREOP_SUCCESS_NO_CALLBACK - we don't want a postOperation callback

--*/
{
		//
		//  We only need to build a MDL for IRP operations.  We don't need to
		//  do this for a FASTIO operation because it is a waste of time since
		//  the FASTIO interface has no parameter for passing the MDL to the
		//  file system.
		//

	if (FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_IRP_OPERATION)) {	
		return SwapPreDirCtrlBuffers(Data,FltObjects,CompletionContext);
	}
	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS
PreWriteBuffers(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__deref_out_opt PVOID *CompletionContext
)
/*++

Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - Receives the context that will be passed to the
post-operation callback.

Return Value:

FLT_PREOP_SUCCESS_WITH_CALLBACK - we want a postOpeation callback
FLT_PREOP_SUCCESS_NO_CALLBACK - we don't want a postOperation callback
FLT_PREOP_COMPLETE -
--*/
{
	PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;
	NTSTATUS status;
	PFLT_FILE_NAME_INFORMATION FileNameInformation = NULL;

	// if (iopb->IrpFlags & IRP_PAGING_IO) DbgPrint("\n PreRead IRP : 0x%08x ops IRP_PAGING_IO", iopb->IrpFlags);
	// else { DbgPrint("\n NOT IRP_PAGING_IO"); return FLT_PREOP_SUCCESS_NO_CALLBACK; }	

	//if (IsProtectedDir(Data) == FALSE) return FLT_PREOP_SUCCESS_NO_CALLBACK;

	status = FltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT, &FileNameInformation);
	if (NT_SUCCESS(status)) {
		status = FltParseFileNameInformation(FileNameInformation);
		if (NT_SUCCESS(status)) {
			if (TRUE == IsProtectionFile(FileNameInformation))
			{
				Data->IoStatus.Status = STATUS_ACCESS_DENIED;
				Data->IoStatus.Information = 0;
				FltReleaseFileNameInformation(FileNameInformation);
				return FLT_PREOP_COMPLETE;
			}
		}
		FltReleaseFileNameInformation(FileNameInformation);
	}

	// DbgPrint("\n PostWrite 0x%08x : 0x%08x", iopb->MinorFunction, iopb->IrpFlags);
	// if (iopb->IrpFlags & IRP_PAGING_IO) DbgPrint("\n PreWrite IRP : 0x%08x ops IRP_PAGING_IO", iopb->IrpFlags);

	//return SwapPreWriteBuffers(Data, FltObjects, CompletionContext);
	return SpyPreOperationCallback(Data, FltObjects, CompletionContext);
	//return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

VOID
CleanupVolumeContext(
	__in PFLT_CONTEXT Context,
	__in FLT_CONTEXT_TYPE ContextType
)
/*++

Routine Description:

The given context is being freed.
Free the allocated name buffer if there one.

Arguments:

Context - The context being freed

ContextType - The type of context this is

Return Value:

None

--*/
{
	PAGED_CODE();
	SwapCleanupVolumeContext(Context, ContextType);
	return;
}


FLT_POSTOP_CALLBACK_STATUS
PostReadBuffers(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__in PVOID CompletionContext,
	__in FLT_POST_OPERATION_FLAGS Flags
)
/*++

Arguments:

Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - The completion context set in the pre-operation routine.

Flags - Denotes whether the completion is successful or is being drained.

Return Value:

FLT_POSTOP_FINISHED_PROCESSING
FLT_POSTOP_MORE_PROCESSING_REQUIRED

--*/
{
	NTSTATUS status = FLT_POSTOP_FINISHED_PROCESSING;
	//status = SwapPostReadBuffers(Data,FltObjects,CompletionContext,Flags);
	//status = SpyPostOperationCallback(Data, FltObjects, CompletionContext, Flags);
	return status;
}


FLT_POSTOP_CALLBACK_STATUS
PostWriteBuffers(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__in PVOID CompletionContext,
	__in FLT_POST_OPERATION_FLAGS Flags
)
/*++

Routine Description:


Arguments:


Return Value:

--*/
{
	NTSTATUS status = FLT_POSTOP_FINISHED_PROCESSING;
	//status = SwapPostReadBuffers(Data, FltObjects, CompletionContext,Flags);
	//if(TRUE == IsProtectionFile(Data)) 
	status = SpyPostOperationCallback(Data, FltObjects, CompletionContext, Flags);
	return status;
}

FLT_POSTOP_CALLBACK_STATUS
PostSetInformation(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__in PVOID CompletionContext,
	__in FLT_POST_OPERATION_FLAGS Flags	
)
/*++

Routine Description:


Arguments:


Return Value:

--*/
{
	NTSTATUS status = FLT_POSTOP_FINISHED_PROCESSING;
	//status = SwapPostReadBuffers(Data, FltObjects, CompletionContext,Flags);
	//if(TRUE == IsProtectionFile(Data)) 
	status = SpyPostOperationCallback(Data, FltObjects, CompletionContext, Flags);
	return status;	
}

FLT_POSTOP_CALLBACK_STATUS
PostDirCtrlBuffers(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__in PVOID CompletionContext,
	__in FLT_POST_OPERATION_FLAGS Flags
)
/*++

Arguments:

This routine does postRead buffer swap handling
Data - Pointer to the filter callbackData that is passed to us.

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance, its associated volume and
file object.

CompletionContext - The completion context set in the pre-operation routine.

Flags - Denotes whether the completion is successful or is being drained.

Return Value:

FLT_POSTOP_FINISHED_PROCESSING
FLT_POSTOP_MORE_PROCESSING_REQUIRED

--*/
{
	NTSTATUS status;
	status = SwapPostDirCtrlBuffers(Data, FltObjects, CompletionContext, Flags);
	// if(TRUE == IsProtectionFile(Data))
	// 	status = SpyPostOperationCallback(Data, FltObjects, CompletionContext, Flags);
	return status;
}


NTSTATUS JudgeFileExist(PUNICODE_STRING FileName)						//判断文件是否存在
{
	NTSTATUS status;
	HANDLE FileHandle;													//如果遇到Open_IF就先去判断是否存在，如果不存在就记下这个目录。
	IO_STATUS_BLOCK IoBlock;
	OBJECT_ATTRIBUTES ObjectAttributes;
	InitializeObjectAttributes(&ObjectAttributes, FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
	status = ZwCreateFile(&FileHandle, GENERIC_ALL, &ObjectAttributes, &IoBlock, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
	if (NT_SUCCESS(status))
		ZwClose(FileHandle);
	return status;
}

FLT_PREOP_CALLBACK_STATUS
PreCreate(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__deref_out_opt PVOID *CompletionContext
) {
	NTSTATUS status;
	ULONG CreatePosition;
	ULONG Position;
	PFLT_FILE_NAME_INFORMATION NameInfo;

	UNREFERENCED_PARAMETER(CompletionContext);
	UNREFERENCED_PARAMETER(FltObjects);

	Position = Data->Iopb->Parameters.Create.Options;
	//经过反复检验发现，Create.Options的分布是这样子的，第一字节是create disposition values，后面三个字节是option flags
	CreatePosition = (Data->Iopb->Parameters.Create.Options >> 24) & 0xFF;	

	if (Position & FILE_DIRECTORY_FILE)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;								//如果发现是文件夹选项直接返回

	if (CreatePosition == FILE_OPEN)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;								//如果是FILE_OPEN打开文件，直接返回

	status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &NameInfo);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Query Name Fail!\n"));
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	status = FltParseFileNameInformation(NameInfo);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Parse Name Fail!\n"));
		FltReleaseFileNameInformation(NameInfo);
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	// if (FALSE == IsProtectionFile(NameInfo))                                   //只允许保护类文件创建
	// {
	// 	Data->IoStatus.Status = STATUS_ACCESS_DENIED;
	// 	Data->IoStatus.Information = 0;
	// 	FltReleaseFileNameInformation(NameInfo);
	// 	return FLT_PREOP_COMPLETE;
	// }

	//如果这里出现了****IF代表如果存在则****，否则创建，所以需要先判断是否存在，如果存在则不在过滤范围，否则就是过滤范围内了。
	if (CreatePosition == FILE_OPEN_IF || CreatePosition == FILE_OVERWRITE_IF)						
	{
		//KdPrint(("FILE_OPEN_IF OR FILE_OVERWRITE_IF\n"));
		if (NT_SUCCESS(JudgeFileExist(&NameInfo->Name)))
		{
			//KdPrint(("文件已经存在！\n"));
			FltReleaseFileNameInformation(NameInfo);
			return FLT_PREOP_SUCCESS_NO_CALLBACK;
		}
	}

	FltReleaseFileNameInformation(NameInfo);

	status = SpyPreOperationCallback(Data, FltObjects, CompletionContext);

	return status;
}


FLT_PREOP_CALLBACK_STATUS PreReNameFile(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, __deref_out_opt PVOID *CompletionContext)
{
	NTSTATUS status;
	PFILE_RENAME_INFORMATION pReNameInfo;
	PFLT_FILE_NAME_INFORMATION NameInfo;

	pReNameInfo = (PFILE_RENAME_INFORMATION)Data->Iopb->Parameters.SetFileInformation.InfoBuffer;

	status = FltGetDestinationFileNameInformation(FltObjects->Instance,
		Data->Iopb->TargetFileObject,
		pReNameInfo->RootDirectory,
		pReNameInfo->FileName,
		pReNameInfo->FileNameLength,
		FLT_FILE_NAME_NORMALIZED,
		&NameInfo);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Get Destination Name Fail!\n"));
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (TRUE == IsProtectionFile(NameInfo))																//禁止出现对应名称的文件Rename。								
	{
		Data->IoStatus.Status = STATUS_ACCESS_DENIED;
		Data->IoStatus.Information = 0;
		FltReleaseFileNameInformation(NameInfo);
		return FLT_PREOP_COMPLETE;
	}

	//if(TRUE == IsProtectionFileByProtectedDirName1(NameInfo))
	//	return SpyPreOperationCallback(Data, FltObjects, CompletionContext);

	FltReleaseFileNameInformation(NameInfo);
	return SpyPreOperationCallback(Data, FltObjects, CompletionContext);
	//return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
	  
FLT_PREOP_CALLBACK_STATUS PreDeleteFile(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, __deref_out_opt PVOID *CompletionContext)
{
	NTSTATUS status;
	BOOLEAN isDir;
	PFLT_FILE_NAME_INFORMATION NameInfo;

	status = FltIsDirectory(FltObjects->FileObject, FltObjects->Instance, &isDir);
	if (!NT_SUCCESS(status))
	{
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (isDir)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;					//这里代表如果是文件夹，就不去管它。

	status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED
		| FLT_FILE_NAME_QUERY_DEFAULT, &NameInfo);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Query Name Fail!\n"));
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}


	FltParseFileNameInformation(NameInfo);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Parse Name Fail!\n"));
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (TRUE == IsProtectionFile(NameInfo))																//禁止出现对应名称的文件删除。								
	{
		Data->IoStatus.Status = STATUS_ACCESS_DENIED;
		Data->IoStatus.Information = 0;
		FltReleaseFileNameInformation(NameInfo);
		return FLT_PREOP_COMPLETE;
	}	

	//if(TRUE == IsProtectionFileByProtectedDirName1(NameInfo))
	//	return SpyPreOperationCallback(Data, FltObjects, CompletionContext);

	FltReleaseFileNameInformation(NameInfo);
	return SpyPreOperationCallback(Data, FltObjects, CompletionContext);
	//return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS
PreSetInformation(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__deref_out_opt PVOID *CompletionContext
)
{
	UNREFERENCED_PARAMETER(CompletionContext);

	if (Data->Iopb->Parameters.SetFileInformation.FileInformationClass == FileRenameInformation)						//重命名操作
		return PreReNameFile(Data, FltObjects, CompletionContext);

	else if (Data->Iopb->Parameters.SetFileInformation.FileInformationClass == FileDispositionInformation)				//删除操作
		return PreDeleteFile(Data, FltObjects, CompletionContext);

	else
		return FLT_PREOP_SUCCESS_NO_CALLBACK;																			//其他操作不管，直接返回SUCCESS
}


BOOLEAN IsProtectedDir(PFLT_CALLBACK_DATA Data)
{
	PFLT_FILE_NAME_INFORMATION FileNameInformation = NULL;

	NTSTATUS status;

	status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED, &FileNameInformation);

	if (NT_SUCCESS(status))
	{
		status = FltParseFileNameInformation(FileNameInformation);

		if (NT_SUCCESS(status))
		{
			if ((NULL == FileNameInformation->ParentDir.Buffer) || (0 == FileNameInformation->ParentDir.Length)) 
			{
				return FALSE;
			}

			//KdPrint(("Parent Dir is %S\n", FileNameInformation->ParentDir.Buffer));
			
			if (RtlCompareUnicodeString(&FileNameInformation->ParentDir, &ProtectedDirName, FALSE) == 0)
			{
				//DbgPrint("\n Compare");
				FltReleaseFileNameInformation(FileNameInformation);
				return TRUE;
			}
			if (RtlCompareMemory(FileNameInformation->ParentDir.Buffer, ProtectedDirName.Buffer, sizeof(WCHAR) * 11) == 0)
			{
				KdPrint((" !!!!! sTARt with test \n"));
				FltReleaseFileNameInformation(FileNameInformation);
				return TRUE;
			}

			FltReleaseFileNameInformation(FileNameInformation);
		}
		else
		{
			KdPrint(("!IsProtectedDir : Error FltParseFileNameInformation "));
		}
	}
	else if (status == STATUS_FLT_INVALID_NAME_REQUEST)
	{
		KdPrint(("!IsProtectedDir : Error STATUS_FLT_INVALID_NAME_REQUEST return by FltGetFileNameInformation "));
	}
	else if (status == STATUS_INSUFFICIENT_RESOURCES)
	{
		KdPrint(("!IsProtectedDir : Error STATUS_INSUFFICIENT_RESOURCES return by FltGetFileNameInformation "));
	}
	else if (status == STATUS_INVALID_PARAMETER)
	{
		KdPrint(("!IsProtectedDir : Error STATUS_INVALID_PARAMETER return by FltGetFileNameInformation "));
	}
	return FALSE;
}

PUNICODE_STRING GetProttectinFolder()
{
	return &ProtectedDirName;
}

VOID SetProtectionFolder(PUNICODE_STRING dir)
{
	RtlCopyUnicodeString(&ProtectedDirName, dir);
	KdPrint(("!ProtectedDirName is setting to : %wZ", ProtectedDirName.Buffer));
	return;
}