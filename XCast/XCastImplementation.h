/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
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
 */

#pragma once

#include "Module.h"
#include <interfaces/Ids.h>
#include <interfaces/IXCast.h>
#include <interfaces/IPowerManager.h>
#include <interfaces/IConfiguration.h>
#include <interfaces/INetworkManager.h>
 
#include <com/com.h>
#include <core/core.h>
#include <mutex>
#include <vector>
#include <glib.h> 

#include "XCastManager.h"
#include "XCastNotifier.h"

#include "libIBus.h"
#include "PowerManagerInterface.h"


#define SYSTEM_CALLSIGN "org.rdk.System"
#define SYSTEM_CALLSIGN_VER SYSTEM_CALLSIGN".1"
#define SECURITY_TOKEN_LEN_MAX 1024

using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;

namespace WPEFramework
{
    namespace Plugin
    {
        WPEFramework::Exchange::IPowerManager::PowerState m_powerState = WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY;
        class XCastImplementation : public Exchange::IXCast,public Exchange::IConfiguration, public XCastNotifier 
        {
         public:
            enum PluginState
            {
                PLUGIN_DEACTIVATED,
                PLUGIN_ACTIVATED
            };

             // We do not allow this plugin to be copied !!
             XCastImplementation();
             ~XCastImplementation() override;

              enum Event {
                    LAUNCH_REQUEST_WITH_PARAMS,
                    LAUNCH_REQUEST,
                    STOP_REQUEST,
                    HIDE_REQUEST,
                    STATE_REQUEST,
                    RESUME_REQUEST,
                    UPDATE_POWERSTATE
            };
 
             static XCastImplementation *instance(XCastImplementation *XCastImpl = nullptr);
 
             // We do not allow this plugin to be copied !!
             XCastImplementation(const XCastImplementation &) = delete;
             XCastImplementation &operator=(const XCastImplementation &) = delete;

        public:
             class EXTERNAL Job : public Core::IDispatch {
                protected:
                    Job(XCastImplementation *tts, Event event,string callsign,JsonObject &params)
                        : _xcast(tts)
                        , _event(event)
                        , _callsign(callsign)
                        , _params(params) {
                        if (_xcast != nullptr) {
                            _xcast->AddRef();
                        }
                    }

                public:
                    Job() = delete;
                    Job(const Job&) = delete;
                    Job& operator=(const Job&) = delete;
                    ~Job() {
                        if (_xcast != nullptr) {
                            _xcast->Release();
                        }
                    }

                public:
                    static Core::ProxyType<Core::IDispatch> Create(XCastImplementation *tts, Event event,string callsign,JsonObject params) {
                        #ifndef USE_THUNDER_R4
                            return (Core::proxy_cast<Core::IDispatch>(Core::ProxyType<Job>::Create(tts, event, callsign, params)));
                        #else
                            return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<Job>::Create(tts, event, callsign, params)));
                        #endif
                    }

                    virtual void Dispatch() {
                        _xcast->Dispatch(_event, _callsign, _params);
                    }

                private:
                    XCastImplementation *_xcast;
                    const Event _event;
                    const string _callsign;
                    const JsonObject _params;
            };

        private:
            class PowerManagerNotification : public Exchange::IPowerManager::INetworkStandbyModeChangedNotification,
                                                     public Exchange::IPowerManager::IModeChangedNotification {
                private:
                    PowerManagerNotification(const PowerManagerNotification&) = delete;
                    PowerManagerNotification& operator=(const PowerManagerNotification&) = delete;

                public:
                    explicit PowerManagerNotification(XCastImplementation& parent)
                    : _parent(parent)
                    {
                    }
                    ~PowerManagerNotification() override = default;

                public:
                    void OnPowerModeChanged(const PowerState currentState, const PowerState newState) override
                    {
                        LOGINFO("onPowerModeChanged: State Changed [%d] -- > [%d]",currentState, newState);
                        m_powerState = newState;
                        LOGINFO("creating worker thread for threadPowerModeChangeEvent m_powerState :%d",m_powerState);
                        std::thread powerModeChangeThread = std::thread(&XCastImplementation::threadPowerModeChangeEvent,&_parent);
                        powerModeChangeThread.detach();
                    }

                    void OnNetworkStandbyModeChanged(const bool enabled) override
                    {
                        _parent.m_networkStandbyMode = enabled;
                        LOGWARN("creating worker thread for threadNetworkStandbyModeChangeEvent Mode :%u", _parent.m_networkStandbyMode);
                        std::thread networkStandbyModeChangeThread = std::thread(&XCastImplementation::networkStandbyModeChangeEvent,&_parent);
                        networkStandbyModeChangeThread.detach();
                    }

                    template <typename T>
                    T* baseInterface()
                    {
                        static_assert(std::is_base_of<T, PowerManagerNotification>(), "base type mismatch");
                        return static_cast<T*>(this);
                    }

                    BEGIN_INTERFACE_MAP(PowerManagerNotification)
                    INTERFACE_ENTRY(Exchange::IPowerManager::INetworkStandbyModeChangedNotification)
                    INTERFACE_ENTRY(Exchange::IPowerManager::IModeChangedNotification)
                    END_INTERFACE_MAP

                private:
                    XCastImplementation& _parent;
            };

