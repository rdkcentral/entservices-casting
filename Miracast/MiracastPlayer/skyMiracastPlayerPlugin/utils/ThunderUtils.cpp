/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
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
#include "ThunderUtils.h"
#include "RDKTextToSpeech.h"
#include "MiracastPlayerApplication.hpp"
#include "MiracastPlayerDeviceProperties.h"
#include "MiracastPlayerLogging.hpp"
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace WPEFramework;

#define SECURITY_TOKEN_LEN_MAX 1024
#define THUNDER_RPC_TIMEOUT 5000
#define MIRACASTPLAYER_CALLSIGN "MiracastPlayer"
std::string client_id_;
std::mutex thunderUtilsMutex_;
ThunderUtils *ThunderUtils::_instance(nullptr);

ThunderUtils::ThunderUtils()
{
#ifndef SKY_BUILD // (SKY_BUILD_ENV != 1)
    Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), (_T("127.0.0.1:9998")));
    MIRACASTLOG_INFO(" Thunder plugins setEnvironment\n");
#else 
    MIRACASTLOG_INFO(" Thunder plugins not setEnvironment\n");
#endif
    /* Create Thunder Security token */
    unsigned char buffer[SECURITY_TOKEN_LEN_MAX] = {0};
    int ret = GetSecurityToken(SECURITY_TOKEN_LEN_MAX, buffer);
    string sToken;
    string query;
    if (ret > 0)
    {
        sToken = (char *)buffer;
        query = "token=" + sToken;
    }
    controller = new WPEFramework::JSONRPC::LinkType<Core::JSON::IElement>("", "", false, query);
    registerPlugins();
    for (Plugins::iterator it=plugins.begin(); it!=plugins.end(); it++)
    {
        (it->second).obj = new WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>(it->first.c_str(), "", false, query);
        if ( (it->second).obj != nullptr)
        {
            /* HOMEKITTV_CALLSIGN  will activate as part of service */
            if (it->first == HOMEKITTV_CALLSIGN) {
                continue;
            }
            //Activate plugin
            JsonObject result, params;
            params["callsign"] = it->first.c_str();
            int rpcRet = controller->Invoke("activate", params, result);
            if (rpcRet == Core::ERROR_NONE)
                MIRACASTLOG_VERBOSE("Activated %s plugin", it->first.c_str());
            else
               MIRACASTLOG_ERROR("Could not activate %s plugin.  Failed with %d", it->first.c_str(), rpcRet);
            //register for events
            Event events = (it->second).events;
            for (Event::iterator evtItr=events.begin(); evtItr!=events.end(); evtItr++)
            {
                int evRet = (it->second).obj->Subscribe<JsonObject>(THUNDER_RPC_TIMEOUT, evtItr->first.c_str(), evtItr->second);
                if (evRet == Core::ERROR_NONE)
                    MIRACASTLOG_VERBOSE("%s - Updated event %s", it->first.c_str(), evtItr->first.c_str());
                else
                   MIRACASTLOG_ERROR("%s - failed to subscribe %s", it->first.c_str(), evtItr->first.c_str());
            }
        }
    }
#ifdef SKY_BUILD // (SKY_BUILD_ENV != 1)
    updateAppIPtoDaemon();
#endif
}

ThunderUtils::~ThunderUtils()
{
    MIRACASTLOG_VERBOSE("ThunderUtils::~ThunderUtils", MIRACASTPLAYER_APP_LOG);
    for (Plugins::iterator it = plugins.begin(); it != plugins.end(); it++)
    {
        Event events = (it->second).events;
        for (Event::iterator evtItr = events.begin(); evtItr != events.end(); evtItr++)
            (it->second).obj->Unsubscribe(1000, evtItr->first.c_str());
        delete (it->second).obj;
        (it->second).obj = 0;
    }
}

ThunderUtils *ThunderUtils::getinstance()
{
    const std::lock_guard<std::mutex> lock(thunderUtilsMutex_);
    if (_instance == nullptr)
    {
        _instance = new ThunderUtils;
    }
    return _instance;
}
void ThunderUtils::thunderInvoke(const std::string &callsign, const std::string &method, const JsonObject &param, JsonObject &result)
{
    JsonObject response;
    if (plugins[callsign].obj != nullptr)
    {
        uint32_t ret = plugins[callsign].obj->Invoke<JsonObject, JsonObject>(THUNDER_RPC_TIMEOUT, method.c_str(), param, response);
        if (ret == Core::ERROR_NONE)
        {
            MIRACASTLOG_VERBOSE("%s -%s call success", callsign.c_str(), method.c_str());
            result = response;
        }
        else
        {
            MIRACASTLOG_ERROR("%s -%s call Failed", callsign.c_str(), method.c_str());
        }
    }
}

