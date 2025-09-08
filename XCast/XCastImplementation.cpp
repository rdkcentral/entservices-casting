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

#include "XCastImplementation.h"
#include <sys/prctl.h>
 
#include "UtilsJsonRpc.h"
#include "UtilsIarm.h"
#include "UtilsSynchroIarm.hpp"

#include "rfcapi.h"
#include <string> 
#include <vector>

#define LOG_INPARAM() { string json; parameters.ToString(json); NMLOG_INFO("params=%s", json.c_str() ); }

#if defined(SECURITY_TOKEN_ENABLED) && ((SECURITY_TOKEN_ENABLED == 0) || (SECURITY_TOKEN_ENABLED == false))
#define GetSecurityToken(a, b) 0
#define GetToken(a, b, c) 0
#else
#include <securityagent/securityagent.h>
#include <securityagent/SecurityTokenUtil.h>
#endif
 
#define SERVER_DETAILS "127.0.0.1:9998"
#define NETWORK_CALLSIGN_VER "org.rdk.Network.1"
#define THUNDER_RPC_TIMEOUT 5000
#define MAX_SECURITY_TOKEN_SIZE 1024

#define API_VERSION_NUMBER_MAJOR 2
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 9

 
#define LOCATE_CAST_FIRST_TIMEOUT_IN_MILLIS  5000  //5 seconds
#define LOCATE_CAST_SECOND_TIMEOUT_IN_MILLIS 10000  //10 seconds


#define DIAL_MAX_ADDITIONALURL (1024)
 
 
namespace WPEFramework
{
    namespace Plugin
    {
        SERVICE_REGISTRATION(XCastImplementation, 1, 0);
        XCastImplementation *XCastImplementation::_instance = nullptr;    
        XCastManager* XCastImplementation::m_xcast_manager = nullptr;
        static std::vector <DynamicAppConfig*> m_appConfigCache;
        static std::mutex m_appConfigMutex;
        static std::mutex m_TimerMutexSync;
        static bool xcastEnableCache = false;

        #ifdef XCAST_ENABLED_BY_DEFAULT
        bool XCastImplementation::m_xcastEnable = true;
        #else
        bool XCastImplementation::m_xcastEnable = false;
        #endif

        #ifdef XCAST_ENABLED_BY_DEFAULT_IN_STANDBY
        bool XCastImplementation::m_standbyBehavior = true;
        #else
        bool XCastImplementation::m_standbyBehavior = false;
        #endif

        bool m_networkStandbyMode = false;
        string m_friendlyName = "";

        bool powerModeChangeActive = false;

        static string friendlyNameCache = "Living Room";
        static string m_activeInterfaceName = "";
        static bool m_isDynamicRegistrationsRequired = false;

        static bool m_is_restart_req = false;
        static int m_sleeptime = 1;

        XCastImplementation::XCastImplementation()
        : _service(nullptr),
        _pwrMgrNotification(*this),
         _registeredPowerEventHandlers(false),
         _registeredNMEventHandlers(false),
         _networkManagerPlugin(nullptr),
         _adminLock()
        {
            LOGINFO("Create XCastImplementation Instance");
            m_locateCastTimer.connect( bind( &XCastImplementation::onLocateCastTimer, this ));
            XCastImplementation::_instance = this;
        }
 
        XCastImplementation::~XCastImplementation()
        {
            LOGINFO("Call XCastImplementation destructor\n");
            XCastImplementation::_instance = nullptr;
            _service = nullptr;
        }
         
         /**
          * Register a notification callback
          */
        Core::hresult XCastImplementation::Register(Exchange::IXCast::INotification *notification)
        {
            ASSERT(nullptr != notification);
 
            _adminLock.Lock();
            LOGINFO("Register notification %p", notification);

            // Make sure we can't register the same notification callback multiple times
            if (std::find(_xcastNotification.begin(), _xcastNotification.end(), notification) == _xcastNotification.end())
            {
                _xcastNotification.push_back(notification);
                notification->AddRef();
            }
            else
            {
                LOGERR("same notification is registered already");
            }
 
            _adminLock.Unlock();

            LOGINFO("Registered a notification on the xcast inprocess %p", notification);
 
            return Core::ERROR_NONE;
         }
 
         /**
          * Unregister a notification callback
          */
        Core::hresult XCastImplementation::Unregister(Exchange::IXCast::INotification *notification)
        {
            Core::hresult status = Core::ERROR_GENERAL;
 
            ASSERT(nullptr != notification);
 
            _adminLock.Lock();
 
            // we just unregister one notification once
            auto itr = std::find(_xcastNotification.begin(), _xcastNotification.end(), notification);
            if (itr != _xcastNotification.end())
            {
                (*itr)->Release();
                LOGINFO("Unregister notification");
                _xcastNotification.erase(itr);
                status = Core::ERROR_NONE;
            }
            else
            {
                LOGERR("notification not found");
            }
 
            _adminLock.Unlock();
 
            return status;
         }

        uint32_t XCastImplementation::Initialize(bool networkStandbyMode)
        {
            LOGINFO("Entering..!!!");
            if(nullptr == m_xcast_manager)
            {
                m_networkStandbyMode = networkStandbyMode;
                m_xcast_manager  = XCastManager::getInstance();
                if(nullptr != m_xcast_manager)
                {
                    m_xcast_manager->setService(this);
                    if( false == connectToGDialService())
                    {
                        startTimer(LOCATE_CAST_FIRST_TIMEOUT_IN_MILLIS);
                    }
                }
            }
            LOGINFO("Exiting ..!!!");
            return Core::ERROR_NONE;
        }

        void XCastImplementation::Deinitialize(void)
        {
            LOGINFO("Entering..!!!");

            if(nullptr != m_xcast_manager)
            {
                LOGINFO("TRACE");
                stopTimer();
                LOGINFO("TRACE");
                m_xcast_manager->shutdown();
                LOGINFO("TRACE");
                m_xcast_manager = nullptr;
                LOGINFO("TRACE");
            }
            LOGINFO("TRACE");
            unregisterPowerEventHandlers();
            LOGINFO("TRACE");
            unregisterNetworkEventHandlers();
            LOGINFO("TRACE");
            if (_powerManagerPlugin) {
                _powerManagerPlugin.Reset();
            }
            LOGINFO("TRACE");
            if (_networkManagerPlugin) {
                _networkManagerPlugin->Release();
            }
            LOGINFO("TRACE");
            LOGINFO("Exiting ...");
        }

        void XCastImplementation::onActiveInterfaceChange(const string prevActiveInterface, const string currentActiveinterface)
        {
            LOGINFO("XCast onDefaultInterfaceChanged, old interface: %s, new interface: %s", prevActiveInterface.c_str(), currentActiveinterface.c_str());
            updateNWConnectivityStatus(currentActiveinterface.c_str(), true);
        }

        void XCastImplementation::onIPAddressChange(const string interface, const string ipversion, const string ipaddress, const Exchange::INetworkManager::IPStatus status)
        {
            if (("IPv4" == ipversion) && (Exchange::INetworkManager::IP_ACQUIRED == status))
            {
                bool isAcquired = false;
                if (!ipaddress.empty())
                {
                    isAcquired = true;
                }
                updateNWConnectivityStatus(interface.c_str(), isAcquired, ipaddress.c_str());
            }
        }