            class NetworkManagerNotification : public Exchange::INetworkManager::INotification
            {
                private:
                    NetworkManagerNotification(const NetworkManagerNotification&) = delete;
                    NetworkManagerNotification& operator=(const NetworkManagerNotification&) = delete;

                public:
                    explicit NetworkManagerNotification(XCastImplementation& parent)
                    : _parent(parent)
                    {
                    }
                    ~NetworkManagerNotification() override = default;

                public:
                    void onActiveInterfaceChange(const string prevActiveInterface, const string currentActiveinterface) override
                    {
                        LOGINFO("Active interface changed [%s] -- > [%s]",prevActiveInterface.c_str(), currentActiveinterface.c_str());
                        _parent.onActiveInterfaceChange(std::move(prevActiveInterface), std::move(currentActiveinterface));
                    }

                    void onIPAddressChange(const string interface, const string ipversion, const string ipaddress, const Exchange::INetworkManager::IPStatus status) override
                    {
                        LOGINFO("IP Address changed: Interface [%s] IP Version [%s] Address [%s] Status [%d]", interface.c_str(), ipversion.c_str(), ipaddress.c_str(), status);
                        _parent.onIPAddressChange(std::move(interface), std::move(ipversion), std::move(ipaddress), status);
                    }

                    void onInterfaceStateChange(const Exchange::INetworkManager::InterfaceState state, const string interface) override
                    {
                        LOGINFO("Interface State Changed: Interface [%s] State [%d]", interface.c_str(), state);
                    }

                    void onInternetStatusChange(const Exchange::INetworkManager::InternetStatus prevState, const Exchange::INetworkManager::InternetStatus currState, const string interface) override
                    {
                        LOGINFO("Internet Status Changed for Interface [%s]: [%d] -- > [%d]", interface.c_str(), prevState, currState);
                    }

                    void onAvailableSSIDs(const string jsonOfScanResults) override
                    {
                        LOGINFO("SSIDs: [%s]", jsonOfScanResults.c_str());
                    }

                    void onWiFiStateChange(const Exchange::INetworkManager::WiFiState state) override
                    {
                        LOGINFO("WiFi State changed: [%d]", state);
                    }

                    void onWiFiSignalQualityChange(const string ssid, const string strength, const string noise, const string snr, const Exchange::INetworkManager::WiFiSignalQuality quality) override
                    {
                        LOGINFO("WiFi Signal Quality changed: SSID [%s] Strength [%s] Noise [%s] SNR [%s] Quality [%d]", ssid.c_str(), strength.c_str(), noise.c_str(), snr.c_str(), quality);
                    }

                    BEGIN_INTERFACE_MAP(NetworkManagerNotification)
                    INTERFACE_ENTRY(Exchange::INetworkManager::INotification)
                    END_INTERFACE_MAP

                private:
                    XCastImplementation& _parent;
            };
 
        public:
            Core::hresult Register(Exchange::IXCast::INotification *notification) override;
            Core::hresult Unregister(Exchange::IXCast::INotification *notification) override; 
            
            Core::hresult SetApplicationState(const string& applicationName, const Exchange::IXCast::State& state, const string& applicationId, const Exchange::IXCast::ErrorCode& error,  Exchange::IXCast::XCastSuccess &success) override;
            Core::hresult GetProtocolVersion(string &protocolVersion, bool &success) override;
            Core::hresult SetManufacturerName(const string &manufacturername,  Exchange::IXCast::XCastSuccess &success) override;
            Core::hresult GetManufacturerName(string &manufacturername, bool &success) override;
            Core::hresult SetModelName(const string &modelname,  Exchange::IXCast::XCastSuccess &success) override;
            Core::hresult GetModelName(string &modelname, bool &success) override;
            Core::hresult SetEnabled(const bool& enabled,  Exchange::IXCast::XCastSuccess &success) override;
            Core::hresult GetEnabled(bool &enabled , bool &success ) override;
            Core::hresult SetStandbyBehavior(const Exchange::IXCast::StandbyBehavior &standbybehavior,  Exchange::IXCast::XCastSuccess &success) override;
            Core::hresult GetStandbyBehavior(Exchange::IXCast::StandbyBehavior &standbybehavior, bool &success) override;
            Core::hresult SetFriendlyName(const string &friendlyname,  Exchange::IXCast::XCastSuccess &success) override;
            Core::hresult GetFriendlyName(string &friendlyname , bool &success ) override;
            Core::hresult RegisterApplications(Exchange::IXCast::IApplicationInfoIterator* const appInfoList,  Exchange::IXCast::XCastSuccess &success) override;
            Core::hresult UnregisterApplications(Exchange::IXCast::IStringIterator* const apps,  Exchange::IXCast::XCastSuccess &success) override;

