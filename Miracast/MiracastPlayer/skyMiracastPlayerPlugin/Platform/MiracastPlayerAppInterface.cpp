#include "MiracastPlayerAppInterface.hpp"
#include "MiracastPlayerAppMainStub.hpp"
#include "MiracastPlayerLogging.hpp"

int app_exit_err_code = 0;
int appInterface_startMiracastPlayerApp(int argc, const char **argv,void* userdata)
{
    int err = EXIT_SUCCESS;
	app_exit_err_code = EXIT_SUCCESS;
    MIRACASTLOG_INFO("Received call to MiracastPlayer Application");
    err = MiracastPlayerAppMain::getMiracastPlayerAppMainInstance()->launchMiracastPlayerAppMain(argc, argv, userdata);
	if(err == EXIT_SUCCESS){
		if(app_exit_err_code == EXIT_APP_REQUESTED){
			err = EXIT_APP_REQUESTED;
		}
	}
	MIRACASTLOG_VERBOSE("MiracastPlayer Application Exited with code:%d\n",err);
    return err;
}
void appInterface_stopMiracastPlayerApp()
{
	app_exit_err_code = EXIT_APP_REQUESTED;
        MIRACASTLOG_INFO("Received call Stop to MiracastPlayer Application\n");
	if(MiracastPlayerAppMain::isMiracastPlayerAppMainObjValid()){
		if(MiracastPlayerAppMain::getMiracastPlayerAppMainInstance()->getMiracastPlayerAppstate() != MIRACASTPLAYER_STOPPED){
			MiracastPlayerAppMain::getMiracastPlayerAppMainInstance()->handleStopMiracastPlayerApp();
		} else {
			MIRACASTLOG_INFO("Received stopMiracastPlayerApp call when Application in stopped state\n");
		}
	}
	else{
		MIRACASTLOG_INFO("Received stopMiracastPlayerApp call when Application in destroyed state\n");
	}
}
int appInterface_getAppState()
{
	return MiracastPlayerAppMain::getMiracastPlayerAppMainInstance()->getMiracastPlayerAppstate();
}
void appInterface_destroyAppInstance()
{
    MIRACASTLOG_VERBOSE("MiracastPlayer App stopped and freed resources. Ok to call destructor.\n");
	MiracastPlayerAppMain::getMiracastPlayerAppMainInstance()->destroyMiracastPlayerAppMainInstance();
}

pthread_t   playback_thread_;
static bool hasConnection = false;

void* playbackThread(void * ctx)
{
	bool visible = (void *)ctx;
	
	int miracastplayer_state;
	if (!hasConnection && !visible)
	{
		miracastplayer_state = appInterface_getAppState();
		if (miracastplayer_state == MIRACASTPLAYER_STARTED || miracastplayer_state == MIRACASTPLAYER_STOPPED)
		{
			MIRACASTLOG_INFO(" MiracastPlayer plugin waiting for engine state RUNNING\n");
			while (miracastplayer_state != MIRACASTPLAYER_RUNNING){
				usleep(1000);
				miracastplayer_state = appInterface_getAppState();
			}
		}
		sleep(10);
		hasConnection = true;
	}
	if (playback_thread_ == pthread_self())
		MiracastPlayerAppMain::getMiracastPlayerAppMainInstance()->setMiracastPlayerAppVisibility(visible);
	pthread_exit(NULL);
	return 0;
}
void appInterface_setAppVisibility(bool visible)
{
    MIRACASTLOG_INFO("MiracastPlayer visibility request = %d\n",visible);
    if(visible) {
         MIRACASTLOG_INFO("MiracastPlayer visibility request = %d & cancel running connection thread \n",visible);
         hasConnection = true; //we assume app is in background now  
		 if(playback_thread_)
         	pthread_cancel(playback_thread_);
         MiracastPlayerAppMain::getMiracastPlayerAppMainInstance()->setMiracastPlayerAppVisibility(visible);
     } 
     else
         pthread_create(&playback_thread_, NULL, &playbackThread, (void *)visible);
}

bool isAppExitRequested(){
	return (app_exit_err_code == EXIT_APP_REQUESTED)?true:false;
}