void ThunderUtils::thunderInvoke(const std::string &callsign, const std::string &method, JsonObject &result)
{
    JsonObject response;
    if (plugins[callsign].obj != nullptr)
    {
        uint32_t ret = plugins[callsign].obj->Invoke<void, JsonObject>(THUNDER_RPC_TIMEOUT, method.c_str(), response);
        if (ret == Core::ERROR_NONE)
        {
            MIRACASTLOG_VERBOSE("%s -%s call success", callsign.c_str(), method.c_str());
            result = response;
        }
        else
        {
            MIRACASTLOG_ERROR("%s -%s call failed", callsign.c_str(), method.c_str());
        }
    }
}

void ThunderUtils::thunderSet(const std::string &callsign, const std::string &method, const JsonObject &param)
{
    string result = "";
    if (plugins[callsign].obj != nullptr)
    {
        uint32_t ret = plugins[callsign].obj->Set(THUNDER_RPC_TIMEOUT, method.c_str(), param);
        if (ret == Core::ERROR_NONE)
        {
            MIRACASTLOG_VERBOSE("%s -%s call success", callsign.c_str(), method.c_str());
        }
        else
        {
           MIRACASTLOG_ERROR("%s -%s call failed", callsign.c_str(), method.c_str());
        }
    }
}
void ThunderUtils::thunderGet(const std::string &callsign, const std::string &method, auto &result)
{
   if (plugins[callsign].obj != nullptr)
    {
        uint32_t ret = plugins[callsign].obj->Get(THUNDER_RPC_TIMEOUT, method.c_str(), result);
        if (ret == Core::ERROR_NONE)
        {
            MIRACASTLOG_INFO("%s -%s call success", callsign.c_str(), method.c_str());
        }
        else
        {
            MIRACASTLOG_INFO("%s -%s call failed", callsign.c_str(), method.c_str());
        }
    }
}
string ThunderUtils::getAudioFormat(std::string AudioFormats)
{
    JsonObject params, result;
    thunderInvoke(DISPLAYSETTINGS_CALLSIGN, "getAudioFormat", params, result);
    if(result["success"].Boolean())
    {
        string sName = result["supportedAudioFormat"].String().c_str();
        AudioFormats = sName.c_str();
        MIRACASTLOG_VERBOSE("getAudioFormat call success", MIRACASTPLAYER_APP_LOG);
        return AudioFormats;
    }
    else
    {
        MIRACASTLOG_VERBOSE("getAudioFormat call failed", MIRACASTPLAYER_APP_LOG);
        return AudioFormats;
    }
}
string ThunderUtils::getColorFormat(std::string ColorFormats)
{
    JsonObject params, result;
    thunderInvoke(DISPLAYSETTINGS_CALLSIGN, "getVideoFormat", params, result);
    if(result["success"].Boolean())
    {
        string sName = result["supportedVideoFormat"].String().c_str();
        ColorFormats = sName.c_str();
        MIRACASTLOG_VERBOSE("getColorFormats call success", MIRACASTPLAYER_APP_LOG);
        return ColorFormats;
    }
    else
    {
        MIRACASTLOG_VERBOSE("getColorFormats call failed", MIRACASTPLAYER_APP_LOG);
	return ColorFormats;
    }
}

std::string ThunderUtils::getDeviceLocation()
{
    JsonObject result;
    std::string deviceLocation;
    thunderGet(LOCATION_CALLSIGN, "location", result);
    if(result.HasLabel("country"))
    {
        deviceLocation = result["country"].String();
        MIRACASTLOG_VERBOSE(" getDeviceLocation call success, location:%s", deviceLocation.c_str());
    }
    else {
        MIRACASTLOG_VERBOSE(" getDeviceLocation call failed");
    }
    return deviceLocation;
}

