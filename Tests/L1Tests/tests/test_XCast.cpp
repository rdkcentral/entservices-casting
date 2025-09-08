/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
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
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mntent.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>

#include "XCast.h"
#include "XCastImplementation.h"
#include "ServiceMock.h"
#include "COMLinkMock.h"
#include "ThunderPortability.h"
#include "Module.h"
#include "WorkerPoolImplementation.h"
#include "WrapsMock.h"
#include "RfcApiMock.h"
#include "gdialserviceMock.h"
#include "NetworkManagerMock.h"
#include "FactoriesImplementation.h"

#define TEST_LOG(x, ...) fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); fflush(stderr);

using ::testing::NiceMock;
using namespace WPEFramework;

class XCastTest : public ::testing::Test {
protected:
    ServiceMock* mServiceMock = nullptr;
    WrapsImplMock *p_wrapsImplMock   = nullptr;
    MockINetworkManager* mockNetworkManager = nullptr;
    gdialServiceImplMock *p_gdialserviceImplMock = nullptr;
    Core::JSONRPC::Message message;
    FactoriesImplementation factoriesImplementation;
    PLUGINHOST_DISPATCHER *dispatcher;

    Core::ProxyType<Plugin::XCast> plugin;
    Plugin::XCastImplementation *mXCastImpl;

    Core::ProxyType<WorkerPoolImplementation> workerPool;
    Core::JSONRPC::Handler& mJsonRpcHandler;
    DECL_CORE_JSONRPC_CONX connection;
    string mJsonRpcResponse;

