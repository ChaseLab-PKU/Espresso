#include "sim/instance.hh"
#include "instance.hh"

uint8_t running_instance = 0;

Instance::Instance(Engine& glb_engine, Host* pHost, uint8_t id, std::string sw_config_file, 
            std::string hw_config_file, std::string output_dir):
    engine(glb_engine), pHost(pHost), id(id), sw_config_file(sw_config_file), 
    hw_config_file(hw_config_file), output_dir(output_dir) {}

Instance::~Instance() {}

void joinPath(std::string &lhs, std::string &rhs)
{
  if (rhs.front() == '/')
  {
    // Assume absolute path
    lhs = rhs;
  }
  else if (lhs.back() == '/')
  {
    lhs += rhs;
  }
  else
  {
    lhs += '/';
    lhs += rhs;
  }
}

int Instance::initLog(bool &noLogPrintOnScreen)
{
  std::string logPath = swConfig.readString(CONFIG_GLOBAL, GLOBAL_LOG_FILE);
  std::string debugLogPath =
      swConfig.readString(CONFIG_GLOBAL, GLOBAL_DEBUG_LOG_FILE);
  std::string latencyLogPath =
      swConfig.readString(CONFIG_GLOBAL, GLOBAL_LATENCY_LOG_FILE);
  std::string bandwidthLogPath =
      swConfig.readString(CONFIG_GLOBAL, GLOBAL_BANDWIDTH_LOG_FILE);
  std::string openblockPath =
      swConfig.readString(CONFIG_GLOBAL, GLOBAL_OPENBLOCK_FILE);
  std::string opcountPath =
      swConfig.readString(CONFIG_GLOBAL, GLOBAL_OPCOUNT_FILE);
  std::string retryLogPath =
      swConfig.readString(CONFIG_GLOBAL, GLOBAL_RETRYLOG_FILE);

  // Log
  if (logPath.compare("STDOUT") == 0)
  {
    noLogPrintOnScreen = false;
    pLog = &std::cout;
  }
  else if (logPath.compare("STDERR") == 0)
  {
    noLogPrintOnScreen = false;
    pLog = &std::cerr;
  }
  else if (logPath.length() != 0)
  {
    std::string full(output_dir);

    joinPath(full, logPath);
    logOut.open(full);

    if (!logOut.is_open())
    {
      std::cerr << " Failed to open log file: " << full << std::endl;

      return 3;
    }

    pLog = &logOut;
  }

  // Debug Log
  if (debugLogPath.compare("STDOUT") == 0)
  {
    noLogPrintOnScreen = false;
    pDebugLog = &std::cout;
  }
  else if (debugLogPath.compare("STDERR") == 0)
  {
    noLogPrintOnScreen = false;
    pDebugLog = &std::cerr;
  }
  else if (debugLogPath.length() != 0)
  {
    std::string full(output_dir);

    joinPath(full, debugLogPath);
    debugLogOut.open(full);

    if (!debugLogOut.is_open())
    {
      std::cerr << " Failed to open log file: " << full << std::endl;

      return 3;
    }

    pDebugLog = &debugLogOut;
  }

  // Open Block
  if (openblockPath.compare("STDOUT") == 0)
  {
    noLogPrintOnScreen = false;
    pOpenBlock = &std::cout;
  }
  else if (openblockPath.compare("STDERR") == 0)
  {
    noLogPrintOnScreen = false;
    pOpenBlock = &std::cerr;
  }
  else if (openblockPath.length() != 0)
  {
    std::string full(output_dir);

    joinPath(full, openblockPath);
    OpenblockOut.open(full);

    if (!OpenblockOut.is_open())
    {
      std::cerr << " Failed to open log file: " << full << std::endl;

      return 3;
    }

    pOpenBlock = &OpenblockOut;
  }

  // OpCount
  if (opcountPath.compare("STDOUT") == 0)
  {
    noLogPrintOnScreen = false;
    pOpcount = &std::cout;
  }
  else if (opcountPath.compare("STDERR") == 0)
  {
    noLogPrintOnScreen = false;
    pOpcount = &std::cerr;
  }
  else if (opcountPath.length() != 0)
  {
    std::string full(output_dir);

    joinPath(full, opcountPath);
    OpcountOut.open(full);

    if (!OpcountOut.is_open())
    {
      std::cerr << " Failed to open log file: " << full << std::endl;

      return 3;
    }

    pOpcount = &OpcountOut;
  }

  // Retry Log
  if (retryLogPath.compare("STDOUT") == 0)
  {
    noLogPrintOnScreen = false;
    pRetryLog = &std::cout;
  }
  else if (retryLogPath.compare("STDERR") == 0)
  {
    noLogPrintOnScreen = false;
    pRetryLog = &std::cerr;
  }
  else if (retryLogPath.length() != 0)
  {
    std::string full(output_dir);

    joinPath(full, retryLogPath);
    RetryOut.open(full);

    if (!RetryOut.is_open())
    {
      std::cerr << " Failed to open log file: " << full << std::endl;

      return 3;
    }

    pRetryLog = &RetryOut;
  }

  // Latency Log
  if (latencyLogPath.length() != 0)
  {
    std::string full(output_dir);

    joinPath(full, latencyLogPath);
    latencyFile.open(full);

    if (!latencyFile.is_open())
    {
      std::cerr << " Failed to open log file: " << full << std::endl;

      return 3;
    }

    pLatencyFile = &latencyFile;
  }

  // Latency Log
  if (bandwidthLogPath.length() != 0)
  {
    std::string full(output_dir);

    joinPath(full, bandwidthLogPath);
    bandwidthFile.open(full);

    if (!bandwidthFile.is_open())
    {
      std::cerr << " Failed to open log file: " << full << std::endl;

      return 3;
    }

    pBandwidthFile = &bandwidthFile;
  }

  return 0;
}

