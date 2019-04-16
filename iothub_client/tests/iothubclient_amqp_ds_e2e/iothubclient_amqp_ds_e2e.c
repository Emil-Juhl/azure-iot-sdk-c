// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "testrunnerswitcher.h"
#include "iothubclient_common_ds_e2e.h"
#include "iothubtransportamqp.h"

BEGIN_TEST_SUITE(iothubclient_amqp_ds_e2e)

    TEST_SUITE_INITIALIZE(TestClassInitialize)
    {
        ds_e2e_init(false);
    }

    TEST_SUITE_CLEANUP(TestClassCleanup)
    {
        ds_e2e_deinit();
    }

#ifndef USE_WOLFSSL
    TEST_FUNCTION(IoTHub_Accept_Device_Streaming_Request_AMQP_e2e)
    {
        ds_e2e_receive_device_streaming_request(AMQP_Protocol, true);
    }
#endif

    TEST_FUNCTION(IoTHub_Reject_Device_Streaming_Request_AMQP_e2e)
    {
        ds_e2e_receive_device_streaming_request(AMQP_Protocol, false);
    }

END_TEST_SUITE(iothubclient_amqp_ds_e2e)
