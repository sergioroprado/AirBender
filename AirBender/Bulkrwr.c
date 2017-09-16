#include "Driver.h"
#include "L2CAP.h"

#include "bulkrwr.tmh"

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
AirBenderConfigContReaderForBulkReadEndPoint(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    WDF_USB_CONTINUOUS_READER_CONFIG contReaderConfig;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "%!FUNC! Entry");

    WDF_USB_CONTINUOUS_READER_CONFIG_INIT(&contReaderConfig,
        AirBenderEvtUsbBulkReadPipeReadComplete,
        DeviceContext,    // Context
        BULK_IN_BUFFER_LENGTH);   // TransferLength

    contReaderConfig.EvtUsbTargetPipeReadersFailed = AirBenderEvtUsbBulkReadReadersFailed;

    //
    // Reader requests are not posted to the target automatically.
    // Driver must explictly call WdfIoTargetStart to kick start the
    // reader.  In this sample, it's done in D0Entry.
    // By defaut, framework queues two requests to the target
    // endpoint. Driver can configure up to 10 requests with CONFIG macro.
    //
    status = WdfUsbTargetPipeConfigContinuousReader(DeviceContext->BulkReadPipe,
        &contReaderConfig);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_BULKRWR,
            "OsrFxConfigContReaderForInterruptEndPoint failed %x\n",
            status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "%!FUNC! Exit");

    return status;
}

NTSTATUS WriteBulkPipe(
    PDEVICE_CONTEXT Context,
    PVOID Buffer,
    ULONG BufferLength,
    PULONG BytesWritten)
{
    NTSTATUS                        status;
    WDF_MEMORY_DESCRIPTOR           memDesc;

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
        &memDesc,
        Buffer,
        BufferLength
    );

    status = WdfUsbTargetPipeWriteSynchronously(
        Context->BulkWritePipe,
        NULL,
        NULL,
        &memDesc,
        BytesWritten
    );

    return status;
}

VOID
AirBenderEvtUsbBulkReadPipeReadComplete(
    WDFUSBPIPE  Pipe,
    WDFMEMORY   Buffer,
    size_t      NumBytesTransferred,
    WDFCONTEXT  Context
)
{
    NTSTATUS                        status;
    PDEVICE_CONTEXT                 pDeviceContext = Context;
    PUCHAR                          buffer;
    BTH_HANDLE                      clientHandle;
    PBTH_DEVICE                     pClientDevice;
    L2CAP_CID                       dcid = { 0 };
    L2CAP_CID                       scid = { 0 };
    L2CAP_SIGNALLING_COMMAND_CODE   code;
    L2CAP_PSM                       psm;
    BYTE                            channelId;

    UNREFERENCED_PARAMETER(status);
    UNREFERENCED_PARAMETER(Pipe);
    UNREFERENCED_PARAMETER(pDeviceContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "%!FUNC! Entry");

    if (NumBytesTransferred == 0) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_BULKRWR,
            "!FUNC! Zero length read "
            "occurred on the Interrupt Pipe's Continuous Reader\n"
        );
        return;
    }

    buffer = WdfMemoryGetBuffer(Buffer, NULL);

    BTH_HANDLE_FROM_BUFFER(clientHandle, buffer);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPT, "LSB/MSB: %02X %02X", clientHandle.Lsb, clientHandle.Msb);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR,
        "BULK Devices: %d",
        BTH_DEVICE_LIST_GET_COUNT(&pDeviceContext->ClientDeviceList));

    pClientDevice = BTH_DEVICE_LIST_GET_BY_HANDLE(&pDeviceContext->ClientDeviceList, &clientHandle);

    if (pClientDevice != NULL)
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "Device found");

    if (L2CAP_IS_CONTROL_CHANNEL(buffer))
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "L2CAP_IS_CONTROL_CHANNEL");

        if (L2CAP_IS_SIGNALLING_COMMAND_CODE(buffer))
        {
            code = L2CAP_GET_SIGNALLING_COMMAND_CODE(buffer);

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "L2CAP_IS_SIGNALLING_COMMAND_CODE: 0x%02X", (BYTE)code);

            switch (code)
            {
            case L2CAP_Connection_Request:

                L2CAP_GET_SOURCE_CHANNEL_ID(code, buffer, &scid);
                psm = L2CAP_GET_PROTOCOL_SERVICE_MULTIPLEXER(buffer);
                L2CAP_SET_CONNECTION_TYPE(pDeviceContext,
                    pClientDevice,
                    psm,
                    scid,
                    &dcid,
                    0);
                channelId = L2CAP_GET_CHANNEL_ID(buffer);

                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "L2CAP_Connection_Request PSM: %02X SCID: %02X %02X DCID: %02X %02X",
                    psm, scid.Lsb, scid.Msb, dcid.Lsb, dcid.Msb);

                status = L2CAP_Command_Connection_Response(
                    pDeviceContext,
                    pClientDevice->HCI_ConnectionHandle,
                    channelId,
                    dcid,
                    scid,
                    L2CAP_ConnectionResponseResult_ConnectionPending,
                    L2CAP_ConnectionResponseStatus_AuthorisationPending);

                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_BULKRWR, "L2CAP_Command_Connection_Response (PENDING) failed");
                }

                status = L2CAP_Command_Connection_Response(
                    pDeviceContext,
                    pClientDevice->HCI_ConnectionHandle,
                    channelId,
                    dcid,
                    scid,
                    L2CAP_ConnectionResponseResult_ConnectionSuccessful,
                    L2CAP_ConnectionResponseStatus_NoFurtherInformationAvailable);

                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_BULKRWR, "L2CAP_Command_Connection_Response (SUCCESSFUL) failed");
                }

                break;
            case L2CAP_Connection_Response:

                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "L2CAP_Connection_Response");

                break;
            case L2CAP_Configuration_Request:

                L2CAP_GET_SOURCE_CHANNEL_ID(code, buffer, &scid);

                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "L2CAP_Configuration_Request SCID: %02X %02X",
                    scid.Lsb, scid.Msb);

                L2CAP_DEVICE_GET_SCID(pClientDevice, dcid, &scid);

                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "L2CAP_Configuration_Request SCID: %02X %02X DCID: %02X %02X",
                    scid.Lsb, scid.Msb, dcid.Lsb, dcid.Msb);

                break;
            default:
                break;
            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "%!FUNC! Exit");
}

BOOLEAN
AirBenderEvtUsbBulkReadReadersFailed(
    _In_ WDFUSBPIPE Pipe,
    _In_ NTSTATUS Status,
    _In_ USBD_STATUS UsbdStatus
)
{
    WDFDEVICE device = WdfIoTargetGetDevice(WdfUsbTargetPipeGetIoTarget(Pipe));
    PDEVICE_CONTEXT pDeviceContext = DeviceGetContext(device);

    UNREFERENCED_PARAMETER(pDeviceContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_ERROR, TRACE_BULKRWR, "Status: 0x%X, USBD_STATUS: 0x%X", Status, UsbdStatus);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BULKRWR, "%!FUNC! Exit");

    return TRUE;
}
