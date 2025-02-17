/*
    Copyright (C) 2021 Apple Inc. All Rights Reserved. Not to be used or disclosed without permission from Apple.
*/
#ifndef MiracastPlayerLogging_hpp
#define MiracastPlayerLogging_hpp
#define kLoggingFacilityAppEngine "MiracastPlayer.Engine"
#define MIRACASTPLAYER_APP_LOG "MiracastPlayerApp"
#define MIRACASTPLAYER_MARKER "MIRACASTPLAYER_MARKER"
extern int Level_Limit;
#include <string>
#include <syslog.h>
#include "MiracastLogger.h"

//TODO: Flush out logging
namespace MiracastPlayer {
namespace Logging {
void setLogLevel();
}
}
#endif /* MiracastPlayerLogging_hpp */
