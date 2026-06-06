/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

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
#include "sim/instance.hh"
#include "sim/host.hh"
#include "simplessd/util/simplessd.hh"
#include "simplessd/dram/remote.hh"
#include "util/print.hh"

#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

// Global objects
Engine engine;
uint8_t num_instance;
std::vector<Instance *> pInstances;
Host *pHost = nullptr;
bool noLogPrintOnScreen = true;
std::thread *pThread = nullptr;
std::mutex killLock;

// Declaration
void cleanup(int);
void threadFunc(int);

int main(int argc, char *argv[]) {
  int ret = 0;

  std::cout << "SimpleSSD Standalone v2.0" << std::endl;

  // Check argument
  if (argc < 6) {
    std::cerr << " Invalid number of argument!" << std::endl;
    std::cerr << "  Usage: simplessd-standalone <Number of instance (N)> <Output directory> <Simulation configuration file of host>"
                 "{<Simulation configuration file of instance i> <SimpleSSD configuration file of instance i>} * N"
              << std::endl;

    return 1;
  }
  
  // Check the number of instance
  num_instance = atoi(argv[1]);
  if (num_instance != (argc - 4 ) / 2) {
    std::cerr << " Invalid number of argument!" << std::endl;
    std::cerr << "  Usage: simplessd-standalone <Number of instance (N)> <Output directory> <Simulation configuration file of host>"
                 "{<Simulation configuration file of instance i> <SimpleSSD configuration file of instance i>} * N"
              << std::endl;
    return 1;
  }
  pInstances.resize(num_instance);

  // Install signal handler
  installSignalHandler(cleanup);

  // Init Xerxes engine
  xerxes::init_xerxes_engine();
  xerxes::create_switch();

  // Init host
  pHost = new Host(engine, argv[3]);
  ret = pHost->initHost(argv[2]);
  if (ret) {
    std::cerr << " Failed to initialize host" << std::endl;
    return ret;
  }

  // Init each instance
  for (int i = 0; i < num_instance; ++i) {
    std::string output_dir(argv[2]);
    std::string sw_config_file(argv[4+2*i]);
    std::string hw_config_file(argv[5+2*i]);
    
    pInstances[i] = new Instance(engine, pHost, i, sw_config_file, hw_config_file, output_dir);
    
    ret = pInstances[i]->initInstance();
    if (ret) return ret;

    noLogPrintOnScreen &= pInstances[i]->noLogPrintOnScreen;
  }

  // Init Xerxes route
  xerxes::build_route();

  // Do Simulation
  std::cout << "********** Begin of simulation **********" << std::endl;

  for(int i = 0; i < num_instance; ++i) {
    pInstances[i]->begin();
  }

  if (noLogPrintOnScreen) {
    // ESFUTURE: Get period from configuration file
    pThread = new std::thread(threadFunc, 1);
  }

  while (engine.doNextEvent())
    ;

  cleanup(0);

  return 0;
}

void cleanup(int) {
  uint64_t tick;

  killLock.lock();

  tick = engine.getCurrentTick();

  if (tick == 0) {
    // Exit program
    exit(0);
  }

  // Wait for last progress output
  if (pThread) {
    pThread->join();

    delete pThread;
  }

  // Erase progress
  printf("\33[2K                                                           \r");

  engine.printStats(std::cout);

  for (int i = 0; i < num_instance; ++i) {
    pInstances[i]->destroyInstance();
    delete pInstances[i];
  }

  pHost->destroyHost();
  delete pHost;

  std::cout << "End of simulation @ tick " << tick << std::endl;

  // Exit program
  exit(0);
}

void threadFunc(int tick) {
  uint64_t current;
  uint64_t old = 0;
  float progress;
  auto duration = std::chrono::seconds(tick);
  BIL::Progress data;

  while (true) {
    std::this_thread::sleep_for(duration);

    if (killLock.try_lock()) {
      killLock.unlock();
    }
    else {
      break;
    }

    engine.getStat(current);

    printf("Engine Speed: (%.2f ops)\n", (double)(current - old) / tick);

    for (int i = 0; i < num_instance; ++i) {
      pInstances[i]->getProgress(progress, data);
      printf("SSD %d: Progress: %.2f%%, IOPS: %ld, BW: %ld MB/s, Avg. Lat: %ld ns\n",
              i, progress * 100.f, data.iops, data.bandwidth / 1000 / 1000, data.latency / 1000);
    }

    fflush(stdout);

    for(int i = 0; i < num_instance + 1; ++i) {
      printf("\033[1A\033[K\r");
    }

    old = current;
  }
}