void Instance::releaseLog() 
{
   if (logOut.is_open()) {
    logOut.close();
  }
  if (debugLogOut.is_open()) {
    debugLogOut.close();
  }
  if (latencyFile.is_open()) {
    latencyFile.close();
  }
  if (bandwidthFile.is_open()) {
    bandwidthFile.close();
  }
  if (OpenblockOut.is_open()) {
    OpenblockOut.close();
  }
  if (OpcountOut.is_open()) {
    OpcountOut.close();
  }
  if (RetryOut.is_open()) {
    RetryOut.close();
  }
}

void Instance::initSSDEngine()
{
  hwConfig = initSimpleSSDEngine(&engine, pDebugLog, pDebugLog,
                                 pOpenBlock, pOpcount, pRetryLog, hw_config_file);
}

void Instance::initCPU() 
{
  useHostCPU = hwConfig.readBoolean(SimpleSSD::CONFIG_CPU, SimpleSSD::CPU::CPU_USE_HOST);
  if (useHostCPU) {
    pCPU = pHost->pCPU;
  } else {
    pCPU = new SimpleSSD::CPU::CPU(hwConfig, id);
  }
}

int Instance::initSIL()
{
  switch (swConfig.readUint(CONFIG_GLOBAL, GLOBAL_INTERFACE))
  {
  case INTERFACE_NONE:
    pInterface = new SIL::None::Driver(engine, pHost, hwConfig, pCPU);

    break;
  case INTERFACE_NVME:
    pInterface = new SIL::NVMe::Driver(engine, pHost, hwConfig, pCPU, id, output_dir);

    break;
  default:
    std::cerr << " Undefined interface specified." << std::endl;

    return 4;
  }

  return 0;
}

void Instance::initBIL()
{
  pBIOEntry = new BIL::BlockIOEntry(swConfig, engine, pHost, pInterface, pLatencyFile);
}

int Instance::initIGL()
{ 
  std::function<void()> endCallback = [this]() {
    // If stat printout is scheduled, delete it
    // if (swConfig.readUint(CONFIG_GLOBAL, GLOBAL_LOG_PERIOD) > 0) {
    //   engine.descheduleEvent(statEvent);
    // }

    // Stop simulation
    endTime = engine.getCurrentTick();
    if(--running_instance == 0) {
      engine.stopEngine();
    }
  };

  switch (swConfig.readUint(CONFIG_GLOBAL, GLOBAL_SIM_MODE))
  {
  case MODE_REQUEST_GENERATOR:

    pIOGen =
        new IGL::RequestGenerator(id, engine, pHost, *pBIOEntry, endCallback, swConfig);

    break;
  case MODE_TRACE_REPLAYER:

    pIOGen =
        new IGL::TraceReplayer(id, engine, pHost, *pBIOEntry, endCallback, swConfig);

    break;
  default:
    std::cerr << " Undefined simulation mode specified." << std::endl;

    return 5;
  }

  return 0;
}

