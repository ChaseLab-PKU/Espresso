#pragma once
#ifndef __SIM_INSTANCE__
#define __SIM_INSTANCE__

#include <fstream>
#include <iostream>
#include <thread>

#include "bil/entry.hh"
#include "igl/request/request_generator.hh"
#include "igl/trace/trace_replayer.hh"
#include "sil/none/none.hh"
#include "sil/nvme/nvme.hh"
#include "sim/engine.hh"
#include "sim/signal.hh"
#include "sim/host.hh"
#include "simplessd/util/simplessd.hh"
#include "simplessd/cpu/cpu.hh"
#include "util/print.hh"

#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

/**
 * Instance means a SimpleSSD with its own IGL, BIL, SIL, HIL, ICL, FTL, PAL and other necessory components.
 * e.g., Log
 */
class Instance {
private:
    // Global engine
    Engine &engine;

    // Host
    Host *pHost = nullptr;
    bool useHostCPU = false;

    // SSD ID
    uint32_t id;

    // File
    std::string sw_config_file;
    std::string hw_config_file;
    std::string output_dir;

    // Configuration
    ConfigReader swConfig;
    SimpleSSD::ConfigReader hwConfig;

    // SSD related components
    BIL::DriverInterface *pInterface = nullptr;
    BIL::BlockIOEntry *pBIOEntry = nullptr;
    IGL::IOGenerator *pIOGen = nullptr;

    SimpleSSD::CPU::CPU *pCPU = nullptr;

    // Log related components
    std::ostream *pLog = nullptr;
    std::ostream *pDebugLog = nullptr;
    std::ostream *pLatencyFile = nullptr;
    std::ostream *pBandwidthFile = nullptr;
    std::ostream *pOpenBlock = nullptr;
    std::ostream *pOpcount = nullptr;
    std::ostream *pRetryLog = nullptr;
    std::ofstream logOut;
    std::ofstream debugLogOut;
    std::ofstream latencyFile;
    std::ofstream bandwidthFile;
    std::ofstream OpenblockOut;
    std::ofstream OpcountOut;
    std::ofstream RetryOut;

    // Statistic
    std::vector<SimpleSSD::Stats> statList;
    SimpleSSD::Event statEvent;
    uint64_t lastTick = 0;
    uint64_t lastReadBytes = 0;
    uint64_t lastWriteBytes = 0;
    uint64_t lastBytes = 0;

    // Running
    uint64_t endTime;

public:
    bool noLogPrintOnScreen = true;
    
private:
    int initLog(bool &noLogPrintOnScreen);
    void releaseLog();
    void initSSDEngine();
    void initCPU();
    int initSIL();
    void initBIL();
    int initIGL();
    void initStatistic();

public:
    Instance(Engine& glb_engine, Host* pHost, uint8_t id, std::string sw_config_file, 
        std::string hw_config_file, std::string output_dir);
    ~Instance();
    int initInstance();
    void destroyInstance();
    void begin();
    void statistics(uint64_t tick);
    void getProgress(float &progress, BIL::Progress &data);
};

#endif