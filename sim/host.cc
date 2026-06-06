#include "sim/host.hh"
#include "sim/log.hh"
#include "host.hh"

Host::Host(Engine& glb_engine, std::string config_file):
    engine(glb_engine), config_file(config_file) {}

Host::~Host() {}

int Host::initHost(std::string outputDir) {
  // Initialize configuration
  if (!config.init(config_file, true)) {
    std::cerr << " Failed to open simulation configuration file: " << config_file << std::endl;
    return -2;
  }

  // Initialize SimpleSSD engine
  SimpleSSD::setSimulator(&engine);

  // Initialize CPU
  pCPU = new SimpleSSD::CPU::CPU(config, 0xFF);
  if (pCPU == nullptr) {
    std::cerr << " Failed to create CPU" << std::endl;
    return 1;
  }
  pCPU->isHostCPU = true;

  // Initialize statistics
  logOut.open(outputDir + "/" + "host_log.txt");
  if (!logOut.is_open()) {
    std::cerr << " Failed to open host log file: " << logPath << std::endl;
    return 3;
  }
  initStatistic();

  return 0;
}


void Host::destroyHost() {
  // Print last statistics
  statistics(engine.getCurrentTick());
  pCPU->printLastStat();

  // Cleanup all here
  delete pCPU;

  // Close log file
  if (logOut.is_open()) {
    logOut.close();
  }
}


void Host::initStatistic() {
  pCPU->getStatList(statList, "cpu");

  statEvent = engine.allocateEvent([this](uint64_t tick) {
    statistics(tick);
    engine.scheduleEvent(statEvent, tick + statistic_period * 1000000000ULL); 
  });

  engine.scheduleEvent(statEvent, statistic_period * 1000000000ULL);
}

void Host::statistics(uint64_t tick) {
  std::vector<double> stat;
  uint64_t count = 0;

  pCPU->getStatValues(stat);

  count = statList.size();

  if (count != stat.size()) {
    std::cerr << " Stat list length mismatch" << std::endl;
    std::terminate();
  }

  logOut << "Periodic log printout of host CPU " << " @ tick " << tick << std::endl;

  for (uint64_t i = 0; i < count; i++) {
    print(logOut, statList[i].name, 40);
    logOut << "\t";
    print(logOut, stat[i], 20);
    logOut << "\t" << statList[i].desc << std::endl;
  }

  logOut << "End of log @ tick " << tick << std::endl;
}