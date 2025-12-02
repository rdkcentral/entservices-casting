/*
* If not stated otherwise in this file or this component's Licenses.txt file the
* following copyright and licenses apply:
*
* Copyright 2023 RDK Management
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
#include "MiracastLogger.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <sys/time.h>

namespace MIRACAST
{
    static inline void sync_stdout()
    {
        if (getenv("SYNC_STDOUT"))
            setvbuf(stdout, NULL, _IOLBF, 0);
    }

    static int gDefaultLogLevel = INFO_LEVEL;
    static std::string service_name = "NOT-DEFINED";

    void logger_init(const char* module_name)
    {
        const char *level = getenv("MIRACAST_DEFAULT_LOG_LEVEL");

        sync_stdout();
        if (level)
        {
            /* FIX: Validate level string before atoi to prevent issues with malformed input */
            int level_val = atoi(level);
            if (level_val >= FATAL_LEVEL && level_val <= TRACE_LEVEL)
            {
                set_loglevel(static_cast<LogLevel>(level_val));
            }
        }

        if ( nullptr != module_name )
        {
            service_name = module_name;
        }
    }

    void logger_deinit()
    {
        /* NOP */
    }

    void set_loglevel(LogLevel level)
    {
        gDefaultLogLevel = level;
    }

    void log(LogLevel level,
            const char *func,
            const char *file,
            int line,
            int threadID,
            const char *format, ...)
    {
        const char *levelMap[] = {"FATAL", "ERROR", "WARN", "INFO", "VERBOSE", "TRACE"};
        const short kFormatMessageSize = 4096;
        char formatted[kFormatMessageSize];

        if (((MIRACAST::FATAL_LEVEL != level)&&(MIRACAST::ERROR_LEVEL != level))&&
            (gDefaultLogLevel < level)){
                return;
        }

        /* FIX: Add null pointer checks for func, file, and format parameters */
        if (!func || !file || !format)
        {
            return;
        }

        va_list argptr;
        va_start(argptr, format);
        vsnprintf(formatted, kFormatMessageSize, format, argptr);
        va_end(argptr);

        fprintf(stderr, "[%s][%d] %s [%s:%d] %s: %s \n",
                    service_name.c_str(),
                    (int)syscall(SYS_gettid),
                    levelMap[static_cast<int>(level)],
                    basename(file),
                    line,
                    func,
                    formatted);
        fflush(stderr);
    }
} // namespace MIRACAST
