#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <string>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <thread>
#include <iomanip>
#include <unordered_map>
#include <map>
#include <unordered_set>

// #define NDEBUG
#include <assert.h>
#include <time.h>
#include <malloc.h>

#include "json.hpp"
using json = nlohmann::json;

#define CODE_2_STR(___a_) #___a_

#define GET_DOUBLE_TS(x) (double) (x.tv_sec + x.tv_nsec*(1e-9))
#define GET_UINT64_TS(x) (uint64_t) (x.tv_sec * 1e9 + x.tv_nsec)

static inline double get_time_spec() {
    timespec ret_time_spec;
    timespec_get(&ret_time_spec, TIME_UTC);
    return GET_DOUBLE_TS(ret_time_spec);
}

static inline timespec get_time_spec(double dtime) {
    timespec ret_time_spec;
    ret_time_spec.tv_sec = ceil(dtime);
    ret_time_spec.tv_nsec = (u_int64_t) ((dtime - ceil(dtime)) * 1e9);
    return ret_time_spec;
}

#define ENHANCED_OUTPUT
#ifndef ENHANCED_OUTPUT
    #define KNRM  "\x1B[0m"
    #define KRED  "\x1B[31m"
    #define KGRN  "\x1B[32m"
    #define KYEL  "\x1B[33m"
    #define KBLU  "\x1B[34m"
    #define KMAG  "\x1B[35m"
    #define KCYN  "\x1B[36m"
    #define KWHT  "\x1B[37m"
#else
    #define KNRM  "\x1B[0m"
    #define KRED  "\x1B[31;1m"
    #define KGRN  "\x1B[32;1m"
    #define KYEL  "\x1B[33;1m"
    #define KBLU  "\x1B[34;1m"
    #define KMAG  "\x1B[35;1m"
    #define KCYN  "\x1B[36;1m"
    #define KWHT  "\x1B[37;1m"
#endif

#define HIGH_LIGHT_ERROR KCYN
#define HIGH_LIGHT_LOG KGRN
#define HIGH_LIGHT_WARN KRED
#define HIGH_LIGHT_TIME KMAG

// 条件编译的宏定义标志
#define SYM_DEBUG
#define FUNCTION_TIME

#ifdef HIGH_LIGHT_ERROR
    
    #define FATAL_ERROR(error_info) \
        do {\
            std::cerr << HIGH_LIGHT_ERROR << "[FATAL_ERROR@" << __FILE__ <<  ":" <<  __LINE__ << "->" << __FUNCTION__ << "()]: " << KNRM; \
            std::cerr << error_info << std::endl;\
            exit(-1);\
        } while(0)

#else

    #define FATAL_ERROR(error_info) \
        do {\
            std::cerr << "[FATAL ERROR @ " << __FILE__ <<  ":" <<  __LINE__ << "->" << __FUNCTION__ << "()]: "; \
            std::cerr << error_info << std::endl;\
            exit(-1);\
        } while(0)

#endif


#ifdef SYM_DEBUG
    
    #ifdef HIGH_LIGHT_LOG
        #ifndef LOG
            
            #define LOG(log_info, ...) \
                do {\
                    printf("%s[NORMAL_LOG@%s:%d->%s()]%s: %s\n", HIGH_LIGHT_LOG, __FILE__, __LINE__, __FUNCTION__, KNRM, log_info);\
                } while(0)

        #else

            #warning LOG Define Conflict

        #endif
    #else
        #ifndef LOG

            #define LOG(log_info) \
                do {\
                    printf("[NORMAL_LOG@%s:%d->%s()]: %s\n", __FILE__, __LINE__, __FUNCTION__, log_info);\
                } while(0)

        #else

            #warning LOG Define Conflict

        #endif
        
    #endif

    #define LOG_LOCATE() LOG("")

    #ifdef HIGH_LIGHT_LOG

        #define LOGF(__fmt__, ...) \
        do { \
            printf("%s[NORMAL_LOG@%s:%d->%s()]%s: " __fmt__ "\n", HIGH_LIGHT_LOG, __FILE__, __LINE__, __FUNCTION__, KNRM, ##__VA_ARGS__); \
        } while (0)

    #else

        #define LOGF(__fmt__, ...) \
        do { \
            printf("[NORMAL_LOG@%s:%d->%s()]: " __fmt__ "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
        } while (0)
        
    #endif

#else

    #define LOG(log_info)
    #define LOGF(__fmt__, ...)

#endif

#ifdef SYM_DEBUG

    #ifdef HIGH_LIGHT_WARN

        #define WARN(log_info) \
            do { \
                printf("%s[WARN_LOG@%s:%d->%s()]%s: %s\n", HIGH_LIGHT_WARN, __FILE__, __LINE__, __FUNCTION__, KNRM, log_info); \
            } while (0)

    #else

        #define WARN(log_info) \
            do { \
                printf("[WARN_LOG@%s:%d->%s()]: %s\n", __FILE__, __LINE__, __FUNCTION__, log_info); \
            } while (0)

    #endif

    #ifdef HIGH_LIGHT_WARN

        #define WARNF(__fmt__, ...) \
        do { \
            printf("%s[WARN_LOG@%s:%d->%s()]%s: " __fmt__ "\n", HIGH_LIGHT_WARN, __FILE__, __LINE__, __FUNCTION__, KNRM, ##__VA_ARGS__); \
        } while (0)

    #else

        #define WARNF(__fmt__, ...) \
        do { \
            printf("[WARN_LOG@%s:%d->%s()]: " __fmt__ "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
        } while (0)

    #endif

#else

    #define WARN(log_info)
    #define WARNF(__fmt__, ...)

#endif


#ifdef FUNCTION_TIME
    
    #define __START_TIMER__ timespec _start_time; timespec_get(&_start_time, TIME_UTC);
    #define __STOP_TIMER__ timespec _stop_time; timespec_get(&_stop_time, TIME_UTC);
    
    #ifdef HIGH_LIGHT_TIME

        #define __PRINT_EXE_TIME__ \
            do { \
                printf("%s[TIMER_LOG@%s->%s()]%s: %7.6lf\n", HIGH_LIGHT_TIME, __FILE__, __FUNCTION__, KNRM, \
                        GET_DOUBLE_TS(_stop_time) - GET_DOUBLE_TS(_start_time)); \
            } while(0);

    #else

        #define __PRINT_EXE_TIME__ \
            do { \
                printf("[TIMER_LOG@%s->%s()]: %7.6lf\n", __FILE__, __FUNCTION__, \
                        GET_DOUBLE_TS(_stop_time) - GET_DOUBLE_TS(_start_time)); \
            } while(0);

    #endif

#endif