std::string ThunderUtils::getSystemLocale()
{
    JsonObject result;
    std::string systemLocale;
    thunderInvoke(USER_PREFERENCES_CALLSIGN,"getUILanguage", result);
    if(result["success"].Boolean() && result.HasLabel("ui_language")){
        systemLocale = result["ui_language"].String();
        MIRACASTLOG_VERBOSE(" getSystemLocale call success, Locale:%s", systemLocale.c_str());
    }
    else {
        MIRACASTLOG_VERBOSE(" getSystemLocale call failed");
    }
    return systemLocale;
}

std::string ThunderUtils::getModelName()
{
    JsonObject result;
    std::string modelName;
    thunderGet(DEVICE_INFO_CALLSIGN, "modelname", result);
    if (result.HasLabel("model"))
    {
        modelName = result["model"].String();
        MIRACASTLOG_INFO("getModelName call success: %s", modelName.c_str());
    }
    else {
        MIRACASTLOG_INFO("getModelName call failed");
    }
    return modelName;
}
void ThunderUtils::getResolution(char* resolution, int sLen){
    Core::JSON::String response;
    thunderGet(PLAYER_INFO_CALLSIGN, "resolution",response);
    string result = response.Value();
    int resultlength = result.size();
    int bytesToCopy = std::min(sLen, resultlength);
    strncpy(resolution, result.c_str(), bytesToCopy);
    resolution[bytesToCopy] = '\0';
}
void ThunderUtils::getActiveAudioPorts(string &activeAudioPort)
{
  JsonObject params, result;
  std::string audioPort = "";
  thunderInvoke(DISPLAYSETTINGS_CALLSIGN, "getConnectedAudioPorts", params, result);
  if (result["connectedAudioPorts"].Content() == JsonValue::type::ARRAY)
    {
    JsonArray audioPortsArray = result["connectedAudioPorts"].Array();
    for(int i= 0 ; i < audioPortsArray.Length() ; i++)
    {
        JsonObject audioPort_Obj = audioPortsArray[i].Object();
        audioPort = audioPortsArray[i].String().c_str();
        bool active_AudioPort = getEnableAudioPort(audioPort);
        if(active_AudioPort)
        {
                if((audioPort.compare("HDMI_ARC0")==0))
                {
                    activeAudioPort=audioPort;
                }
        }
    }
    MIRACASTLOG_VERBOSE("getActiveAudioPorts call success: %s", activeAudioPort.c_str());
   }
  else
  {
    MIRACASTLOG_ERROR("getActiveAudioPorts call failed");
  }
} 
std::string ThunderUtils::getFirmwareVersion()
{
    JsonObject result;
    std::string fwVersion;
    thunderGet(DEVICE_INFO_CALLSIGN, "firmwareversion", result);
    if (result.HasLabel("imagename"))
    {
        fwVersion = result["imagename"].String();
        MIRACASTLOG_VERBOSE("getFirmwareVersion call success: %s", fwVersion.c_str());
    }
    else {
        MIRACASTLOG_ERROR("getFirmwareVersion call failed");
    }
    return fwVersion;
}

std::string ThunderUtils::getConnectedNWInterface(){
    JsonObject result;
    std::string nwInterface;
    thunderInvoke(NETWORK_CALLSIGN,"getInterfaces", result);
    if (result["success"].Boolean()){
        if (result.HasLabel("interfaces") && (result["interfaces"].Content() == JsonValue::type::ARRAY)){
            const JsonArray& interfaces = result["interfaces"].Array();
            MIRACASTLOG_VERBOSE("No. interfaces:%d", interfaces.Length());
            for(int i=0; i < interfaces.Length();i++)
            {
                const JsonObject& interfaceObj = interfaces[i].Object();
                if(interfaceObj["connected"].Boolean()==true){
                     nwInterface = interfaceObj["interface"].String();
                     break;
                }
            }
            MIRACASTLOG_VERBOSE("getInterfaces call success: Connected interface: %s",nwInterface.c_str());
        }
        else {
            MIRACASTLOG_ERROR("No interfaces found or interfaces type is not array");
        }
    }
    else {
        MIRACASTLOG_ERROR("getInterfaces call failed");
    }
    return nwInterface;
}