            virtual void onXcastApplicationLaunchRequestWithParam (string appName, string strPayLoad, string strQuery, string strAddDataUrl) override ;
            virtual void onXcastApplicationLaunchRequest(string appName, string parameter) override ;
            virtual void onXcastApplicationStopRequest(string appName, string appId) override ;
            virtual void onXcastApplicationHideRequest(string appName, string appId) override ;
            virtual void onXcastApplicationResumeRequest(string appName, string appId) override ;
            virtual void onXcastApplicationStateRequest(string appName, string appId) override ;
            virtual void onGDialServiceStopped(void) override;

            BEGIN_INTERFACE_MAP(XCastImplementation)
            INTERFACE_ENTRY(Exchange::IXCast)
            INTERFACE_ENTRY(Exchange::IConfiguration)
            END_INTERFACE_MAP

        private:
            static XCastManager* m_xcast_manager;
            TpTimer m_locateCastTimer;
            
            WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement> *m_SystemPluginObj = NULL;
            PluginHost::IShell* _service;

            PowerManagerInterfaceRef _powerManagerPlugin;
            Core::Sink<PowerManagerNotification> _pwrMgrNotification;
            void threadPowerModeChangeEvent(void);
            void networkStandbyModeChangeEvent(void);
            static bool m_xcastEnable;
            static bool m_standbyBehavior;
            bool m_networkStandbyMode;
            bool _registeredPowerEventHandlers;
            bool _registeredNMEventHandlers;

        private:
            Exchange::INetworkManager* _networkManagerPlugin;
            mutable Core::CriticalSection _adminLock;
             
            std::list<Exchange::IXCast::INotification *> _xcastNotification; // List of registered notifications
            Core::Sink<NetworkManagerNotification> _networkManagerNotification;

            void dumpDynamicAppCacheList(string strListName, std::vector<DynamicAppConfig*>& appConfigList);
            bool deleteFromDynamicAppCache(vector<string>& appsToDelete);

            void dispatchEvent(Event,string callsign, const JsonObject &params);
            void Dispatch(Event event,string callsign, const JsonObject params);

            uint32_t Initialize(bool networkStandbyMode);
            void Deinitialize(void);

            void onActiveInterfaceChange(const string prevActiveInterface, const string currentActiveinterface);
            void onIPAddressChange(const string interface, const string ipversion, const string ipaddress, const Exchange::INetworkManager::IPStatus status);

            void onLocateCastTimer();
            void startTimer(int interval);
            void stopTimer();
            bool isTimerActive();

            void registerPowerEventHandlers();
            void unregisterPowerEventHandlers();
            void checkPowerAndNetworkStandbyStates();
            void InitializePowerManager(PluginHost::IShell *service);

            void registerNetworkEventHandlers();
            void unregisterNetworkEventHandlers();
            void InitializeNetworkManager(PluginHost::IShell *service);
            string getInterfaceNameToType(const string & interface);

            bool connectToGDialService(void);
            bool getDefaultNameAndIPAddress(std::string& interface, std::string& ipaddress);
            void updateNWConnectivityStatus(std::string nwInterface, bool nwConnected, std::string ipaddress = "");
            uint32_t enableCastService(string friendlyname,bool enableService);
            uint32_t Configure(PluginHost::IShell* shell);
            
            void getSystemPlugin();
            int updateSystemFriendlyName();
            void threadSystemFriendlyNameChangeEvent(void);
            void onFriendlyNameUpdateHandler(const JsonObject& parameters);
            
            void onXcastUpdatePowerStateRequest(string powerState);
            uint32_t SetNetworkStandbyMode(bool networkStandbyMode);
            bool setPowerState(const std::string& powerState);
            void updateDynamicAppCache(Exchange::IXCast::IApplicationInfoIterator* const appInfoList);
            
        public:
            static XCastImplementation* _instance;
            friend class Job;
        };
    } // namespace Plugin
} // namespace WPEFramework