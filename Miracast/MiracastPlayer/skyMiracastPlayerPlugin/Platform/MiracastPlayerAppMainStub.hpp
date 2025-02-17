//
//  MiracastPlayerAppMainStub.hpp
//  Copyright (C) 2020 Apple Inc. All Rights Reserved. Not to be used or disclosed without permission from Apple.
//

#ifndef MiracastPlayerAppMainStub_hpp
#define MiracastPlayerAppMainStub_hpp

#include<mutex>

#include "MiracastPlayerApplication.hpp"
#include "MiracastPlayerGraphicsDelegate.hpp"
#include "ThunderUtils.h"

using namespace MiracastPlayer;
using namespace MiracastPlayer::Graphics;

class MiracastPlayerAppMain{
    public:
        static MiracastPlayerAppMain * getMiracastPlayerAppMainInstance();
        static void destroyMiracastPlayerAppMainInstance();
        static bool isMiracastPlayerAppMainObjValid();
        uint32_t launchMiracastPlayerAppMain(int argc, const char **argv,void*);
	void handleStopMiracastPlayerApp();
	int getMiracastPlayerAppstate();
    void setMiracastPlayerAppVisibility(bool visible);
    private:
        static MiracastPlayerAppMain * mMiracastPlayerAppMain;
    	MiracastPlayerGraphicsDelegate * mGraphicsDelegate {nullptr};
        MiracastPlayer::Application::Engine *mAppEngine {nullptr};
	    std::mutex mAppRunnerMutex;
        MiracastPlayerAppMain();
        ~MiracastPlayerAppMain();
		MiracastPlayerAppMain & operator=(const MiracastPlayerAppMain &) = delete;
		MiracastPlayerAppMain(const MiracastPlayerAppMain &) = delete;
};
#endif /* MiracastPlayerAppMainStub_hpp */
