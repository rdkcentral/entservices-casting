/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2022 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/

#include <gtest/gtest.h>

#include "XCast.h"

#include "FactoriesImplementation.h"
#include "ServiceMock.h"
#include "ThunderPortability.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include "COMLinkMock.h"
#include "WrapsMock.h"
#include "RfcApiMock.h"
#include "gdialserviceMock.h"
#include "WorkerPoolImplementation.h"
#include "XCastImplementation.h"
#include <sys/time.h>
#include <future>
#include <thread>

using namespace WPEFramework;

using ::testing::NiceMock;
namespace 
{
    #define TEST_LOG(FMT, ...) log(__func__, __FILE__, __LINE__, syscall(__NR_gettid),FMT,##__VA_ARGS__)

    void current_time(char *time_str)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        long microseconds = tv.tv_usec;

        // Convert time to human-readable format
        struct tm *tm_info;
        tm_info = localtime(&tv.tv_sec);

        sprintf(time_str, ": %02d:%02d:%02d:%06ld", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, microseconds);
    }

    void log( const char *func, const char *file, int line, int threadID,const char *format, ...)
    {
        const short kFormatMessageSize = 4096;
        char formatted[kFormatMessageSize];
        char time[24] = {0};
        va_list argptr;

        current_time(time);

        va_start(argptr, format);
        vsnprintf(formatted, kFormatMessageSize, format, argptr);
        va_end(argptr);

        fprintf(stderr, "[GUNIT][%d] INFO [%s:%d %s] %s: %s \n",
                    (int)syscall(SYS_gettid),
                    basename(file),
                    line,
                    time,
                    func,
                    formatted);

        fflush(stderr);
    }

    static void removeFile(const char* fileName)
    {
        if (std::remove(fileName) != 0)
        {
            TEST_LOG("ERROR: deleting File [%s] ...",strerror(errno));
        }
        else
        {
            TEST_LOG("File %s successfully deleted", fileName);
        }
    }

    // static void createFile(const char* fileName, const char* fileContent)
    // {
    //     removeFile(fileName);
    //     std::ofstream fileContentStream(fileName);
    //     fileContentStream << fileContent;
    //     fileContentStream << "\n";
    //     fileContentStream.close();
    //     TEST_LOG("File %s successfully created", fileName);
    // }
}

class XCastTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::XCast> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    Core::JSONRPC::Message message;
    string response;

    WrapsImplMock   *p_wrapsImplMock = nullptr;
    RfcApiImplMock  *p_rfcApiImplMock = nullptr;
    gdialserviceImplMock *p_gdialserviceImplMock = nullptr;

    Core::ProxyType<Plugin::XCastImplementation> xcastImpl;

    NiceMock<COMLinkMock> comLinkMock;
    NiceMock<ServiceMock> service;
    PLUGINHOST_DISPATCHER* dispatcher;
    Core::ProxyType<WorkerPoolImplementation> workerPool;
    
    NiceMock<FactoriesImplementation> factoriesImplementation;

    XCastTest()
        : plugin(Core::ProxyType<Plugin::XCast>::Create())
        , handler(*(plugin))
        , INIT_CONX(1, 0)
        , workerPool(Core::ProxyType<WorkerPoolImplementation>::Create(2, Core::Thread::DefaultStackSize(), 16))
    {
        p_wrapsImplMock = new NiceMock<WrapsImplMock>;
        printf("Pass created wrapsImplMock: %p ", p_wrapsImplMock);
        Wraps::setImpl(p_wrapsImplMock);

        p_rfcApiImplMock  = new NiceMock <RfcApiImplMock>;
        printf("Pass created RfcApiImplMock: %p ", p_rfcApiImplMock);
        RfcApi::setImpl(p_rfcApiImplMock);

        p_gdialserviceImplMock  = new NiceMock<gdialserviceImplMock>;
        printf("Pass created gdialserviceImplMock: %p ", p_gdialserviceImplMock);
        gdialService::setImpl(p_gdialserviceImplMock);
        
        ON_CALL(service, COMLink())
        .WillByDefault(::testing::Invoke(
            [this]() {
                    TEST_LOG("Pass created comLinkMock: %p ", &comLinkMock);
                    return &comLinkMock;
                }));

        #ifdef USE_THUNDER_R4
            ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_))
                .WillByDefault(::testing::Invoke(
                        [&](const RPC::Object& object, const uint32_t waitTime, uint32_t& connectionId) {
                            xcastImpl = Core::ProxyType<Plugin::XCastImplementation>::Create();
                            TEST_LOG("Pass created xcastImpl: %p ", &xcastImpl);
                            return &xcastImpl;
                    }));
        #else
            ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
                .WillByDefault(::testing::Return(xcastImpl));
        #endif /*USE_THUNDER_R4 */

        ON_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                    WDMP_STATUS wdmpStatus = WDMP_SUCCESS;
                    EXPECT_EQ(string(pcCallerID), string("XCastPlugin"));
                    if (string(pcParameterName) == "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.AppList") {
                        strncpy(pstParamData->value, "youtube::netflix", sizeof(pstParamData->value));
                        pstParamData->type = WDMP_STRING;
                    }
                    else if ((string(pcParameterName) == "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.FriendlyNameEnable")||
                            (string(pcParameterName) == "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.WolWakeEnable")||
                            (string(pcParameterName) == "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.DynamicAppList")) {
                        strncpy(pstParamData->value, "true", sizeof(pstParamData->value));
                        pstParamData->type = WDMP_BOOLEAN;
                    }
                    else {
                        EXPECT_EQ(string(pcParameterName), string("RFC mocks required for this parameter"));
                    }
                    return WDMP_SUCCESS;
                }));

        PluginHost::IFactories::Assign(&factoriesImplementation);

        Core::IWorkerPool::Assign(&(*workerPool));
        workerPool->Run();

        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(
        plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(&service);

        EXPECT_EQ(string(""), plugin->Initialize(&service));
    }

    virtual ~XCastTest() override
    {
        TEST_LOG("XCastTest Destructor");

        plugin->Deinitialize(&service);

        dispatcher->Deactivate();
        dispatcher->Release();

        Core::IWorkerPool::Assign(nullptr);
        workerPool.Release();
    
        Wraps::setImpl(nullptr);
        if (p_wrapsImplMock != nullptr)
        {
            delete p_wrapsImplMock;
            p_wrapsImplMock = nullptr;
        }
        RfcApi::setImpl(nullptr);
        if (p_rfcApiImplMock != nullptr)
        {
            delete p_rfcApiImplMock;
            p_rfcApiImplMock = nullptr;
        }
        gdialService::setImpl(nullptr);
        if (p_gdialserviceImplMock != nullptr)
        {
            delete p_gdialserviceImplMock;
            p_gdialserviceImplMock = nullptr;
        }
        PluginHost::IFactories::Assign(nullptr);
        IarmBus::setImpl(nullptr);
    }
};