    Core::hresult createResources()
    {
        Core::hresult status = Core::ERROR_GENERAL;
        mServiceMock = new NiceMock<ServiceMock>;
        p_wrapsImplMock  = new NiceMock <WrapsImplMock>;
        Wraps::setImpl(p_wrapsImplMock);
        p_gdialserviceImplMock  = new NiceMock<gdialServiceImplMock>;
        printf("Pass created gdialServiceImplMock: %p ", p_gdialserviceImplMock);
        gdialService::setImpl(p_gdialserviceImplMock);
        p_rfcApiImplMock  = new NiceMock <RfcApiImplMock>;
        printf("Pass created RfcApiImplMock: %p ", p_rfcApiImplMock);
        RfcApi::setImpl(p_rfcApiImplMock);
        mockNetworkManager = new MockINetworkManager();

        PluginHost::IFactories::Assign(&factoriesImplementation);
        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(
        plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(mServiceMock);
        TEST_LOG("In createResources!");
        EXPECT_CALL(*mServiceMock, QueryInterfaceByCallsign(::testing::_, ::testing::_))
          .Times(::testing::AnyNumber())
          .WillRepeatedly(::testing::Invoke(
              [&](const uint32_t id, const std::string& name) -> void* {
                if (name == "org.rdk.NetworkManager.1") {
                    return static_cast<void*>(mockNetworkManager);
                }
            return nullptr;
        }));

        EXPECT_CALL(*mockNetworkManager, GetPrimaryInterface(::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
                    [&](string& interface) -> uint32_t {
                    interface = "eth0";
                    return Core::ERROR_NONE;
                    }));

        EXPECT_CALL(*mockNetworkManager, GetIPSettings(::testing::_, ::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
                    [&](string& , const string&, WPEFramework::Exchange::INetworkManager::IPAddress& address) -> uint32_t {
                    address.primarydns = "75.75.75.76";
                    address.secondarydns = "75.75.76.76";
                    address.dhcpserver = "192.168.0.1";
                    address.gateway = "192.168.0.1";
                    address.ipaddress = "192.168.0.11";
                    address.ipversion = "IPv4";
                    address.prefix = 24;
                    return Core::ERROR_NONE;
                    }));

        EXPECT_EQ(string(""), plugin->Initialize(mServiceMock));
        mXCastImpl = Plugin::XCastImplementation::getInstance();
        TEST_LOG("createResources - All done!");
        status = Core::ERROR_NONE;

        return status;
    }

    void releaseResources()
    {
        TEST_LOG("In releaseResources!");

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

        dispatcher->Deactivate();
        dispatcher->Release();

        plugin->Deinitialize(mServiceMock);
        delete mServiceMock;
        mXCastImpl = nullptr;
    }
    XCastTest()
        : plugin(Core::ProxyType<Plugin::XCast>::Create()),
        workerPool(Core::ProxyType<WorkerPoolImplementation>::Create(2, Core::Thread::DefaultStackSize(), 16)),
        mJsonRpcHandler(*plugin),
        INIT_CONX(1, 0)
    {
        Core::IWorkerPool::Assign(&(*workerPool));
        workerPool->Run();
    }

    virtual ~XCastTest() override
    {
        TEST_LOG("Delete ~XCastTest Instance!");
        Core::IWorkerPool::Assign(nullptr);
        workerPool.Release();
    }
};

TEST_F(XCastTest, RegisteredMethodsUsingJsonRpcSuccess)
{
    Core::hresult status;

    status = createResources();
    EXPECT_EQ(Core::ERROR_NONE, status);

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("setApplicationState")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("setEnabled")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getEnabled")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getStandbyBehavior")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("setStandbyBehavior")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getFriendlyName")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("setFriendlyName")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getProtocolVersion")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("unregisterApplications")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getProtocolVersion")));

    if(status == Core::ERROR_NONE)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getsetFriendlyName)
{
    Core::hresult status;

    status = createResources();
    EXPECT_EQ(Core::ERROR_NONE, status);

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setFriendlyName"), _T("{\"friendlyname\": \"friendlyTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getFriendlyName"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"friendlyname\":\"friendlyTest\",\"success\":true}"));

    if(status == Core::ERROR_NONE)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getsetStandbyBehavoir)
{
    Core::hresult status;
    status = createResources();
    EXPECT_EQ(Core::ERROR_NONE, status);

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setStandbyBehavior"), _T("{\"standbybehavior\": \"active\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getStandbyBehavior"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"standbybehavior\":\"active\",\"success\":true}"));

    if(status == Core::ERROR_NONE)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getsetManufacturerName)
{
    Core::hresult status;
    status = createResources();
    EXPECT_EQ(Core::ERROR_NONE, status);

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setManufacturerName"), _T("{\"manufacturer\": \"manufacturerTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getManufacturerName"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"manufacturer\":\"manufacturerTest\",\"success\":true}"));

    if(status == Core::ERROR_NONE)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getsetModelName)
{
    Core::hresult status;
    status = createResources();
    EXPECT_EQ(Core::ERROR_NONE, status);

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setModelName"), _T("{\"model\": \"modelTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getModelName"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"model\":\"modelTest\",\"success\":true}"));

    if(status == Core::ERROR_NONE)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, setApplicationState)
{
    Core::hresult status;
    status = createResources();
    EXPECT_EQ(Core::ERROR_NONE, status);

    EXPECT_CALL(*p_gdialserviceImplMock, ApplicationStateChanged(::testing::_, ::testing::_, ::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke(
                [](string applicationName, string appState, string applicationId, string error) {
                    EXPECT_EQ(applicationName, string("NetflixApp"));
                    EXPECT_EQ(appState, string("running"));
                    EXPECT_EQ(applicationId, string("1234"));
                    EXPECT_EQ(error, string(""));
                    return GDIAL_SERVICE_ERROR_NONE;
                }));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setApplicationState"), _T("{\"applicationName\": \"NetflixApp\", \"state\":\"running\", \"applicationId\": \"1234\", \"error\": \"\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    if(status == Core::ERROR_NONE)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getProtocolVersion)
{
    Core::hresult status;
    status = createResources();
    EXPECT_EQ(Core::ERROR_NONE, status);

    EXPECT_CALL(*p_gdialserviceImplMock, getProtocolVersion())
            .WillOnce(::testing::Invoke(
                []() {
                    return std::string("test");
                }));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getProtocolVersion"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"version\":\"test\",\"success\":true}"));

    if(status == Core::ERROR_NONE)
    {
        releaseResources();
    }
}