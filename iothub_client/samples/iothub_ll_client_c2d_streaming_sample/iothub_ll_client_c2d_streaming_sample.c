// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// CAVEAT: This sample is to demonstrate azure IoT client concepts only and is not a guide design principles or style
// Checking of return codes and error values shall be omitted for brevity.  Please practice sound engineering practices 
// when writing production code.

#include <stdio.h>
#include <stdlib.h>

#include "iothub.h"
#include "iothub_device_client_ll.h"
#include "iothub_client_options.h"
#include "iothub_message.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/shared_util_options.h"
#include "azure_c_shared_utility/ws_url.h"
#include "azure_c_shared_utility/urlencode.h"
#include "azure_c_shared_utility/uws_client.h"

#ifdef SET_TRUSTED_CERT_IN_SAMPLES
#include "certs.h"
#endif // SET_TRUSTED_CERT_IN_SAMPLES

/* This sample uses the _LL APIs of iothub_client for example purposes.
Simply changing the using the convenience layer (functions not having _LL)
and removing calls to _DoWork will yield the same results. */

// The protocol you wish to use should be uncommented
//
#define SAMPLE_MQTT
//#define SAMPLE_MQTT_OVER_WEBSOCKETS
//#define SAMPLE_AMQP
//#define SAMPLE_AMQP_OVER_WEBSOCKETS
//#define SAMPLE_HTTP

#ifdef SAMPLE_MQTT
    #include "iothubtransportmqtt.h"
#endif // SAMPLE_MQTT
#ifdef SAMPLE_MQTT_OVER_WEBSOCKETS
    #include "iothubtransportmqtt_websockets.h"
#endif // SAMPLE_MQTT_OVER_WEBSOCKETS
#ifdef SAMPLE_AMQP
    #include "iothubtransportamqp.h"
#endif // SAMPLE_AMQP
#ifdef SAMPLE_AMQP_OVER_WEBSOCKETS
    #include "iothubtransportamqp_websockets.h"
#endif // SAMPLE_AMQP_OVER_WEBSOCKETS
#ifdef SAMPLE_HTTP
    #include "iothubtransporthttp.h"
#endif // SAMPLE_HTTP

#ifdef SET_TRUSTED_CERT_IN_SAMPLES
#include "certs.h"
#endif // SET_TRUSTED_CERT_IN_SAMPLES

MU_DEFINE_ENUM_STRINGS(WS_OPEN_RESULT, WS_OPEN_RESULT_VALUES)
MU_DEFINE_ENUM_STRINGS(WS_ERROR, WS_ERROR_VALUES)
MU_DEFINE_ENUM_STRINGS(WS_SEND_FRAME_RESULT, WS_SEND_FRAME_RESULT_VALUES)


/* Paste in the your iothub connection string  */
static const char* connectionString = "[device connection string]";
static bool g_continueRunning = true;
static UWS_CLIENT_HANDLE g_uws_client_handle = NULL;
static char stream_payload[128];


static void on_ws_open_complete(void* context, WS_OPEN_RESULT ws_open_result)
{
    (void)context;
    (void)printf("Client connected to the streaming gateway (%s)\r\n", MU_ENUM_TO_STRING(WS_OPEN_RESULT, ws_open_result));
}

// 
static void on_ws_send_frame_complete(void* context, WS_SEND_FRAME_RESULT ws_send_frame_result)
{
    (void)context;

    if (ws_send_frame_result == WS_SEND_FRAME_OK)
    {
        (void)printf("Sent stream data: %s\r\n", stream_payload);
    }
    else
    {
        (void)printf("Failed sending stream data (%s)\r\n", MU_ENUM_TO_STRING(WS_SEND_FRAME_RESULT, ws_send_frame_result));
    }

    g_continueRunning = false;
}

static void on_ws_frame_received(void* context, unsigned char frame_type, const unsigned char* buffer, size_t size)
{
    (void)context;
    (void)size;

    (void)memcpy(stream_payload, buffer, size);
    stream_payload[size] = '\0';

    (void)printf("Received stream data: %s\r\n", stream_payload);

    (void)uws_client_send_frame_async(g_uws_client_handle, frame_type, buffer, size, true, on_ws_send_frame_complete, NULL);
}

static void on_ws_peer_closed(void* context, uint16_t* close_code, const unsigned char* extra_data, size_t extra_data_length)
{
    (void)context;
    (void)extra_data_length;
    (void)printf("on_ws_peer_closed\r\nCode: %d\r\nData: %s\r\n", *(int*)close_code, extra_data);
}

static void on_ws_error(void* context, WS_ERROR error_code)
{
    (void)context;
    (void)printf("on_ws_error (%s)\r\n", MU_ENUM_TO_STRING(WS_ERROR, error_code));
}

