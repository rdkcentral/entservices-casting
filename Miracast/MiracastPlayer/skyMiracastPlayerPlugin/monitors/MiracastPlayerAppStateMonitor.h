#ifndef _MIRACASTPLAYERAPPSTATTEMONITOR_H
#define _MIRACASTPLAYERAPPSTATTEMONITOR_H

#include "IMonitor.h"
#include <wayland-client.h>
#include <wayland-skyq-shell-client-protocol.h>
#include <atomic>

#include "SkyAsProxy.h"

#define MIRACASTPLAYER_SKY_PLUGIN "MiracastPlayerSkyPlugin"
#define WST_CLIENT_IDENTIFIER "wst-miracastplayer"

class SkyAsProxy;
class MiracastPlayerAppStateMonitor final : public IMonitor
{
    struct WaylandComponents
    {
        WaylandComponents() : mDisplay(nullptr), mRegistry(nullptr), mCompositor(nullptr), mShell(nullptr)
        {}

        wl_display* mDisplay;
        wl_registry* mRegistry;
        wl_compositor* mCompositor;
        skyq_shell* mShell;
    };
public:
    enum Mode
    {
        Background,
        Foreground,
        Exiting
    };
    bool start() override;
    void stop() override;
    MiracastPlayerAppStateMonitor(bool isAppLaunchOnStartup /*, const LaunchDetails::AppLaunchDetails:: &launchDetails*/);
    ~MiracastPlayerAppStateMonitor() final;
    void handleInterfaceRegistry(struct wl_registry* registry, uint32_t id, const char* interface);
    void handleStateChangeEvent(uint32_t state);
    void handleCloseEvent();
    inline bool getIsAppLaunchOnStartup() {return _mIsAppLaunchOnStartup;}
    bool resumeApp();
    bool suspendApp();
private:
    std::atomic<bool> mIsLoopRunning;
    std::shared_ptr<SkyAsProxy> mSkyAsProxy;
    WaylandComponents mWaylandComponents;
    Mode mMode;
    bool _mIsAppLaunchOnStartup;
    void run() override;
};
#endif //_MIRACASTPLAYERAPPSTATTEMONITOR_H