bool ThunderUtils::isttsenabled()
{
    JsonObject params, result;
    bool isTTSEnabled = false;
    thunderInvoke(TEXTTOSPEECH_CALLSIGN, "isttsenabled", params, result);
    if (result["success"].Boolean())
    {
        isTTSEnabled = result["isenabled"].Boolean();
        MIRACASTLOG_VERBOSE("TTS Enabled call success");
    }
    else
    {
        MIRACASTLOG_ERROR("TTS Enabled call failed");
    }
    return isTTSEnabled;
}

void ThunderUtils::setttsRate(double& wpm)
{
        JsonObject params, result;
        int rate;
         params.Set(_T("rate"),wpm);
        thunderInvoke(TEXTTOSPEECH_CALLSIGN, "setttsconfiguration",params,result);
          if (result["success"].Boolean())
            {
                     rate = result["rate"].Number();
                MIRACASTLOG_VERBOSE("TTS SetRate call success");
            }
            else
            {
                MIRACASTLOG_ERROR("TTS SetRate check call failed");
            }
}

void ThunderUtils::cancelTts(int& speech_id){
        JsonObject params, result;
         params.Set(_T("speechid"),speech_id);
        thunderInvoke(TEXTTOSPEECH_CALLSIGN, "cancel",params,result);
          if (result["success"].Boolean())
            {

                MIRACASTLOG_VERBOSE("TTS Cancel check call success");
            }
            else
            {
                MIRACASTLOG_ERROR("TTS Cancel check call failed");
            }
}
int ThunderUtils::SpeakTts(std::string& text, int speak_id)
{
    JsonObject params, result;
    params.Set(_T("text"), text);
    params.Set(_T("callsign"), MIRACASTPLAYER_CALLSIGN);
    int speechId=speak_id;
    thunderInvoke(TEXTTOSPEECH_CALLSIGN, "speak", params, result);
      if (result["success"].Boolean())
        {
            if (speak_id != result["speechid"].Number())
            {
                speechId = result["speechid"].Number();
            }
            MIRACASTLOG_VERBOSE("SpeakTts: Speak call success, Speak ID: %d", speechId);
        }
        else
        {
            MIRACASTLOG_ERROR("SpeakTts: Speak call  failed");
        }

    return speechId;
}
bool ThunderUtils::isspeaking(int& speech_id)
{
    JsonObject params, result;
    bool isspeaking = false;
    thunderInvoke(TEXTTOSPEECH_CALLSIGN, "isspeaking", params, result);
    if (result["success"].Boolean())
    {
        isspeaking = result["isenabled"].Boolean();
        MIRACASTLOG_VERBOSE("TTS Enabled check call success");
    }
    else
    {
        MIRACASTLOG_ERROR("TTS Enabled check call failed");
    }
    return isspeaking;
}

void ThunderUtils::eventHandler_onspeechinterrupted(const JsonObject& parameters)
{
    MIRACASTLOG_VERBOSE("%s plugin \n", parameters["callsign"].String().c_str());
    RDKTextToSpeech::ttsEventHandler(ttsNotificationType::EVENT_ONSPEECHINTERRUPTED, parameters);
}

void ThunderUtils::eventHandler_onspeechcomplete(const JsonObject& parameters)
{
    MIRACASTLOG_VERBOSE("%s plugin \n", parameters["callsign"].String().c_str());
    RDKTextToSpeech::ttsEventHandler(ttsNotificationType::EVENT_ONSPEECHCOMPLETE, parameters);
}

void ThunderUtils::eventHandler_onspeechstart(const JsonObject& parameters)
{
    MIRACASTLOG_VERBOSE("%s plugin \n", parameters["callsign"].String().c_str());
    RDKTextToSpeech::ttsEventHandler(ttsNotificationType::EVENT_ONSPEECHSTART, parameters);
}

void ThunderUtils::eventHandler_onerror(const JsonObject& parameters)
{
    MIRACASTLOG_VERBOSE("%s plugin \n", parameters["callsign"].String().c_str());
    RDKTextToSpeech::ttsEventHandler(ttsNotificationType::EVENT_ONSPEECHERROR, parameters);
}