static UWS_CLIENT_HANDLE create_websocket_client(const DEVICE_STREAM_C2D_REQUEST* stream_request)
{
    UWS_CLIENT_HANDLE result;

    WS_URL_HANDLE ws_url;
    WS_PROTOCOL protocols;
    char auth_header_value[1024];

    const char* host;
    size_t host_length;
    const char* path;
    size_t path_length;
    size_t port;

    char host_address[1024];
    char resource_name[1024];

    // Parsing the information
    ws_url = ws_url_create(stream_request->url);
    (void)ws_url_get_host(ws_url, &host, &host_length);
    (void)ws_url_get_path(ws_url, &path, &path_length);
    (void)ws_url_get_port(ws_url, &port);

    (void)memcpy(host_address, host, host_length);
    host_address[host_length] = '\0';

    (void)memcpy(resource_name + 1, path, path_length);
    resource_name[0] = '/';
    resource_name[path_length + 1] = '\0';

    // Establishing the connection to the streaming gateway
#if defined SAMPLE_AMQP || defined SAMPLE_AMQP_OVER_WEBSOCKETS
    protocols.protocol = "AMQP";
#elif defined SAMPLE_HTTP
    protocols.protocol = "HTTP";
#else
    protocols.protocol = "MQTT";
#endif

    (void)sprintf(auth_header_value, "Bearer %s", stream_request->authorization_token);

    result = uws_client_create(host_address, (int)port, resource_name, true, &protocols, 1);
    (void)uws_client_set_request_header(result, "Authorization", auth_header_value);
    (void)uws_client_open_async(result, on_ws_open_complete, NULL, on_ws_frame_received, NULL, on_ws_peer_closed, NULL, on_ws_error, NULL);

    ws_url_destroy(ws_url);

    return result;
}

static DEVICE_STREAM_C2D_RESPONSE* streamRequestCallback(const DEVICE_STREAM_C2D_REQUEST* stream_request, void* context)
{
    (void)context;

    (void)printf("Received stream request (%s)\r\n", stream_request->name);

    g_uws_client_handle = create_websocket_client(stream_request);

    return IoTHubClient_StreamC2DResponseCreate(stream_request, true);
}

int main(void)
{
    IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol;

    // Select the Protocol to use with the connection
#ifdef SAMPLE_MQTT
    protocol = MQTT_Protocol;
#endif // SAMPLE_MQTT
#ifdef SAMPLE_MQTT_OVER_WEBSOCKETS
    protocol = MQTT_WebSocket_Protocol;
#endif // SAMPLE_MQTT_OVER_WEBSOCKETS
#ifdef SAMPLE_AMQP
    protocol = AMQP_Protocol;
#endif // SAMPLE_AMQP
#ifdef SAMPLE_AMQP_OVER_WEBSOCKETS
    protocol = AMQP_Protocol_over_WebSocketsTls;
#endif // SAMPLE_AMQP_OVER_WEBSOCKETS
#ifdef SAMPLE_HTTP
    protocol = HTTP_Protocol;
#endif // SAMPLE_HTTP

    // Used to initialize IoTHub SDK subsystem
    (void)IoTHub_Init();

    IOTHUB_DEVICE_CLIENT_LL_HANDLE device_ll_handle;

    // Create the iothub handle here
    device_ll_handle = IoTHubDeviceClient_LL_CreateFromConnectionString(connectionString, protocol);

    if (device_ll_handle == NULL)
    {
        (void)printf("Failure creating the IotHub device. Hint: Check you connection string.\r\n");
    }
    else
    {
        // Set any option that are neccessary.
        // For available options please see the iothub_sdk_options.md documentation

        bool traceOn = true;
        IoTHubDeviceClient_LL_SetOption(device_ll_handle, OPTION_LOG_TRACE, &traceOn);

#ifdef SET_TRUSTED_CERT_IN_SAMPLES
        // Setting the Trusted Certificate.  This is only necessary on system with without
        // built in certificate stores.
            IoTHubDeviceClient_LL_SetOption(device_ll_handle, OPTION_TRUSTED_CERT, certificates);
#endif // SET_TRUSTED_CERT_IN_SAMPLES

#if defined SAMPLE_MQTT || defined SAMPLE_MQTT_WS
        //Setting the auto URL Encoder (recommended for MQTT). Please use this option unless
        //you are URL Encoding inputs yourself.
        //ONLY valid for use with MQTT
        //bool urlEncodeOn = true;
        //IoTHubDeviceClient_LL_SetOption(iothub_ll_handle, OPTION_AUTO_URL_ENCODE_DECODE, &urlEncodeOn);
#endif

        if (IoTHubDeviceClient_LL_SetStreamRequestCallback(device_ll_handle, streamRequestCallback, NULL) != IOTHUB_CLIENT_OK)
        {
            printf("Failed setting the stream request callback");
        }
        else
        {
            do
            {
                IoTHubDeviceClient_LL_DoWork(device_ll_handle);

                if (g_uws_client_handle != NULL)
                {
                    uws_client_dowork(g_uws_client_handle);
                }

                ThreadAPI_Sleep(100);

            } while (g_continueRunning);

        }
 
        // Clean up the iothub sdk handle
        IoTHubDeviceClient_LL_Destroy(device_ll_handle);

        if (g_uws_client_handle != NULL)
        {
            uws_client_destroy(g_uws_client_handle);
        }
    }

    // Free all the sdk subsystem
    IoTHub_Deinit();

    (void)printf("Press any key to continue");
    getchar();

    return 0;
}
