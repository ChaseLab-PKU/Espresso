#pragma once

#ifndef __HOST_SIM__
#define __HOST_SIM__

#include <fstream>
#include <iostream>

#include "sim/engine.hh"
#include "simplessd/cpu/cpu.hh"
#include "simplessd/util/simplessd.hh"
#include "util/print.hh"
#include "sim/simulator.hh"

class Host {
public:
  // Global engine
  Engine &engine;

  // File
  std::string config_file;
  SimpleSSD::ConfigReader config;

  // Statistics
  SimpleSSD::Event statEvent;
  uint32_t statistic_period = 1; // in miliseconds, TODO: get from config
  std::string logPath;
  std::ofstream logOut;
  std::vector<SimpleSSD::Stats> statList;

  // Resources
  SimpleSSD::CPU::CPU *pCPU = nullptr;

private:
  void initStatistic();

public:
  Host(Engine& glb_engine, std::string config_file);
  ~Host();
  int initHost(std::string outputDir);
  void destroyHost();
  void statistics(uint64_t tick);
};

#endif