void ThunderUtils::eventHandler_onttsstatechanged(const JsonObject& parameters)
{
    MIRACASTLOG_VERBOSE("MiracastPlayer.TTS onttsstatechanged : %d \n", parameters["state"].Boolean());
    MIRACASTLOG_VERBOSE(" MiracastPlayer.TTS onttsstatechanged : %d \n", parameters["state"].Boolean());
    //Call registered listern to handle statechange event
    RDKTextToSpeech::ttsEventHandler(ttsNotificationType::EVENT_ONTTSSTATECHANGE, parameters);
    //Call app engine to update the screen reader accessibility property
    MiracastPlayer::Application::Engine::getAppEngineInstance()->_updatescreenReaderAccessibilityProperty(parameters["state"].Boolean());
}

void ThunderUtils::eventHandler_onConnectionStatusChanged(const JsonObject& parameters)
{
    std::string connectionStatus, nwInterface;
    connectionStatus = parameters["status"].String();
    nwInterface = parameters["interface"].String();
    MIRACASTLOG_VERBOSE(" MiracastPlayer onConnectionStatusChanged, interface: %s status: %s",nwInterface.c_str(), connectionStatus.c_str());
    
    if (nwInterface == "WIFI") {
        if (connectionStatus == "CONNECTED") {
            MiracastPlayerDeviceProperties::getInstance()->updateNWConnectivityStatus(nwInterface, true);
        } else {
            MiracastPlayerDeviceProperties::getInstance()->updateNWConnectivityStatus(nwInterface, false);
        }
    } else if (nwInterface == "ETHERNET" && connectionStatus != "CONNECTED") {
        MiracastPlayerDeviceProperties::getInstance()->updateNWConnectivityStatus(nwInterface, false);
    }
}

void ThunderUtils::eventHandler_onDefaultInterfaceChanged(const JsonObject& parameters)
{
    std::string oldInterfaceName, newInterfaceName;
    oldInterfaceName = parameters["oldInterfaceName"].String();
    newInterfaceName = parameters["newInterfaceName"].String();

    MIRACASTLOG_VERBOSE("MiracastPlayer onDefaultInterfaceChanged, old interface: %s, new interface: %s", oldInterfaceName.c_str(), newInterfaceName.c_str());
    MiracastPlayerDeviceProperties::getInstance()->updateNWConnectivityStatus(newInterfaceName, true);   
}
void ThunderUtils::updateAppIPtoDaemon()
{
    std::string deviceIP = getIPAddress();
    JsonObject params;
    params["ipaddress"] = deviceIP.c_str();
    MIRACASTLOG_INFO("IP address = %s", deviceIP.c_str());
    thunderSet(HOMEKITTV_CALLSIGN, "setAppContainerIPAddress", params);
}
std::string ThunderUtils::getIPAddress()
{
    struct ifaddrs *ifAddrStruct = nullptr;
    struct ifaddrs *ifa = nullptr;
    std::string ipAddress = "";

    if (getifaddrs(&ifAddrStruct) == -1)
    {
        MIRACASTLOG_ERROR("Error getting network interfaces.", MIRACASTPLAYER_APP_LOG);
        return "";
    }

    for (ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr)
        {
            continue;
        }

        // Check if it's an IPv4 address
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            char ipAddr[INET_ADDRSTRLEN];
            struct sockaddr_in *inAddr = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
            inet_ntop(AF_INET, &(inAddr->sin_addr), ipAddr, INET_ADDRSTRLEN);
            MIRACASTLOG_INFO("IP address = %s", ipAddr);
            if (strncmp(ipAddr, "100.64.11", 9) == 0)
            {
                ipAddress = ipAddr;
                break;
            }
        }
    }
    freeifaddrs(ifAddrStruct);
    return ipAddress;
}
bool ThunderUtils::getEnableAudioPort(string &enabledAudioPort)
{
    bool enable_port = false;
    MIRACASTLOG_TRACE("Entering ...");
    JsonObject params, result;
    params["audioPort"] = enabledAudioPort.c_str();
    thunderInvoke(DISPLAYSETTINGS_CALLSIGN, "getEnableAudioPort",params, result);
    if (result["success"].Boolean())
    {
        MIRACASTLOG_VERBOSE("getEnabledAudioPorts call success");
        enable_port = result["enable"].Boolean();
        MIRACASTLOG_VERBOSE("getEnabledAudioPorts success, audio port:%s enabled_port = %d\n",enabledAudioPort.c_str(), enable_port);
    }
    else
    {
        MIRACASTLOG_ERROR("getEnabledAudioPorts call failed");
    }
    return enable_port;
    MIRACASTLOG_TRACE("Exiting ...");
}