class XCastEventTest : public XCastTest {
protected:
    NiceMock<ServiceMock> service;
    NiceMock<FactoriesImplementation> factoriesImplementation;
    PLUGINHOST_DISPATCHER* dispatcher;
    Core::JSONRPC::Message message;

    XCastEventTest()
        : XCastTest()
    {
        PluginHost::IFactories::Assign(&factoriesImplementation);

        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(&service);
    }

    virtual ~XCastEventTest() override
    {
        dispatcher->Deactivate();
        dispatcher->Release();

        PluginHost::IFactories::Assign(nullptr);
    }
};

TEST_F(XCastTest, RegisteredMethods)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("setApplicationState")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("setEnabled")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("getEnabled")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("getStandbyBehavior")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("setStandbyBehavior")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("getFriendlyName")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("setFriendlyName")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("getProtocolVersion")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("unregisterApplications")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("getProtocolVersion")));
}

TEST_F(XCastTest, getsetFriendlyName)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setFriendlyName"), _T("{\"friendlyname\": \"friendlyTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));


    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getFriendlyName"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"friendlyname\":\"friendlyTest\",\"success\":true}"));
}

TEST_F(XCastTest, getsetStandbyBehavoir)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setStandbyBehavior"), _T("{\"standbybehavior\": \"active\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getStandbyBehavior"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"standbybehavior\":\"active\",\"success\":true}"));
}

TEST_F(XCastTest, getsetManufacturerName)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setManufacturerName"), _T("{\"manufacturer\": \"manufacturerTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getManufacturerName"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"manufacturer\":\"manufacturerTest\",\"success\":true}"));
}

TEST_F(XCastTest, getsetModelName)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setModelName"), _T("{\"model\": \"modelTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getModelName"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"model\":\"modelTest\",\"success\":true}"));
}

TEST_F(XCastTest, setApplicationState)
{
    EXPECT_CALL(*p_gdialserviceImplMock, ApplicationStateChanged(::testing::_, ::testing::_, ::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke(
                [](string applicationName, string appState, string applicationId, string error) {
                    EXPECT_EQ(applicationName, string("NetflixApp"));
                    EXPECT_EQ(appState, string("running"));
                    EXPECT_EQ(applicationId, string("1234"));
                    EXPECT_EQ(error, string(""));
                    return GDIAL_SERVICE_ERROR_NONE;
                }));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setApplicationState"), _T("{\"applicationName\": \"NetflixApp\", \"state\":\"running\", \"applicationId\": \"1234\", \"error\": \"\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));
}

TEST_F(XCastTest, getProtocolVersion)
{
    EXPECT_CALL(*p_gdialserviceImplMock, getProtocolVersion())
            .WillOnce(::testing::Invoke(
                []() {
                    return std::string("test");
                }));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getProtocolVersion"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"version\":\"test\",\"success\":true}"));
}