void Instance::initStatistic()
{
  pInterface->initStats(statList);

  if (swConfig.readUint(CONFIG_GLOBAL, GLOBAL_LOG_PERIOD) > 0) {
    statEvent = engine.allocateEvent([this](uint64_t tick) {
        statistics(tick);
        engine.scheduleEvent(statEvent, tick + swConfig.readUint(CONFIG_GLOBAL, GLOBAL_LOG_PERIOD) * 1000000000ULL); 
    });

    engine.scheduleEvent(statEvent, swConfig.readUint(CONFIG_GLOBAL, GLOBAL_LOG_PERIOD) * 1000000000ULL);
  }
}

int Instance::initInstance() 
{
  int ret = 0;

  // Init software configuration (i.e., LOG, IGL, BIL, and SIL)
  if (!swConfig.init(sw_config_file)) {
    std::cerr << " Failed to open simulation configuration file: " << sw_config_file << std::endl;
    return 2;
  }

  // Log setting
  ret = initLog(noLogPrintOnScreen);
  if (ret) return ret;

  // Initialize SimpleSSD
  initSSDEngine();

  // Initialize CPU
  initCPU();

  // Create Driver
  ret = initSIL();
  if (ret) return ret;

  // Create Block I/O Layer
  initBIL();

  // Create I/O generator
  ret = initIGL();
  if (ret) return ret;

  // Insert stat event
  initStatistic();

  return 0;
}

void Instance::destroyInstance() {
  // Print last statistics
  statistics(engine.getCurrentTick());

  pCPU->printLastStat();
  
  releaseSimpleSSDEngine();

  printf("*********************** SSD %d Final Output ********************\n", id);
  pIOGen->printStats(std::cout);
  printf("******************** End of SSD %d Final Output ****************\n", id);

  // Cleanup all here
  if (!useHostCPU) {delete pCPU;}
  delete pInterface;
  delete pIOGen;
  delete pBIOEntry;

  releaseLog();
}

void Instance::begin() {
  running_instance++;

  std::function<void()> beginCallback = [this]() {
    uint64_t bytesize;
    uint32_t bs;

    pInterface->getInfo(bytesize, bs);
    pIOGen->init(bytesize, bs);
    pIOGen->begin();
  };

  pInterface->init(beginCallback);
}

void Instance::statistics(uint64_t tick) {
  if (pLog == nullptr) {
    return;
  }

  std::ostream &out = *pLog;
  std::vector<double> stat;
  uint64_t count = 0;

  pInterface->getStats(stat);

  count = statList.size();

  if (count != stat.size()) {
    std::cerr << " Stat list length mismatch" << std::endl;

    std::terminate();
  }

  out << "Periodic log printout of SSD " << id << " @ tick " << tick << std::endl;

  for (uint64_t i = 0; i < count; i++) {
    print(out, statList[i].name, 40);
    out << "\t";
    print(out, stat[i], 20);
    out << "\t" << statList[i].desc << std::endl;
  }

  out << "End of log @ tick " << tick << std::endl;

  if (tick != 0) {
    uint64_t readBytes = 0;
    uint64_t writeBytes = 0;
    uint64_t bytes = 0;

    pBIOEntry->getStat(readBytes, writeBytes, bytes);

    double readBandwidth = (readBytes - lastReadBytes) / ((tick - lastTick) / 1000000000000.0) / 1000 / 1000;
    double writeBandwidth = (writeBytes - lastWriteBytes) / ((tick - lastTick) / 1000000000000.0) / 1000 / 1000;
    double bandwidth = (bytes - lastBytes) / ((tick - lastTick) / 1000000000000.0) / 1000 / 1000;
    
    bandwidthFile << tick << "\t" << readBandwidth << "\t" << writeBandwidth << "\t" << bandwidth << std::endl;

    lastTick = tick;
    lastReadBytes = readBytes;
    lastWriteBytes = writeBytes;
    lastBytes = bytes;
  }
}

void Instance::getProgress(float &progress, BIL::Progress &data)
{
  pIOGen->getProgress(progress);
  pBIOEntry->getProgress(data);
}
