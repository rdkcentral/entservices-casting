#include <unordered_map>
#include <vector>
#include<openssl/evp.h>
#include <WPEFramework/core/JSON.h>
#include "AppLaunchDetails.h"
#include "MiracastPlayerLogging.hpp"
#include "util_base64.h"

namespace LaunchDetails{
    AppLaunchDetails *AppLaunchDetails::m_AppLauncherInfo{nullptr};

    std::string AppLaunchDetails::getEnvVariable(const std::string& key)
    {
        const char* envVariable = std::getenv(key.c_str());
        return envVariable ? envVariable : "";
    }

    AppLaunchMethod AppLaunchDetails::parseLaunchMethod(const std::string& method)
    {
        MIRACASTLOG_INFO("method: %s\n",method.c_str());

        AppLaunchMethod launchMethod = AppLaunchMethod::Suspend;

        const std::unordered_map<std::string, AppLaunchMethod> launchMethods =
        {
            {"Suspend", AppLaunchMethod::Suspend},
            {"PARTNER_BUTTON", AppLaunchMethod::PartnerButton}
        };

        auto it = launchMethods.find(method);
        if (it != launchMethods.end())
        {
            launchMethod = it->second;
        }

        return launchMethod;
    }

    std::string AppLaunchDetails::base64Decode(const std::string &input)
    {
        std::string decodedStr;
        char decode_str[4000] = {0};
        decodedStr.resize(input.size()+4000);
        MIRACASTLOG_INFO("MiracastPlayer Widget: Entered, encoded sting length:%d\n", input.size()+1);
        int decodedStrLen = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(&decodedStr[0]), reinterpret_cast<const unsigned char*>(&input[0]), input.size());
        if(decodedStrLen != -1){
            decodedStr.resize(decodedStrLen);
            MIRACASTLOG_INFO("MiracastPlayer Widget: Decoded completed:%s decodedStrLen:%d\n", decodedStr, decodedStr.size());
        }
        MIRACASTLOG_VERBOSE("MiracastPlayer Widget: Decoded Str:%s\n", decodedStr.c_str());
        return decodedStr;
    }

    bool AppLaunchDetails::parseLaunchDetails(const std::string& launchMethodStr, const std::string& encodedLaunchParamsStr)
    {
        JsonObject MiracastPlayerAppParams, parameters;
        char decode_str[1000] = {0};
        size_t decode_len=0;
        std::string config_option;
        std::vector<std::string> argVector;
        bool returnValue = true;

        MIRACASTLOG_TRACE("Entering ...");
        
        mLaunchMethod = parseLaunchMethod(launchMethodStr);
        MIRACASTLOG_INFO("MiracastPlayer Widget:Received launch parameters: %s\n",encodedLaunchParamsStr.c_str());
        int status = util_base64_decode(encodedLaunchParamsStr.c_str(), encodedLaunchParamsStr.size(), decode_str,999,&decode_len);
        if(status == 0)
        {
            decode_str[decode_len] = '\0';
            MIRACASTLOG_INFO("MiracastPlayer Widget:Decoded launch parameters: %s decode_len:%d\n",decode_str, decode_len);
            std::string paramStr = decode_str;
            MiracastPlayerAppParams.FromString(paramStr);
            if(MiracastPlayerAppParams.HasLabel("params"))
            {
				parameters = MiracastPlayerAppParams["params"].Object();

                if(parameters.HasLabel("device_parameters")) {
                    JsonObject device_parameters = parameters["device_parameters"].Object();

                    mSourceDevIP = device_parameters["source_dev_ip"].String();
                    mSourceDevMAC = device_parameters["source_dev_mac"].String();
                    mSourceDevName = device_parameters["source_dev_name"].String();
                    mSinkDevIP = device_parameters["sink_dev_ip"].String();
                }
                else
                {
                    MIRACASTLOG_INFO("missing device_parameters from the received parameters\n");
                    returnValue = false;
                }

                if(parameters.HasLabel("video_rectangle"))
                {
                    JsonObject video_rectangle = parameters["video_rectangle"].Object();

                    video_rectangle = parameters["video_rectangle"].Object();

                    mVideoRect.startX = video_rectangle["X"].Number();
                    mVideoRect.startY = video_rectangle["Y"].Number();
                    mVideoRect.width = video_rectangle["W"].Number();
                    mVideoRect.height = video_rectangle["H"].Number();
                }
                else
                {
                    MIRACASTLOG_INFO("missing video_rectangle from the received parameters\n");
                    returnValue = false;
                }

                if(parameters.HasLabel("launchAppInForeground")){
                    mlaunchAppInForeground = parameters["launchAppInForeground"].Boolean();
                    MIRACASTLOG_INFO("launchAppInForeground flag set as: %s\n", mlaunchAppInForeground?"true":"false");
                } else {
                    MIRACASTLOG_INFO("missing launchAppInForeground from the received parameters\n");
                    returnValue = false;
                }
			}
        } else {
            returnValue = false;
            MIRACASTLOG_ERROR("Widget:Failed to Decoded launch parameters are empty\n");
        }
        return returnValue;
    }

    bool AppLaunchDetails::getPlayRequestDetails(std::string& srcDevIP, std::string& srcDevMAC,std::string& srcDevName,std::string& sinkDevIP, VideoRectangleInfo* rect)
    {
        bool returnValue = false;

        MIRACASTLOG_TRACE("Entering ...");

        if (!rect)
        {
            MIRACASTLOG_ERROR("NULL Rectangle pointer \n");
        }
        else
        {
            srcDevIP = mSourceDevIP;
            srcDevMAC = mSourceDevMAC;
            srcDevName = mSourceDevName;
            sinkDevIP = mSinkDevIP;
            rect->startX = mVideoRect.startX;
            rect->startY = mVideoRect.startY;
            rect->width = mVideoRect.width;
            rect->height = mVideoRect.height;
            returnValue = true;
            MIRACASTLOG_INFO("srcDevIP[%s]srcDevMAC[%s]srcDevName[%s]sinkDevIP[%s]Rect[%d,%d,%d,%d]",
                                srcDevIP.c_str(),srcDevMAC.c_str(),srcDevName.c_str(),sinkDevIP.c_str(),
                                rect->startX,rect->startY,rect->width,rect->height);
        }
        MIRACASTLOG_TRACE("Exiting ...");
        return returnValue;
    }

    AppLaunchDetails* parseLaunchDetailsFromEnv()
    {
        AppLaunchDetails* appLaunchInstance = AppLaunchDetails::getInstance();

        if (nullptr != appLaunchInstance)
        {
            MIRACASTLOG_INFO("MiracastPlayer Widget: Entered\n");
            std::string launchMethodStr = appLaunchInstance->getEnvVariable("APPLICATION_LAUNCH_METHOD");
            MIRACASTLOG_INFO(" launchMethodStr set as: %s\n", launchMethodStr.c_str());
            std::string launchArgumentsStr = appLaunchInstance->getEnvVariable("APPLICATION_LAUNCH_PARAMETERS");

            if ( true != appLaunchInstance->parseLaunchDetails(launchMethodStr, launchArgumentsStr))
            {
                MIRACASTLOG_ERROR("Failed to do parseLaunchDetails \n");
                AppLaunchDetails::destroyInstance();
                appLaunchInstance = nullptr;
            }
        }
        return appLaunchInstance;
    }

    AppLaunchDetails *AppLaunchDetails::getInstance()
    {
        if (m_AppLauncherInfo == nullptr)
        {
            m_AppLauncherInfo = new AppLaunchDetails();
        }
        return m_AppLauncherInfo;
    }

    void AppLaunchDetails::destroyInstance()
    {
        if (m_AppLauncherInfo != nullptr)
        {
            delete m_AppLauncherInfo;
            m_AppLauncherInfo = nullptr;
        }
    }

    AppLaunchDetails::AppLaunchDetails()
    {
        
    }

    AppLaunchDetails::~AppLaunchDetails()
    {
        
    }
}