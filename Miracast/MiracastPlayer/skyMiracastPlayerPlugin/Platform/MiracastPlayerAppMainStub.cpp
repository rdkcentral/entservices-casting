//
//  MiracastPlayerAppMainStub.cpp
//  Copyright (C) 2020 Apple Inc. All Rights Reserved. Not to be used or disclosed without permission from Apple.
//

#include "MiracastPlayerAppMainStub.hpp"

#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <gst/gst.h>
#include <unistd.h>
#include <pthread.h>

#include "MiracastPlayerApplication.hpp"
#include "MiracastPlayerLogging.hpp"
#include "MiracastPlayerGraphicsDelegate.hpp"
#include "MiracastPlayerAppMgr.h"

#define kMiracastPlayerAppContentRootPath "/usr/share/apple/miracastplayerDefault"

using namespace MiracastPlayer;
using namespace MiracastPlayer::Graphics;
typedef MiracastPlayer::Application::Status AppStatus;
extern void appInterface_destroyAppInstance();
extern bool isAppExitRequested();
std::mutex mAppInstanceMutex;

MiracastPlayerAppMain * MiracastPlayerAppMain::mMiracastPlayerAppMain  {nullptr};
bool is4KSupported = false;

uint32_t getSupportedColorFormats() {
        uint32_t colorFormats = 0;
        return colorFormats;
}

uint32_t getSupportedAudioFormats() {
        uint32_t audioFormats = 0;
        return audioFormats;
}

// handler function
void SigHandler_cb(int sigId)
{
	MIRACASTLOG_VERBOSE("INT Signal=%d received by handler \n",sigId);
}

// signal handling function
bool signalHandler()
{
    struct sigaction act;
    sigset_t mask;
    sigset_t orig_mask;
	MIRACASTLOG_VERBOSE("signalHandler entered");
    memset (&act, 0, sizeof(act));
    act.sa_handler = SigHandler_cb;
    // catch SIGINT and SIGTERM for proper shutdown
    if (sigaction(SIGTERM, &act, 0)) {
		MIRACASTLOG_ERROR("sigaction() failed to add SIGTERM");
        return false;
    }
    if (sigaction(SIGINT, &act, 0)) {
	MIRACASTLOG_ERROR("sigaction() failed to add SIGINT");
       return false;
    }
    //catch SIGSEGV signal for Segmentation fault
    //catch SIGABRT signal for Abnormal termination
    if (sigaction(SIGSEGV, &act, 0)) {
        MIRACASTLOG_ERROR("sigaction() failed to add SIGSEGV\n");
       return false;
    }
    if (sigaction(SIGABRT, &act, 0)) {
        MIRACASTLOG_ERROR("sigaction() failed to add SIGABRT\n");
       return false;
    }
    // handle custom signals so we can report better shutdown reasons to miracastplayer
    // SIGUSR1 will be used to notify that we got stuck in suspend or resume
    // SIGALRM will be used to notify that the app shutdown because of low resources
    // SIGUSR2 doesn't seem to work for some reason...
    if (sigaction(SIGUSR1, &act, 0)) {
        MIRACASTLOG_ERROR("sigaction() failed to add SIGUSR1\n");
        return false;
    }
    if (sigaction(SIGALRM, &act, 0)) {
        MIRACASTLOG_ERROR("sigaction() failed to add SIGALRM\n");
       return false;
    }

    // follow miracastplayer app and ignore SIGPIPE
    // we don't want the app to terminate if a write or send call fails
    signal(SIGPIPE, SIG_IGN);
    sigemptyset (&mask);
    sigaddset (&mask, SIGTERM);
    if (sigprocmask(SIG_UNBLOCK, &mask, &orig_mask) < 0) {
		MIRACASTLOG_ERROR("sigprocmask() failed to unblock SIGTERM");
        return false;
    }
	MIRACASTLOG_VERBOSE("SIGTERM, SIGINT, SIGUSR1, SIGABRT,SIGSEGV,SIGINT and SIGALRM handlers installed...\n");
	MIRACASTLOG_VERBOSE("addSignalHandling exit \n");
    return true;
}

MiracastPlayerAppMain * MiracastPlayerAppMain::getMiracastPlayerAppMainInstance(){
        mAppInstanceMutex.lock();
        if(mMiracastPlayerAppMain == nullptr){
                mMiracastPlayerAppMain = new MiracastPlayerAppMain();
        }
        mAppInstanceMutex.unlock();
        return mMiracastPlayerAppMain;
}

void MiracastPlayerAppMain::destroyMiracastPlayerAppMainInstance(){
    AppStatus status = AppStatus::OK;
    if(mMiracastPlayerAppMain != nullptr){
        /*If application stop requested by plugin, then the setWindownEvent
        * Handled at handleStopMiracastPlayerApp(). If application exited 
        * due to other failures, the setWindownEvent() will be invoked here
        * */
        if(!isAppExitRequested()){
                // window closed
        }
        mMiracastPlayerAppMain->mAppEngine->stop();
        if(status != AppStatus::OK)
                {
            MIRACASTLOG_ERROR("Error during stop appEngine!");
        }
        else {
            MIRACASTLOG_INFO("stop appEngine! completed");
        }
        if(mMiracastPlayerAppMain->mAppEngine != nullptr){
            MiracastPlayer::Application::Engine::destroyAppEngineInstance();
            mMiracastPlayerAppMain->mAppEngine = nullptr;
        }
        delete mMiracastPlayerAppMain;
        mMiracastPlayerAppMain = nullptr;
    }
        MIRACASTLOG_INFO("com.vendor.MiracastPlayer", "Destroyed MiracastPlayer App instance");
}

