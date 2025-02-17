#ifndef MiracastPlayerAppInterface_hpp
#define MiracastPlayerAppInterface_hpp

#ifndef EXIT_APP_REQUESTED
#define EXIT_APP_REQUESTED 79
#endif

extern int app_exit_err_code;
enum miracastplayer_engine_state {
    MIRACASTPLAYER_STOPPED,
    MIRACASTPLAYER_STARTED,
    MIRACASTPLAYER_RUNNING,
    MIRACASTPLAYER_BACKGROUNDING,
    MIRACASTPLAYER_BACKGROUNDED,
    MIRACASTPLAYER_RESUMING,
};

int appInterface_startMiracastPlayerApp(int argc, const char **argv,void*);
void appInterface_stopMiracastPlayerApp();
void appInterface_destroyAppInstance();
int appInterface_getAppState();
void appInterface_setAppVisibility(bool visible);
bool isAppExitRequested();


#endif /*MiracastPlayerAppInterface_hpp*/
