#ifndef MIRACASTPLAYERAPPMGR_H
#define MIRACASTPLAYERAPPMGR_H

#include <vector>
#include <libIARM.h>
#include <libIBus.h>
#include <iarmUtil.h>
#include "AppLaunchDetails.h"
#include "MiracastPlayerAppStateMonitor.h"
#include "MiracastRTSPMsg.h"
#include "MiracastGstPlayer.h"

/**
 * Abstract class for MiracastPlayerAppMainNotifier Notification.
 */
class MiracastPlayerAppMainNotifier
{
public:
    virtual void onMiracastPlayerEngineStarted(void) = 0;
    virtual void onMiracastPlayerEngineStopped(void) = 0;
};

//#include "AppStateMonitor.h"
class MiracastPlayerAppMgr : public MiracastPlayerAppMainNotifier, public MiracastPlayerNotifier
{
public:
    static MiracastPlayerAppMgr* getInstance(int argc, char *argv[]);
    static void destroyInstance();
    //inline static MiracastPlayerAppMgr* getInstance(){return _mMiracastPlayerAppMgrInstance;}
    std::shared_ptr<MiracastPlayerAppStateMonitor>  mMiracastPlayerAppStateMonitor;
    int startAppAndWaitForFinish();

    virtual void onMiracastPlayerEngineStarted(void) override;
    virtual void onMiracastPlayerEngineStopped(void) override;

    virtual void onStateChange(const std::string& client_mac, const std::string& client_name, eMIRA_PLAYER_STATES player_state, eM_PLAYER_REASON_CODE reason_code ) override;

private:
    MiracastPlayerAppMgr(int argc, char *argv[]);
    virtual ~MiracastPlayerAppMgr();
    MiracastPlayerAppMgr &operator=(const MiracastPlayerAppMgr &) = delete;
    MiracastPlayerAppMgr(const MiracastPlayerAppMgr &) = delete;
    static MiracastPlayerAppMgr *_mMiracastPlayerAppMgrInstance;
    static MiracastRTSPMsg *_mMiracastRTSPInstance;
    static MiracastGstPlayer *_mMiracastGstPlayer;
    bool _mIsLaunchAppOnStartup;
    LaunchDetails::AppLaunchDetails* _mStartupLaunchDetails;
    VIDEO_RECT_STRUCT m_video_sink_rect;
    bool islaunchAppOnStartup();
    std::vector<std::string> constructArguments(int argc, char *argv[]);
    void initializeMonitors(bool isSuspendOnStart);
    bool Initialize(void);
    void initIARM();
    void deinitIARM();
};

#endif //MIRACASTPLAYERAPPMGR_H