        void XCastImplementation::getSystemPlugin()
        {
            LOGINFO("Entering..!!!");
            if(nullptr == m_SystemPluginObj)
            {
                string token = getSecurityToken();
                string query = "token=" + token;
                Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), (_T(SERVER_DETAILS)));
                m_SystemPluginObj = new WPEFramework::JSONRPC::LinkType<Core::JSON::IElement>(_T(SYSTEM_CALLSIGN_VER), (_T(SYSTEM_CALLSIGN_VER)), false, query);
                if (nullptr == m_SystemPluginObj)
                {
                    LOGERR("JSONRPC: %s: initialization failed", SYSTEM_CALLSIGN_VER);
                }
                else
                {
                    LOGINFO("JSONRPC: %s: initialization ok", SYSTEM_CALLSIGN_VER);
                }
            }
            LOGINFO("Exiting..!!!");
        }

        int XCastImplementation::updateSystemFriendlyName()
        {
            JsonObject params, Result;
            LOGINFO("Entering..!!!");

            if (nullptr == m_SystemPluginObj)
            {
                LOGERR("m_SystemPluginObj not yet instantiated");
                return Core::ERROR_GENERAL;
            }

            uint32_t ret = m_SystemPluginObj->Invoke<JsonObject, JsonObject>(THUNDER_RPC_TIMEOUT, _T("getFriendlyName"), params, Result);

            if (Core::ERROR_NONE == ret)
            {
                if (Result["success"].Boolean())
                {
                    m_friendlyName = Result["friendlyName"].String();
                }
                else
                {
                    ret = Core::ERROR_GENERAL;
                    LOGERR("getSystemFriendlyName call failed");
                }
            }
            else
            {
                LOGERR("getSystemFriendlyName call failed E[%u]", ret);
            }
            return ret;
        }


        void XCastImplementation::onFriendlyNameUpdateHandler(const JsonObject& parameters)
        {
            string message;
            string value;
            parameters.ToString(message);
            LOGINFO("[Friendly Name Event], %s : %s", __FUNCTION__,message.c_str());

            if (parameters.HasLabel("friendlyName")) {
                value = parameters["friendlyName"].String();

                    m_friendlyName = std::move(value);
                    LOGINFO("onFriendlyNameUpdateHandler  :%s",m_friendlyName.c_str());
                    if (m_FriendlyNameUpdateTimerID)
                    {
                        g_source_remove(m_FriendlyNameUpdateTimerID);
                        m_FriendlyNameUpdateTimerID = 0;
                    }
                    m_FriendlyNameUpdateTimerID = g_timeout_add(50, XCastImplementation::update_friendly_name_timercallback, this);
                    if (0 == m_FriendlyNameUpdateTimerID)
                    {
                        bool enabledStatus = false;
                        LOGWARN("Failed to create the timer. Setting friendlyName immediately");
                        if (m_xcastEnable && ( (m_standbyBehavior == true) || ((m_standbyBehavior == false)&&(m_powerState == WPEFramework::Exchange::IPowerManager::POWER_STATE_ON))))
                        {
                            enabledStatus = true;
                        }
                        LOGINFO("Updating FriendlyName [%s] status[%x]",m_friendlyName.c_str(),enabledStatus);
                        enableCastService(m_friendlyName,enabledStatus);
                    }
                    else
                    {
                        LOGINFO("Timer triggered to update friendlyName");
                    }
            }
        }

        gboolean XCastImplementation::update_friendly_name_timercallback(gpointer userdata)
        {
            XCastImplementation *self = (XCastImplementation *)userdata;
            bool enabledStatus = false;

            if (m_xcastEnable && ( (m_standbyBehavior == true) || ((m_standbyBehavior == false)&&(m_powerState == WPEFramework::Exchange::IPowerManager::POWER_STATE_ON))))
            {
                enabledStatus = true;
            }

            if (self)
            {
                LOGINFO("Updating FriendlyName from Timer [%s] status[%x]",m_friendlyName.c_str(),enabledStatus);
                self->enableCastService(m_friendlyName,enabledStatus);
            }
            else
            {
                LOGERR("instance NULL [%p]",self);
            }
            return G_SOURCE_REMOVE;
        }
 
         uint32_t XCastImplementation::Configure(PluginHost::IShell* service)
         {
            uint32_t result = Core::ERROR_NONE;
            if (( nullptr == _service ) && (service))
            {
                LOGINFO("Call initialise()\n");
                ASSERT(service != nullptr);
                _service = service;
                _service->AddRef();
                InitializePowerManager(service);
                InitializeNetworkManager(service);
                Initialize(m_networkStandbyMode);
                getSystemPlugin();
                m_SystemPluginObj->Subscribe<JsonObject>(1000, "onFriendlyNameChanged", &XCastImplementation::onFriendlyNameUpdateHandler, this);
                if (Core::ERROR_NONE == updateSystemFriendlyName())
                {
                    LOGINFO("XCast::Initialize m_friendlyName:  %s\n ",m_friendlyName.c_str());
                }
            }
            else if ((_service) && ( nullptr == service ))
            {
                lock_guard<mutex> lck(m_TimerMutexSync);
                LOGINFO("Call deinitialise()\n");
                Deinitialize();
                _service->Release();
            }
            else
            {
                LOGERR("Invalid call");
                result = Core::ERROR_GENERAL;
            }
            return result;
         }

        void XCastImplementation::InitializePowerManager(PluginHost::IShell* service)
        {
            if (nullptr == _powerManagerPlugin)
            {
                _powerManagerPlugin = PowerManagerInterfaceBuilder(_T("org.rdk.PowerManager"))
                    .withIShell(service)
                    .withRetryIntervalMS(200)
                    .withRetryCount(25)
                    .createInterface();

                if (_powerManagerPlugin) {
                    LOGINFO("PowerManagerInterfaceBuilder created successfully");
                    checkPowerAndNetworkStandbyStates();
                }
                else {
                    LOGERR("Failed to get PowerManager instance");
                }
            }
        }

        void XCastImplementation::InitializeNetworkManager(PluginHost::IShell* service)
        {
            if (nullptr == _networkManagerPlugin)
            {
                _networkManagerPlugin = service->QueryInterfaceByCallsign<WPEFramework::Exchange::INetworkManager>("org.rdk.NetworkManager");
                if (_networkManagerPlugin != nullptr)
                {
                    registerNetworkEventHandlers();
                }
                else
                {
                    LOGERR("Failed to get NetworkManager instance");
                }
            }
        }

        void XCastImplementation::registerPowerEventHandlers()
        {
            ASSERT (_powerManagerPlugin);
            if(!_registeredPowerEventHandlers && _powerManagerPlugin) {
                _powerManagerPlugin->Register(_pwrMgrNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
                LOGINFO("INetworkStandbyModeChangedNotification event registered");
                _powerManagerPlugin->Register(_pwrMgrNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
                LOGINFO("IModeChangedNotification event registered");
                _registeredPowerEventHandlers = true;
            }
        }

        void XCastImplementation::unregisterPowerEventHandlers()
        {
            ASSERT (_powerManagerPlugin);
            if (_registeredPowerEventHandlers && _powerManagerPlugin) {
                _powerManagerPlugin->Unregister(_pwrMgrNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
                LOGINFO("INetworkStandbyModeChangedNotification event unregistered");
                _powerManagerPlugin->Unregister(_pwrMgrNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
                LOGINFO("IModeChangedNotification event unregistered");
                _registeredPowerEventHandlers = false;
            }
        }

        void XCastImplementation::registerNetworkEventHandlers()
        {
            if (_networkManagerPlugin)
            {
                if (Core::ERROR_NONE == _networkManagerPlugin->Register(&_networkManagerNotification))
                {
                    LOGINFO("INetworkManager::Register event registered");
                    _registeredNMEventHandlers = true;
                }
                else
                {
                    LOGERR("Failed to register INetworkManager::Register event");
                    _registeredNMEventHandlers = false;
                }
            }
        }

        void XCastImplementation::unregisterNetworkEventHandlers()
        {
            if (_registeredNMEventHandlers && _networkManagerPlugin)
            {
                _networkManagerPlugin->Unregister(&_networkManagerNotification);
                LOGINFO("INetworkManager::Unregister event unregistered");
                _registeredNMEventHandlers = false;
            }
        }

        void XCastImplementation::checkPowerAndNetworkStandbyStates()
        {
            Core::hresult retStatus = Core::ERROR_GENERAL;
            PowerState pwrStateCur = WPEFramework::Exchange::IPowerManager::POWER_STATE_UNKNOWN;
            PowerState pwrStatePrev = WPEFramework::Exchange::IPowerManager::POWER_STATE_UNKNOWN;
            bool nwStandby = false;

            ASSERT (_powerManagerPlugin);
            if (_powerManagerPlugin){
                retStatus = _powerManagerPlugin->GetPowerState(pwrStateCur, pwrStatePrev);
                if (Core::ERROR_NONE == retStatus)
                {
                    m_powerState = pwrStateCur;
                    LOGINFO("m_powerState:%d", m_powerState);
                }

                retStatus = _powerManagerPlugin->GetNetworkStandbyMode(nwStandby);
                if (Core::ERROR_NONE == retStatus)
                {
                    m_networkStandbyMode = nwStandby;
                    LOGINFO("m_networkStandbyMode:%u ",m_networkStandbyMode);
                }
            }
        }

        void XCastImplementation::threadPowerModeChangeEvent(void)
        {
            powerModeChangeActive = true;
            LOGINFO(" threadPowerModeChangeEvent m_standbyBehavior:%d , m_powerState:%d ",m_standbyBehavior,m_powerState);
            if(m_powerState == WPEFramework::Exchange::IPowerManager::POWER_STATE_ON)
            {
                m_sleeptime = 1;
                if (m_is_restart_req)
                {
                    Deinitialize();
                    sleep(1);
                    Initialize(m_networkStandbyMode);
                    m_is_restart_req = false;
                }
            }
            else if (m_powerState == WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP )
            {
                m_sleeptime = 3;
                m_is_restart_req = true; //After DEEPSLEEP, restart xdial again for next transition.
            }

            if(m_standbyBehavior == false)
            {
                if(m_xcastEnable && ( m_powerState == WPEFramework::Exchange::IPowerManager::POWER_STATE_ON))
                    enableCastService(m_friendlyName,true);
                else
                    enableCastService(m_friendlyName,false);
            }
            powerModeChangeActive = false;
        }

        void XCastImplementation::networkStandbyModeChangeEvent(void)
        {
            LOGINFO("m_networkStandbyMode:%u ",m_networkStandbyMode);
            SetNetworkStandbyMode(m_networkStandbyMode);
        }

        void XCastImplementation::onXcastApplicationLaunchRequestWithParam (string appName, string strPayLoad, string strQuery, string strAddDataUrl)
        {
            LOGINFO("Notify LaunchRequestWithParam, appName: %s, strPayLoad: %s, strQuery: %s, strAddDataUrl: %s",
                    appName.c_str(),strPayLoad.c_str(),strQuery.c_str(),strAddDataUrl.c_str());
            JsonObject params;
            params["appName"]  = appName.c_str();
            params["strPayLoad"]  = strPayLoad.c_str();
            params["strQuery"]  = strQuery.c_str();
            params["strAddDataUrl"]  = strAddDataUrl.c_str();
            dispatchEvent(LAUNCH_REQUEST_WITH_PARAMS, "", params);
        }

        void XCastImplementation::onXcastApplicationLaunchRequest(string appName, string parameter)
        {
            LOGINFO("Notify LaunchRequest, appName: %s, parameter: %s",appName.c_str(),parameter.c_str());
            JsonObject params;
            params["appName"]  = appName.c_str();
            params["parameter"]  = parameter.c_str();
            dispatchEvent(LAUNCH_REQUEST, "", params);
        }

        void XCastImplementation::onXcastApplicationStopRequest(string appName, string appId)
        {
            LOGINFO("Notify StopRequest, appName: %s, appId: %s",appName.c_str(),appId.c_str());
            JsonObject params;
            params["appName"]  = appName.c_str();
            params["appId"]  = appId.c_str();
            dispatchEvent(STOP_REQUEST, "", params);
        }

        void XCastImplementation::onXcastApplicationHideRequest(string appName, string appId)
        {
            LOGINFO("Notify StopRequest, appName: %s, appId: %s",appName.c_str(),appId.c_str());
            JsonObject params;
            params["appName"]  = appName.c_str();
            params["appId"]  = appId.c_str();
            dispatchEvent(HIDE_REQUEST, "", params);
        }

        void XCastImplementation::onXcastApplicationResumeRequest(string appName, string appId)
        {
            LOGINFO("Notify StopRequest, appName: %s, appId: %s",appName.c_str(),appId.c_str());
            JsonObject params;
            params["appName"]  = appName.c_str();
            params["appId"]  = appId.c_str();
            dispatchEvent(RESUME_REQUEST, "", params);
        }

        void XCastImplementation::onXcastApplicationStateRequest(string appName, string appId)
        {
            LOGINFO("Notify StopRequest, appName: %s, appId: %s",appName.c_str(),appId.c_str());
            JsonObject params;
            params["appName"]  = appName.c_str();
            params["appId"]  = appId.c_str();
            dispatchEvent(STATE_REQUEST, "", params);
        }

        void XCastImplementation::onXcastUpdatePowerStateRequest(string powerState)
        {
            LOGINFO("Notify updatePowerState, state: %s",powerState.c_str());
            JsonObject params;
            params["powerstate"]  = powerState.c_str();
            setPowerState(powerState);
        }

        void XCastImplementation::onGDialServiceStopped(void)
        {
            LOGINFO("Timer triggered to monitor the GDial, check after 5sec");
            startTimer(LOCATE_CAST_FIRST_TIMEOUT_IN_MILLIS);
        }

        bool XCastImplementation::connectToGDialService(void)
        {
            LOGINFO("Entering ...");
            std::string interface,ipaddress;
            bool status = false;

            LOGINFO("TRACE");
            getDefaultNameAndIPAddress(interface,ipaddress);
            LOGINFO("TRACE");
            if (!interface.empty())
            {
                LOGINFO("TRACE");
                status = m_xcast_manager->initialize(interface,m_networkStandbyMode);
                LOGINFO("TRACE");
                if( true == status)
                {
                    m_activeInterfaceName = getInterfaceNameToType(interface);
                }
                LOGINFO("TRACE");
            }
            LOGINFO("GDialService[%u]IF[%s]IP[%s]",status,interface.c_str(),ipaddress.c_str());
            LOGINFO("Exiting ...");
            return status;
        }

        string XCastImplementation::getInterfaceNameToType(const string & interface)
        {
            if(interface == "wlan0")
                return string("WIFI");
            else if(interface == "eth0")
                return string("ETHERNET");
            return string("");
        }

        bool XCastImplementation::getDefaultNameAndIPAddress(std::string& interface, std::string& ipaddress)
        {
            LOGINFO("Entering ...");
            bool returnValue = false;

            LOGINFO("TRACE");
            InitializeNetworkManager(_service);
            LOGINFO("TRACE");

            if (nullptr == _networkManagerPlugin)
            {
                LOGINFO("TRACE");
                LOGINFO("WARN::Unable to get Network plugin handle not yet");
                return false;
            }
            LOGINFO("TRACE");

            uint32_t rc = Core::ERROR_GENERAL;
            rc = _networkManagerPlugin->GetPrimaryInterface(interface);

            if (Core::ERROR_NONE != rc)
            {
                LOGERR("Failed to get Primary Interface from NM: %u",rc);
            }
            else
            {
                Exchange::INetworkManager::IPAddress address{};

                LOGINFO("Primary Interface is [%s]",interface.c_str());
                rc = _networkManagerPlugin->GetIPSettings(interface, "IPv4", address);

                if (Core::ERROR_NONE != rc)
                {
                    LOGERR("Failed to get IP Settings from NM: %u",rc);
                }
                else
                {
                    if (!address.ipaddress.empty())
                    {
                        ipaddress = address.ipaddress;
                        if ("IPv4" == address.ipversion)
                        {
                            if( 32 < address.prefix || 0 == address.prefix )
                            {
                                LOGERR("Invalid prefix %d", address.prefix);
                            }
                            else
                            {
                                returnValue = true;
                                LOGINFO("IPv4[%s] Prefix[%d] DHCP[%s]GW[%s]PriDNS[%s]SecDNS[%s]",
                                        address.ipaddress.c_str(),
                                        address.prefix,
                                        address.dhcpserver.c_str(),
                                        address.gateway.c_str(),
                                        address.primarydns.c_str(),
                                        address.secondarydns.c_str());
                            }
                        }
                        else
                        {
                            LOGWARN("Non IPv4 Address returned");
                        }
                    }
                }
            }
            LOGINFO("Exiting ...");
            return returnValue;
        }

        void XCastImplementation::updateNWConnectivityStatus(std::string nwInterface, bool nwConnected, std::string ipaddress)
        {
            bool status = false;
            if(nwConnected)
            {
                std::string mappedInterface = getInterfaceNameToType(nwInterface);
                if(mappedInterface.compare("ETHERNET")==0){
                    LOGINFO("Connectivity type Ethernet");
                    status = true;
                }
                else if(mappedInterface.compare("WIFI")==0){
                    LOGINFO("Connectivity type WIFI");
                    status = true;
                }
                else{
                    LOGERR("Connectivity type Unknown");
                }
            }
            else
            {
                LOGERR("Connectivity type Unknown");
            }
            if (!m_locateCastTimer.isActive())
            {
                if (status)
                {
                    if ((0 != mappedInterface.compare(m_activeInterfaceName)) ||
                        ((0 == mappedInterface.compare(m_activeInterfaceName)) && !ipaddress.empty()))
                    {
                        if (m_xcast_manager)
                        {
                            LOGINFO("Stopping GDialService");
                            m_xcast_manager->deinitialize();
                        }
                        LOGINFO("Timer started to monitor active interface");
                        startTimer(LOCATE_CAST_FIRST_TIMEOUT_IN_MILLIS);
                    }
                }
            }
        }

        void XCastImplementation::onLocateCastTimer()
        {
            LOGINFO("Timer Entrying ...");
            {
                lock_guard<mutex> lck(m_TimerMutexSync);
                if( false == connectToGDialService())
                {
                    LOGINFO("TRACE");
                    LOGINFO("Retry after 10 sec...");
                    m_locateCastTimer.setInterval(LOCATE_CAST_SECOND_TIMEOUT_IN_MILLIS);
                    LOGINFO("Timer Exiting ...");
                    return ;
                }
                LOGINFO("TRACE");
                stopTimer();
            }
            LOGINFO("TRACE");

            if ((NULL != m_xcast_manager) && m_isDynamicRegistrationsRequired )
            {
                LOGINFO("TRACE");
                std::vector<DynamicAppConfig*> appConfigList;
                lock_guard<mutex> lck(m_appConfigMutex);
                appConfigList = m_appConfigCache;
                dumpDynamicAppCacheList(string("CachedAppsFromTimer"), appConfigList);
                LOGINFO("> calling registerApplications");
                LOGINFO("TRACE");
                m_xcast_manager->registerApplications (appConfigList);
                LOGINFO("TRACE");
            }
            else {
                LOGINFO("m_xcast_manager: %p: m_isDynamicRegistrationsRequired[%u]",m_xcast_manager,m_isDynamicRegistrationsRequired);
            }
            LOGINFO("TRACE");
            m_xcast_manager->enableCastService(friendlyNameCache,xcastEnableCache);
            LOGINFO("TRACE");
            LOGINFO("XCastImplementation::onLocateCastTimer : Timer still active ? %d ",m_locateCastTimer.isActive());
            LOGINFO("Timer Exiting ...");
        }

        uint32_t XCastImplementation::enableCastService(string friendlyname,bool enableService) 
        {
            LOGINFO("ARGS = %s : %d", friendlyname.c_str(), enableService);
            if (nullptr != m_xcast_manager)
            {
                LOGINFO("m_xcast_manager  :%s",friendlyname.c_str());
                m_xcast_manager->enableCastService(friendlyname,enableService);
                LOGINFO("m_xcast_manager  :%s",friendlyname.c_str());
            }
            xcastEnableCache = enableService;
            friendlyNameCache = std::move(friendlyname);
            return 0;
        }

        void XCastImplementation::startTimer(int interval)
        {
            LOGINFO("Entering ...");
            stopTimer();
            LOGINFO("TRACE");
            m_locateCastTimer.start(interval);
            LOGINFO("TRACE");
            LOGINFO("Exiting ...");
        }

        void XCastImplementation::stopTimer()
        {
            LOGINFO("Entering ...");
            if (m_locateCastTimer.isActive())
            {
                LOGINFO("TRACE");
                m_locateCastTimer.stop();
                LOGINFO("TRACE");
            }
            LOGINFO("Exiting ...");
        }
        bool XCastImplementation::isTimerActive()
        {
            return (m_locateCastTimer.isActive());
        }
        
        void XCastImplementation::dispatchEvent(Event event, string callsign, const JsonObject &params)
        {
            Core::IWorkerPool::Instance().Submit(Job::Create(this, event, callsign, params));
        }

        void XCastImplementation::Dispatch(Event event, string callsign, const JsonObject params)
        {
            _adminLock.Lock();
            
            LOGINFO("Event = %d, callsign = %s", event, callsign.c_str());
            std::list<Exchange::IXCast::INotification*>::iterator index(_xcastNotification.begin());
            while (index != _xcastNotification.end())
            {
                switch(event)
                {
                    case LAUNCH_REQUEST_WITH_PARAMS:
                    {
                        string appName = params["appName"].String();
                        string strPayLoad = params["strPayLoad"].String();
                        string strQuery = params["strQuery"].String();
                        string strAddDataUrl = params["strAddDataUrl"].String();
                        (*index)->OnApplicationLaunchRequestWithParam(appName,strPayLoad,strQuery,strAddDataUrl);
                    }
                    break;
                    case LAUNCH_REQUEST:
                    {
                        string appName = params["appName"].String();
                        string parameter = params["parameter"].String();
                        (*index)->OnApplicationLaunchRequest(appName,parameter);
                    }
                    break;
                    case STOP_REQUEST:
                    {
                        string appName = params["appName"].String();
                        string appId = params["appId"].String();
                        (*index)->OnApplicationStopRequest(appName,appId);
                    }
                    break;
                    case HIDE_REQUEST:
                    {
                        string appName = params["appName"].String();
                        string appId = params["appId"].String();
                        (*index)->OnApplicationHideRequest(appName,appId);
                    }
                    break;
                    case STATE_REQUEST:
                    {
                        string appName = params["appName"].String();
                        string appId = params["appId"].String();
                        (*index)->OnApplicationStateRequest(appName,appId);
                    }
                    break;
                    case RESUME_REQUEST:
                    {
                        string appName = params["appName"].String();
                        string appId = params["appId"].String();
                        (*index)->OnApplicationResumeRequest(appName,appId);
                    }
                    break;
                    default: break;
                }
                ++index;
            }

            _adminLock.Unlock();
        }

        std::string XCastImplementation::getSecurityToken()
        {
            if (nullptr == _service)
            {
                return (std::string(""));
            }

            std::string token;
            auto security = _service->QueryInterfaceByCallsign<PluginHost::IAuthenticate>("SecurityAgent");
            if (nullptr != security)
            {
                std::string payload = "http://localhost";
                if (security->CreateToken(static_cast<uint16_t>(payload.length()),
                                            reinterpret_cast<const uint8_t *>(payload.c_str()),
                                            token) == Core::ERROR_NONE)
                {
                    LOGINFO("got security token - %s", token.empty() ? "" : token.c_str());
                }
                else
                {
                    LOGERR("failed to get security token");
                }
                security->Release();
            }
            else
            {
                LOGERR("No security agent\n");
            }

            std::string query = "token=" + token;
            Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), (_T(SERVER_DETAILS)));
            return query;
        }

        void XCastImplementation::dumpDynamicAppCacheList(string strListName, std::vector<DynamicAppConfig*>& appConfigList)
        {
            LOGINFO ("=================Current Apps[%s] size[%d] ===========================", strListName.c_str(), (int)appConfigList.size());
            for (DynamicAppConfig* pDynamicAppConfig : appConfigList)
            {
                LOGINFO ("Apps: appName:%s, prefixes:%s, cors:%s, allowStop:%d, query:%s, payload:%s",
                            pDynamicAppConfig->appName,
                            pDynamicAppConfig->prefixes,
                            pDynamicAppConfig->cors,
                            pDynamicAppConfig->allowStop,
                            pDynamicAppConfig->query,
                            pDynamicAppConfig->payload);
            }
            LOGINFO ("=================================================================");
        }  

        Core::hresult XCastImplementation::SetApplicationState(const string& applicationName, const Exchange::IXCast::State& state, const string& applicationId, const Exchange::IXCast::ErrorCode& error,Exchange::IXCast::XCastSuccess &success)
        {
            LOGINFO("ARGS = %s : %s : %d : %d ", applicationName.c_str(), applicationId.c_str() , state , error);
            success.success = false;
            uint32_t status = Core::ERROR_GENERAL;
            if(!applicationName.empty() && (nullptr != m_xcast_manager))
            {
                LOGINFO("XCastImplementation::SetApplicationState  ARGS = %s : %s : %d : %d ", applicationName.c_str(), applicationId.c_str() , state , error);
                string appstate = "";
                if (state == Exchange::IXCast::State::RUNNING)
                {
                    appstate = "running";
                }
                else if (state == Exchange::IXCast::State::STOPPED)
                {
                    appstate = "stopped";
                }
                else if(state == Exchange::IXCast::State::HIDDEN)
                {
                    appstate = "suspended";
                }

                string errorStr = "";
                if (error == Exchange::IXCast::ErrorCode::NONE)
                {
                    errorStr = "none";
                }
                else if (error == Exchange::IXCast::ErrorCode::FORBIDDEN)
                {
                    errorStr = "forbidden";
                }
                else if (error == Exchange::IXCast::ErrorCode::UNAVAILABLE)
                {
                    errorStr = "unavailable";
                }
                else if (error == Exchange::IXCast::ErrorCode::INVALID)
                {
                    errorStr = "invalid";
                }
                else if (error == Exchange::IXCast::ErrorCode::INTERNAL)
                {
                    errorStr = "internal";
                }
                else
                {
                    LOGERR("Invalid Error Code [%u]",error);
                    return Core::ERROR_GENERAL;
                }

                m_xcast_manager->applicationStateChanged(applicationName.c_str(), appstate.c_str(), applicationId.c_str(), errorStr.c_str());
                success.success = true;
                status = Core::ERROR_NONE;
            }
            else
            {
                LOGERR("XCastImplementation::SetApplicationState - m_xcast_manager is NULL");
            }
            return status;
        }

    	Core::hresult XCastImplementation::GetProtocolVersion(string &protocolVersion , bool &success)
        {
            LOGINFO("XCastImplementation::getProtocolVersion");
            success = false;
            if (nullptr != m_xcast_manager)
            {
                LOGINFO("m_xcast_manager is not null");
                protocolVersion = m_xcast_manager->getProtocolVersion();
                success = true;
            }
            return Core::ERROR_NONE;
        }

        Core::hresult XCastImplementation::SetNetworkStandbyMode(bool networkStandbyMode)
        {
            LOGINFO("nwStandbymode: %d", networkStandbyMode);
            if (nullptr != m_xcast_manager)
            {
                m_xcast_manager->setNetworkStandbyMode(networkStandbyMode);
                m_networkStandbyMode = networkStandbyMode;
            }
            return 0;
        }

        Core::hresult XCastImplementation::SetManufacturerName(const string &manufacturername, Exchange::IXCast::XCastSuccess &success)
        {
            uint32_t status = Core::ERROR_GENERAL;
            LOGINFO("ManufacturerName : %s", manufacturername.c_str());
            success.success = false;
            if (nullptr != m_xcast_manager)
            {
                m_xcast_manager->setManufacturerName(manufacturername);
                 success.success = true;
                status = Core::ERROR_NONE;
            }
            return status;
        }

        Core::hresult XCastImplementation::GetManufacturerName(string &manufacturername , bool &success)
        {
            LOGINFO("XCastImplementation:getManufacturerName");
            if (nullptr != m_xcast_manager)
            {
                manufacturername = m_xcast_manager->getManufacturerName();
                LOGINFO("Manufacturer[%s]", manufacturername.c_str());
                success = true;
            }
            else
            {
                LOGINFO("XCastImplementation::getManufacturerName m_xcast_manager is NULL");
                success = false;
                return Core::ERROR_GENERAL;
            }
            return Core::ERROR_NONE;
        }

        Core::hresult XCastImplementation::SetModelName(const string &modelname, Exchange::IXCast::XCastSuccess &success)
        { 
            uint32_t status = Core::ERROR_GENERAL;
            success.success = false;
            LOGINFO("ModelName : %s", modelname.c_str());

            if (nullptr != m_xcast_manager)
            {
                m_xcast_manager->setModelName(modelname);
                 success.success = true;
                status = Core::ERROR_NONE;
            }
            return status;
        }

        Core::hresult XCastImplementation::GetModelName(string &modelname , bool &success)
        { 
            LOGINFO("XCastImplementation::getModelName");
            if (nullptr != m_xcast_manager)
            {
                modelname = m_xcast_manager->getModelName();
                LOGINFO("Model[%s]", modelname.c_str());
            }
            else
            {
                LOGINFO("XCastImplementation::getModelName m_xcast_manager is NULL");
                return Core::ERROR_GENERAL;
            }
            success = true;
            return Core::ERROR_NONE;
        }

        Core::hresult XCastImplementation::SetEnabled(const bool& enabled, Exchange::IXCast::XCastSuccess &success)
        {
            LOGINFO("setEnabled [%d]",enabled);
            bool isEnabled = false;
            bool currentNetworkStandbyMode = m_networkStandbyMode;

            m_xcastEnable= enabled;
            success.success = false;
            if ((!_registeredPowerEventHandlers) && (enabled))
            {
                checkPowerAndNetworkStandbyStates();
            }

            if (m_xcastEnable && ( (m_standbyBehavior == true) || ((m_standbyBehavior == false)&&(m_powerState == WPEFramework::Exchange::IPowerManager::POWER_STATE_ON))))
            {
                isEnabled = true;
                registerPowerEventHandlers();
            }
            else
            {
                unregisterPowerEventHandlers();
            }
            LOGINFO("XCastImplementation::setEnabled : %d, enabled : %d" , m_xcastEnable, isEnabled);
            enableCastService(m_friendlyName,isEnabled);
            if (currentNetworkStandbyMode != m_networkStandbyMode) {
                SetNetworkStandbyMode(m_networkStandbyMode);
            }
            success.success = true;
            return Core::ERROR_NONE;
        }

        Core::hresult XCastImplementation::GetEnabled(bool &enabled , bool &success )
        {
            LOGINFO("XCastImplementation::getEnabled - %d",m_xcastEnable);
            enabled = m_xcastEnable;
            success = true;
            return Core::ERROR_NONE;
         }
       
	Core::hresult XCastImplementation::SetStandbyBehavior(const Exchange::IXCast::StandbyBehavior &standbybehavior, Exchange::IXCast::XCastSuccess &success) { 
            LOGINFO("XCastImplementation::setStandbyBehavior\n");
             success.success = false;
            bool enabled = false;
            if (standbybehavior == Exchange::IXCast::StandbyBehavior::ACTIVE)
            {
                enabled = true;
            }
            else if (standbybehavior == Exchange::IXCast::StandbyBehavior::INACTIVE)
            {
                enabled = false;
            }
            else
            {
                LOGERR("XCastImplementation::setStandbyBehavior - Invalid standby behavior ");
                return Core::ERROR_GENERAL;
            }
            m_standbyBehavior = enabled;
             success.success = true;
            LOGINFO("XCastImplementation::setStandbyBehavior m_standbyBehavior : %d", m_standbyBehavior);
            return Core::ERROR_NONE;
        }
	Core::hresult XCastImplementation::GetStandbyBehavior(Exchange::IXCast::StandbyBehavior &standbybehavior, bool &success) { 
            LOGINFO("XCastImplementation::getStandbyBehavior m_standbyBehavior :%d",m_standbyBehavior);
            if(m_standbyBehavior)
                standbybehavior = Exchange::IXCast::StandbyBehavior::ACTIVE;
            else
                standbybehavior = Exchange::IXCast::StandbyBehavior::INACTIVE;
            success = true;
            return Core::ERROR_NONE;
        }

	    Core::hresult XCastImplementation::SetFriendlyName(const string& friendlyname, Exchange::IXCast::XCastSuccess &success)
        { 
            LOGINFO("XCastImplementation::setFriendlyName - %s", friendlyname.c_str());
            uint32_t result = Core::ERROR_GENERAL;
            
            success.success = false;
            bool enabledStatus = false;
            if (!friendlyname.empty())
            {
                m_friendlyName = friendlyname;
                LOGINFO("XCastImplementation::setFriendlyName  :%s",m_friendlyName.c_str());
                if (m_xcastEnable && ( (m_standbyBehavior == true) || ((m_standbyBehavior == false)&&(m_powerState == WPEFramework::Exchange::IPowerManager::POWER_STATE_ON))))
                    {
                        enabledStatus = true;                
                    }
                    else
                    {
                        enabledStatus = false;
                    }
                    LOGINFO("XCastImplementation::setFriendlyName  :%s",m_friendlyName.c_str());
                    enableCastService(m_friendlyName,enabledStatus);
                    LOGINFO("XCastImplementation::setFriendlyName  :%s",m_friendlyName.c_str());
                     success.success = true;
                    result = Core::ERROR_NONE;
            }
            return result;
        }

    	Core::hresult XCastImplementation::GetFriendlyName(string &friendlyname , bool &success )
        { 
            LOGINFO("XCastImplementation::getFriendlyName :%s ",m_friendlyName.c_str());
            friendlyname = m_friendlyName;
            success = true;
            return Core::ERROR_NONE;
        }

        bool XCastImplementation::deleteFromDynamicAppCache(vector<string>& appsToDelete) {
            LOGINFO("XCastImplementation::deleteFromDynamicAppCache");
            bool ret = true;
            {lock_guard<mutex> lck(m_appConfigMutex);
                /*Check if existing cache need to be updated*/
                std::vector<int> entriesTodelete;
                for (string appNameToDelete : appsToDelete) {
                    bool found = false;
                    int index = 0;
                    for (DynamicAppConfig* pDynamicAppConfigOld : m_appConfigCache) {
                        if (0 == strcmp(pDynamicAppConfigOld->appName, appNameToDelete.c_str())){
                            entriesTodelete.push_back(index);
                            found = true;
                            break;
                        }
                        index ++;
                    }
                    if (!found) {
                        LOGINFO("%s not existing in the dynamic cache", appNameToDelete.c_str());
                    }
                }
                std::sort(entriesTodelete.begin(), entriesTodelete.end(), std::greater<int>());
                for (int indexToDelete : entriesTodelete) {
                    LOGINFO("Going to delete the entry: %d from m_appConfigCache  size: %d", indexToDelete, (int)m_appConfigCache.size());
                    //Delete the old unwanted item here.
                    DynamicAppConfig* pDynamicAppConfigOld = m_appConfigCache[indexToDelete];
                    m_appConfigCache.erase (m_appConfigCache.begin()+indexToDelete);
                    free (pDynamicAppConfigOld); pDynamicAppConfigOld = NULL;
                }
                entriesTodelete.clear();

            }
            //Even if requested app names not there return true.
            return ret;
        }
        void XCastImplementation::updateDynamicAppCache(Exchange::IXCast::IApplicationInfoIterator* const appInfoList)
        {
            LOGINFO("XcastService::UpdateDynamicAppCache");

            std::vector <DynamicAppConfig*> appConfigList;
            if (appInfoList != nullptr)
            {
                LOGINFO("Applications:");
                Exchange::IXCast::ApplicationInfo appInfo;
                while (appInfoList->Next(appInfo))
                {
                    LOGINFO("Application: %s", appInfo.appName.c_str());
                    DynamicAppConfig* pDynamicAppConfig = (DynamicAppConfig*) malloc (sizeof(DynamicAppConfig));
                    if(pDynamicAppConfig)
                    {
                        memset ((void*)pDynamicAppConfig, '\0', sizeof(DynamicAppConfig));
                    
                        memset (pDynamicAppConfig->appName, '\0', sizeof(pDynamicAppConfig->appName));
                        strncpy (pDynamicAppConfig->appName, appInfo.appName.c_str(), sizeof(pDynamicAppConfig->appName) - 1);
                        pDynamicAppConfig->appName[sizeof(pDynamicAppConfig->appName) - 1] = '\0';

                        memset (pDynamicAppConfig->prefixes, '\0', sizeof(pDynamicAppConfig->prefixes));
                        strncpy (pDynamicAppConfig->prefixes, appInfo.prefixes.c_str(), sizeof(pDynamicAppConfig->prefixes) - 1);
                        pDynamicAppConfig->prefixes[sizeof(pDynamicAppConfig->prefixes) - 1] = '\0';

                        memset (pDynamicAppConfig->cors, '\0', sizeof(pDynamicAppConfig->cors));
                        strncpy (pDynamicAppConfig->cors, appInfo.cors.c_str(), sizeof(pDynamicAppConfig->cors) - 1);
                        pDynamicAppConfig->cors[sizeof(pDynamicAppConfig->cors) - 1] = '\0';

                        memset (pDynamicAppConfig->query, '\0', sizeof(pDynamicAppConfig->query));
                        strncpy (pDynamicAppConfig->query, appInfo.query.c_str(), sizeof(pDynamicAppConfig->query) - 1);
                        pDynamicAppConfig->query[sizeof(pDynamicAppConfig->query) - 1] = '\0';

                        memset (pDynamicAppConfig->payload, '\0', sizeof(pDynamicAppConfig->payload));
                        strncpy (pDynamicAppConfig->payload, appInfo.payload.c_str(), sizeof(pDynamicAppConfig->payload) - 1);
                        pDynamicAppConfig->payload[sizeof(pDynamicAppConfig->payload) - 1] = '\0';

                        pDynamicAppConfig->allowStop = appInfo.allowStop ? true : false;

                        LOGINFO("appName:%s, prefixes:%s, cors:%s, allowStop:%d, query:%s, payload:%s",
                                pDynamicAppConfig->appName,
                                pDynamicAppConfig->prefixes,
                                pDynamicAppConfig->cors,
                                pDynamicAppConfig->allowStop,
                                pDynamicAppConfig->query,
                                pDynamicAppConfig->payload);
                        appConfigList.push_back (pDynamicAppConfig);
                    }
                    else
                    {
                        LOGINFO("Memory allocation failed for DynamicAppConfig");
                        return;
                    }
                }
            }
          
            dumpDynamicAppCacheList(string("appConfigList"), appConfigList);
            vector<string> appsToDelete;
            for (DynamicAppConfig* pDynamicAppConfig : appConfigList) {
                    appsToDelete.push_back(string(pDynamicAppConfig->appName));
            }
            deleteFromDynamicAppCache (appsToDelete);

            LOGINFO("appConfigList count: %d", (int)appConfigList.size());
            //Update the new entries here.
            {
                lock_guard<mutex> lck(m_appConfigMutex);
                for (DynamicAppConfig* pDynamicAppConfig : appConfigList) {
                    m_appConfigCache.push_back(pDynamicAppConfig);
                }
                LOGINFO("m_appConfigCache count: %d", (int)m_appConfigCache.size());
            }
            //Clear the tempopary list here
            appsToDelete.clear();
            appConfigList.clear();
            lock_guard<mutex> lck(m_appConfigMutex);
            dumpDynamicAppCacheList(string("m_appConfigCache"), m_appConfigCache);
            return;
        }

        Core::hresult XCastImplementation::RegisterApplications(Exchange::IXCast::IApplicationInfoIterator* const appInfoList, Exchange::IXCast::XCastSuccess &success) { 
            LOGINFO("XCastImplementation::registerApplications \n");
            enableCastService(m_friendlyName,false);
            m_isDynamicRegistrationsRequired = true;
            updateDynamicAppCache(appInfoList);
            std::vector<DynamicAppConfig*> appConfigList;
            {
                lock_guard<mutex> lck(m_appConfigMutex);
                appConfigList = m_appConfigCache;
            }
            dumpDynamicAppCacheList(string("m_appConfigCache"), appConfigList);
	        lock_guard<mutex> lck(m_appConfigMutex);
            //Pass the dynamic cache to xdial process
            m_xcast_manager->registerApplications(m_appConfigCache);

            /*Reenabling cast service after registering Applications*/
            if (m_xcastEnable && ( (m_standbyBehavior == true) || ((m_standbyBehavior == false)&&(m_powerState == WPEFramework::Exchange::IPowerManager::POWER_STATE_ON)) ) ) {
                LOGINFO("Enable CastService  m_xcastEnable: %d m_standbyBehavior: %d m_powerState:%d", m_xcastEnable, m_standbyBehavior, m_powerState);
                enableCastService(m_friendlyName,true);
            }
            else {
                LOGINFO("CastService not enabled m_xcastEnable: %d m_standbyBehavior: %d m_powerState:%d", m_xcastEnable, m_standbyBehavior, m_powerState);
            }
            success.success = true;
            return Core::ERROR_NONE;
        }
	Core::hresult XCastImplementation::UnregisterApplications(Exchange::IXCast::IStringIterator* const apps, Exchange::IXCast::XCastSuccess &success) 
        {
            LOGINFO("XcastService::unregisterApplications \n ");
            auto returnStatus = false;
            /*Disable cast service before registering Applications*/
            enableCastService(m_friendlyName,false);
            m_isDynamicRegistrationsRequired = true;

            std::vector<string> appsToDelete;
            string appName;
            while (apps->Next(appName))
            {
                LOGINFO("Going to delete the app: %s from dynamic cache", appName.c_str());
                appsToDelete.push_back(appName);
            }
            
            returnStatus = deleteFromDynamicAppCache(appsToDelete);

            std::vector<DynamicAppConfig*> appConfigList;
            {
                lock_guard<mutex> lck(m_appConfigMutex);
                appConfigList = m_appConfigCache;
            }
            dumpDynamicAppCacheList(string("m_appConfigCache"), appConfigList);
            m_xcast_manager->registerApplications(appConfigList);

            /*Reenabling cast service after registering Applications*/
            if (m_xcastEnable && ( (m_standbyBehavior == true) || ((m_standbyBehavior == false)&&(m_powerState == WPEFramework::Exchange::IPowerManager::POWER_STATE_ON)) ) ) {
                LOGINFO("Enable CastService  m_xcastEnable: %d m_standbyBehavior: %d m_powerState:%d", m_xcastEnable, m_standbyBehavior, m_powerState);
                enableCastService(m_friendlyName,true);
            }
            else {
                LOGINFO("CastService not enabled m_xcastEnable: %d m_standbyBehavior: %d m_powerState:%d", m_xcastEnable, m_standbyBehavior, m_powerState);
            }
            success.success = (returnStatus)? true : false;
            return (returnStatus)? Core::ERROR_NONE : Core::ERROR_GENERAL;
        }

        bool XCastImplementation::setPowerState(const string &powerState)
        {
            PowerState cur_powerState = m_powerState,
            new_powerState = WPEFramework::Exchange::IPowerManager::POWER_STATE_OFF;
            Core::hresult status = Core::ERROR_GENERAL;
            bool ret = true;
            if ("ON" == powerState )
            {
                new_powerState = WPEFramework::Exchange::IPowerManager::POWER_STATE_ON;
            }
            else if ("STANDBY" == powerState)
            {
                new_powerState = WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY;
            }
            else if ("TOGGLE" == powerState)
            {
                new_powerState = ( WPEFramework::Exchange::IPowerManager::POWER_STATE_ON == cur_powerState ) ? WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY : WPEFramework::Exchange::IPowerManager::POWER_STATE_ON;
            }

            if ((WPEFramework::Exchange::IPowerManager::POWER_STATE_OFF != new_powerState) && (cur_powerState != new_powerState))
            {
                ASSERT (_powerManagerPlugin);

                if (_powerManagerPlugin)
                {
                    status = _powerManagerPlugin->SetPowerState(0, new_powerState, "random");
                }

                if (status == Core::ERROR_GENERAL)
                {
                    ret = false;
                    LOGINFO("Failed to change power state [%d] -> [%d] ret[%x]",cur_powerState,new_powerState,ret);
                }
                else
                {
                    LOGINFO("changing power state [%d] -> [%d] success",cur_powerState,new_powerState);
                    sleep(m_sleeptime);
                }
            }
            return ret;
        }

        void XCastImplementation::getUrlFromAppLaunchParams (const char *app_name, const char *payload, const char *query_string, const char *additional_data_url, char *url)
        {
            LOGINFO("getUrlFromAppLaunchParams : Application launch request: appName: %s  query: [%s], payload: [%s], additionalDataUrl [%s]\n",
                app_name, query_string, payload, additional_data_url);

            int url_len = DIAL_MAX_PAYLOAD+DIAL_MAX_ADDITIONALURL+100;
            memset (url, '\0', url_len);
            if(strcmp(app_name,"YouTube") == 0) {
                if ((payload != NULL) && (additional_data_url != NULL)){
                    snprintf( url, url_len, "https://www.youtube.com/tv?%s&additionalDataUrl=%s", payload, additional_data_url);
                }else if (payload != NULL){
                    snprintf( url, url_len, "https://www.youtube.com/tv?%s", payload);
                }else{
                    snprintf( url, url_len, "https://www.youtube.com/tv");
                }
            }
            else if(strcmp(app_name,"YouTubeTV") == 0) {
                if ((payload != NULL) && (additional_data_url != NULL)){
                    snprintf( url, url_len, "https://www.youtube.com/tv/upg?%s&additionalDataUrl=%s", payload, additional_data_url);
                }else if (payload != NULL){
                    snprintf( url, url_len, "https://www.youtube.com/tv/upg?%s", payload);
                }else{
                    snprintf( url, url_len, "https://www.youtube.com/tv/upg?");
                }
            }
            else if(strcmp(app_name,"YouTubeKids") == 0) {
                if ((payload != NULL) && (additional_data_url != NULL)){
                    snprintf( url, url_len, "https://www.youtube.com/tv_kids?%s&additionalDataUrl=%s", payload, additional_data_url);
                }else if (payload != NULL){
                    snprintf( url, url_len, "https://www.youtube.com/tv_kids?%s", payload);
                }else{
                    snprintf( url, url_len, "https://www.youtube.com/tv_kids?");
                }
            }
            else if(strcmp(app_name,"Netflix") == 0) {
                memset( url, 0, url_len );
                strncat( url, "source_type=12", url_len - strlen(url) - 1);
                if(payload != NULL)
                {
                    const char * pUrlEncodedParams;
                    pUrlEncodedParams = payload;
                    if( pUrlEncodedParams ){
                        strncat( url, "&dial=", url_len - strlen(url) - 1);
                        strncat( url, pUrlEncodedParams, url_len - strlen(url) - 1);
                    }
                }

                if(additional_data_url != NULL){
                    strncat(url, "&additionalDataUrl=", url_len - strlen(url) - 1);
                    strncat(url, additional_data_url, url_len - strlen(url) - 1);
                }
            }
            else {
                memset( url, 0, url_len );
                url_len -= DIAL_MAX_ADDITIONALURL+1; //save for &additionalDataUrl
                url_len -= 1; //save for nul byte
                LOGINFO("query_string=[%s]\r\n", query_string);
                int has_query = query_string && strlen(query_string);
                int has_payload = 0;
                if (has_query) {
                    snprintf(url + strlen(url), url_len, "%s", query_string);
                    url_len -= strlen(query_string);
                }
                if(payload && strlen(payload)) {
                    const char payload_key[] = "dialpayload=";
                    if(url_len >= 0){
                        if (has_query) {
                            snprintf(url + strlen(url), url_len, "%s", "&");
                            url_len -= 1;
                        }
                        if(url_len >= 0) {
                            snprintf(url + strlen(url), url_len, "%s%s", payload_key, payload);
                            url_len -= strlen(payload_key) + strlen(payload);
                            has_payload = 1;
                        }
                    }
                    else {
                        LOGINFO("there is not enough room for payload\r\n");
                    }
                }
                
                if(additional_data_url != NULL){
                    if ((has_query || has_payload) && url_len >= 0) {
                        snprintf(url + strlen(url), url_len, "%s", "&");
                        url_len -= 1;
                    }
                    if (url_len >= 0) {
                        snprintf(url + strlen(url), url_len, "additionalDataUrl=%s", additional_data_url);
                        url_len -= strlen(additional_data_url) + 18;
                    }
                }
                LOGINFO(" url is [%s]\r\n", url);
            }
        }
        
    } // namespace Plugin
} // namespace WPEFramework