MiracastPlayerAppMain::MiracastPlayerAppMain()
{
    mAppEngine = MiracastPlayer::Application::Engine::getAppEngineInstance();
    if (!mAppEngine)
    {
        MIRACASTLOG_ERROR("Failed to get getAppEngineInstance ...");
    }
}

MiracastPlayerAppMain::~MiracastPlayerAppMain(){
        MiracastPlayerGraphicsDelegate::destroyInstance();
        MiracastPlayer::Application::Engine::destroyAppEngineInstance();
}

#ifdef SKY_BUILD
uint32_t setJSAppPath(const string path){
	string appFilePath;
	size_t pos = path.find('=');
	struct stat st = { 0 };
	if(pos != std::string::npos){
		appFilePath = path.substr(pos+1);
		std::string mainJsFile = appFilePath + "/main.js";
		if((path.find("miracastplayerApp") != std::string::npos) &&  (stat(mainJsFile.c_str(),&st)==-1)){
			appFilePath = kMiracastPlayerAppContentRootPath;
			MIRACASTLOG_INFO("Set CWD to default path to JS files:%s\n",appFilePath.c_str());
		}
		MIRACASTLOG_INFO("Set CWD to default path to JS files:%s\n",appFilePath.c_str());
		if(chdir(appFilePath.c_str()) != 0){
			MIRACASTLOG_ERROR("Error in setting the cwd to directory with JS files:%s \n", appFilePath.c_str());
			return 1;
		}
		MIRACASTLOG_INFO("Successfully set the CWD to directory with JS files:%s\n",appFilePath.c_str());
	} else {
		MIRACASTLOG_ERROR("Invalid path to JS files:%s\n",appFilePath.c_str()); 
	}
	return 0;
}
#endif //SKY_BUILD

uint32_t MiracastPlayerAppMain::launchMiracastPlayerAppMain(int argc, const char **argv,void* userdata)
{
	MiracastPlayer::Logging::setLogLevel();
        GError* error = NULL;
        gst_init_check(NULL, NULL, &error);
        g_free(error);
	AppStatus status = AppStatus::OK;
        MiracastPlayerAppMainNotifier *appMgrNotifer = (MiracastPlayerAppMainNotifier *)userdata;
	int err = EXIT_SUCCESS;
	MIRACASTLOG_TRACE("Entering ...");
#ifdef SKY_BUILD
	//setJSAppPath(argv[0]);
#endif
	mGraphicsDelegate = MiracastPlayerGraphicsDelegate::getInstance();
    mAppEngine->setLaunchArguments(argc, argv);

    if(nullptr == mGraphicsDelegate)
    {
        MIRACASTLOG_ERROR("GraphicsDelegate nullptr!");
    }

    if (nullptr == mAppEngine)
    {
        MIRACASTLOG_ERROR("AppEngine not yet created ...");
        return EXIT_FAILURE;
    }

    mAppEngine->setGraphicsDelegate(mGraphicsDelegate);
	MIRACASTLOG_VERBOSE("starting appEngine!");
	status = mAppEngine->start();
	if ( status != AppStatus::OK ) {
        MIRACASTLOG_ERROR("Failed to start AppEngine [%x]",status);
		err = EXIT_FAILURE;
	}
    else {
		uint8_t state = mAppEngine->_getAppstate();
        MIRACASTLOG_INFO("Notifying AppEngine Status check");
        appMgrNotifer->onMiracastPlayerEngineStarted();
        MIRACASTLOG_INFO("Notifying AppEngine Status done");
		while ( true ) {
			state = mAppEngine->_getAppstate();
			if(state == mAppEngine->State::STARTED || state ==  mAppEngine->State::RUNNING  || state ==  mAppEngine->State::BACKGROUNDING || state ==  mAppEngine->State::RESUMING || mAppEngine->_isAppStopRequested() == true){
			status = mAppEngine->tick();
				if ( status != AppStatus::OK ) {
					MIRACASTLOG_ERROR("Connection_Error: appEngine.tick() returned NOT Status::OK, so Stop appEngine!");
					break;
				}
			}
			else {
				usleep(10*1000);
			}
		// Please note: if the engine is backgrounded, you should also be suspending this loop
		// Since the logic to call appEngine.background is not provided for you (it is system dependent)
		// Ensure that whatever mechanism you use pauses this loop while backgrounded, and that it resumes as appropriate
		}
        appMgrNotifer->onMiracastPlayerEngineStopped();
		appInterface_destroyAppInstance();
	}
	gst_deinit();
        MIRACASTLOG_TRACE("Exiting ...");
	return err;
}

void MiracastPlayerAppMain::handleStopMiracastPlayerApp()
{
        if(mAppEngine != nullptr)
        {
                MIRACASTLOG_INFO("invoking sendWindowEvent(WINDOW_EVENT_CLOSE)\n");
                mAppEngine->_RequestApptoStop();
        }
}
int MiracastPlayerAppMain::getMiracastPlayerAppstate(){
        mAppRunnerMutex.lock();
        int state = -1;
        if(mAppEngine)
                state = mAppEngine->_getAppstate();
        mAppRunnerMutex.unlock();
        return(state);
}

void MiracastPlayerAppMain::setMiracastPlayerAppVisibility(bool visible)
{
        MIRACASTLOG_INFO("setMiracastPlayerAppVisibility %d", visible);
        if(visible == true){
                mAppEngine->foreground();
                MIRACASTLOG_INFO("invoking sendWindowEvent(WINDOW_EVENT_MAXIMIZED)\n");
        }
        else{
                MIRACASTLOG_INFO("invoking sendWindowEvent(WINDOW_EVENT_MINIMIZED)\n");
                mAppEngine->background();
        }

}

bool MiracastPlayerAppMain::isMiracastPlayerAppMainObjValid()
{
        if(mMiracastPlayerAppMain != nullptr)
                return true;
        else    
                return false;
}