TEST_F(XCastTest, unRegisterAllApplications)
{
    EXPECT_CALL(*p_gdialserviceImplMock, RegisterApplications(::testing::_))
            .WillOnce(::testing::Invoke([](RegisterAppEntryList* appConfigList) {
                    int i = 0;
                    for (RegisterAppEntry* appEntry : appConfigList->getValues())
                    {
                        if (0 == i)
                        {
                            EXPECT_EQ(appEntry->Names, string("Netflix"));
                            EXPECT_EQ(appEntry->prefixes, string("myNetflix"));
                            EXPECT_EQ(appEntry->cors, string("netflix.com"));
                            EXPECT_EQ(appEntry->allowStop, true);
                        }
                        else if (1 == i)
                        {
                            EXPECT_EQ(appEntry->Names, string("Youtube"));
                            EXPECT_EQ(appEntry->prefixes, string("myYouTube"));
                            EXPECT_EQ(appEntry->cors, string("youtube.com"));
                            EXPECT_EQ(appEntry->allowStop, true);
                        }
                        ++i;
                    }
                    return GDIAL_SERVICE_ERROR_NONE;
                }))
            .WillOnce(::testing::Invoke([](RegisterAppEntryList* appConfigList)
                {
                    int i = 0;
                    for (RegisterAppEntry* appEntry : appConfigList->getValues())
                    {
                        if (0 == i)
                        {
                            EXPECT_EQ(appEntry->Names, string("Netflix"));
                            EXPECT_EQ(appEntry->prefixes, string("myNetflix"));
                            EXPECT_EQ(appEntry->cors, string("netflix.com"));
                            EXPECT_EQ(appEntry->allowStop, true);
                        }
                        ++i;
                    }
                    return GDIAL_SERVICE_ERROR_NONE;
                }));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("registerApplications"), _T("{\"applications\": [{\"name\": \"Netflix\", \"prefix\": \"myNetflix\", \"cors\": \"netflix.com\", \"allowStop\": true, \"query\":\"netflix_query\", \"payload\":\"netflix_payload\"},{\"name\": \"Youtube\", \"prefix\": \"myYouTube\", \"cors\": \"youtube.com\", \"allowStop\": true, \"query\":\"youtube_query\", \"payload\":\"youtube_payload\"}]}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("unregisterApplications"), _T("{\"applications\": [\"Youtube\"]}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));
}

#if 0
TEST_F(XCastEventTest, onApplicationHideRequest)
{
    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationHideRequest\",\"params\":{\"applicationName\":\"NetflixApp\",\"applicationId\":\"1234\"}}")));
                return Core::ERROR_NONE;
            }));

    EVENT_SUBSCRIBE(0, _T("onApplicationHideRequest"), _T("client.events"), message);
    plugin->onApplicationHideRequest("Netflix", "1234");
    EVENT_UNSUBSCRIBE(0, _T("onApplicationHideRequest"), _T("client.events"), message);
}
TEST_F(XCastEventTest, onApplicationStateRequest)
{
    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationStateRequest\",\"params\":{\"applicationName\":\"NetflixApp\",\"applicationId\":\"1234\"}}")));
                return Core::ERROR_NONE;
            }));
    EVENT_SUBSCRIBE(0, _T("onApplicationStateRequest"), _T("client.events"), message);
    plugin->onApplicationStateRequest("Netflix", "1234");
    EVENT_UNSUBSCRIBE(0, _T("onApplicationStateRequest"), _T("client.events"), message);
}
TEST_F(XCastEventTest, onApplicationLaunchRequest)
{
    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationLaunchRequest\",\"params\":{\"applicationName\":\"Netflix\",\"parameters\":{\"url\":\"1234\"}}}")));
                return Core::ERROR_NONE;
            }));

    EVENT_SUBSCRIBE(0, _T("onApplicationLaunchRequest"), _T("client.events"), message);
    plugin->onApplicationLaunchRequest("Netflix", "1234");
    EVENT_UNSUBSCRIBE(0, _T("onApplicationLaunchRequest"), _T("client.events"), message);
}
TEST_F(XCastEventTest, onApplicationResumeRequest)
{
    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationResumeRequest\",\"params\":{\"applicationName\":\"NetflixApp\",\"applicationId\":\"1234\"}}")));
                return Core::ERROR_NONE;
            }));

    EVENT_SUBSCRIBE(0, _T("onApplicationResumeRequest"), _T("client.events"), message);
    plugin->onApplicationResumeRequest("Netflix", "1234");
    EVENT_UNSUBSCRIBE(0, _T("onApplicationResumeRequest"), _T("client.events"), message);
}
TEST_F(XCastEventTest, onApplicationStopRequest)
{
    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationStopRequest\",\"params\":{\"applicationName\":\"Netflix\",\"applicationId\":\"1234\"}}")));
                return Core::ERROR_NONE;
            }));

    EVENT_SUBSCRIBE(0, _T("onApplicationStopRequest"), _T("client.events"), message);
    plugin->onApplicationStopRequest("Netflix", "1234");
    EVENT_UNSUBSCRIBE(0, _T("onApplicationStopRequest"), _T("client.events"), message);
}
#endif