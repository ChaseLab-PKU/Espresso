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

#pragma once

#ifndef __DRIVERS_NVME__
#define __DRIVERS_NVME__

#include <list>
#include <queue>

#include "bil/interface.hh"
#include "sil/nvme/prp.hh"
#include "sil/nvme/queue.hh"
#include "simplessd/hil/nvme/interface.hh"
#include "simplessd/util/interface.hh"
#include "simplessd/cpu/cpu.hh"
#include "sim/instance.hh"
#include "sim/host.hh"

#define QUEUE_ENTRY_ADMIN 256
#define QUEUE_ENTRY_IO 1024

namespace SIL {

namespace NVMe {

// Copied from SimpleSSD-FullSystem
typedef struct _DMAEntry {
  uint64_t beginAt;
  uint64_t finishedAt;
  uint64_t addr;
  uint64_t size;
  uint8_t *buffer;
  void *context;
  SimpleSSD::DMAFunction func;

  _DMAEntry(SimpleSSD::DMAFunction &f)
      : beginAt(0),
        finishedAt(0),
        addr(0),
        size(0),
        buffer(nullptr),
        context(nullptr),
        func(f) {}
} DMAEntry;

typedef std::function<void(uint16_t, uint32_t, void *)> ResponseHandler;

typedef struct _CommandEntry {
  uint16_t iv;  // Same as Queue ID
  uint16_t opcode;
  uint16_t cid;
  void *context;

  ResponseHandler callback;

  bool isRemoteReq;
  bool isSimpleSolution;
  bool isCopyBackIO;
  uint8_t debtorSSDID;
  uint16_t debtorSQID;
  uint8_t creditorSSDID;
  uint16_t creditorSQID;

  _CommandEntry(uint16_t i, uint16_t o, uint16_t c, void *p, ResponseHandler &f)
      : iv(i), opcode(o), cid(c), context(p), callback(f),
        isRemoteReq(false), isSimpleSolution(false), isCopyBackIO(false),
        debtorSSDID(UINT8_MAX), debtorSQID(UINT16_MAX), creditorSSDID(UINT8_MAX), creditorSQID(UINT16_MAX) {}
  _CommandEntry(uint16_t i, uint16_t o, uint16_t c, void *p, ResponseHandler &f, bool isRemoteReq, bool isSimpleSolution, bool isCopyBackIO, uint8_t dssd, uint16_t dsq, uint8_t cssd, uint16_t csq)
      : iv(i), opcode(o), cid(c), context(p), callback(f), 
        isRemoteReq(isRemoteReq), isSimpleSolution(isSimpleSolution), isCopyBackIO(isCopyBackIO),
        debtorSSDID(dssd), debtorSQID(dsq), creditorSSDID(cssd), creditorSQID(csq) {}
} CommandEntry;

typedef struct _IOWrapper {
  uint64_t id;
  PRP *prp;
  bool readValidPage;
  std::function<void(uint64_t, bool)> bioCallback;

  _IOWrapper(uint64_t i, PRP *p, std::function<void(uint64_t, bool)> &f)
      : id(i), prp(p), readValidPage(true), bioCallback(f) {}
} IOWrapper;

// Information of creditor Queues mapped with each debtor queue
struct BorrowRelation {
  uint8_t creditorSSDID = UINT8_MAX;
  uint16_t creditorSQID = UINT16_MAX;
  uint16_t creditorIV = UINT16_MAX;
  uint32_t tenancy = 0;
};

struct DebtorHandler {
  std::vector<BorrowRelation> relations;

  set<uint64_t> debtStorage; // Data storaged in creditor SSD
};

struct CreditorHandler {
  uint8_t debtorSSDID = UINT8_MAX;
  uint16_t debtorIV = UINT16_MAX;
  uint16_t debtorSQID = UINT16_MAX;

  set<uint64_t> creditStorage; // Data belong to debtor SSD
};

class Driver : public BIL::DriverInterface, SimpleSSD::HIL::NVMe::Interface {
 private:
  // PCI Express (for DMA throttling)
  SimpleSSD::PCIExpress::PCIE_GEN pcieGen;
  uint8_t pcieLane;
  Host *pHost = nullptr;
  SimpleSSD::CPU::CPU *pCPU = nullptr;

  // DMA scheduling
  SimpleSSD::Event dmaReadEvent;
  SimpleSSD::Event dmaWriteEvent;
  std::queue<DMAEntry> dmaReadQueue;
  std::queue<DMAEntry> dmaWriteQueue;
  bool dmaReadPending;
  bool dmaWritePending;

  // NVMe Identify
  uint64_t capacity;
  uint32_t LBAsize;
  uint32_t namespaceID;

  // Queue
  uint16_t maxQueueEntries;
  uint16_t adminCommandID;
  uint16_t ioCommandID;
  Queue *adminSQ;
  Queue *adminCQ;
  Queue *ioSQ;
  Queue *ioCQ;
  std::list<CommandEntry> pendingCommandList;

  // Espresso (Common)
  uint8_t ssdID;
  uint8_t frontBusyValue = 0;

  // Espresso (Creditor)
  uint8_t maxComputeLendOut;
  uint8_t maxComputeBorrowIn;
  uint8_t createdCreditorResources;   // only used in initialization
  std::vector<Queue *> creditorSQs;
  std::vector<Queue *> creditorCQs;
  std::vector<CreditorHandler> creditorHandlers;

  // SimpleSolution (i.e., virtualization + harvesting)
  bool simpleSolution = false;
  // 0: ideal: never copy back; 
  // 1: interval: trigger copyback every interval; 
  // 2: cumulative: trigger copyback if accumulated data capacity > threshold; 
  // 3: worst: trigger copyback right now
  uint8_t copyBackMethod = 0;
  uint64_t copyBackInterval = 0;  // useful when copyBackMethod = 1, in ticks
  uint64_t copyBackCapThreshold = 0; // useful when copyBackMethod = 2, in bytes
  uint64_t lastCopyBackTick = 0;

  // Espresso (Debtor)
  std::vector<DebtorHandler> debtorHandlers;  // We only have one I/O Queue pair now

  // Random
  uint64_t randseed;
  uint64_t randmax;
  std::mt19937_64 randengine;
  std::uniform_int_distribution<uint64_t> randgen;

  // Output Directory
  std::string output_dir;

  uint64_t pcieBusy = 0;
  uint64_t pcieLoad = 0;


  void dmaReadDone();
  void submitDMARead();
  void dmaWriteDone();
  void submitDMAWrite();

  void increaseCommandID(uint16_t &);

  void _init0(uint16_t, void *);
  void _init1(uint16_t, void *);
  void _init2(uint16_t, void *);
  void _init3(uint16_t, uint32_t, void *);
  void _init4(void *);
  void _init5(void *);

  void _io(uint16_t, void *);

  void submitCommand(uint16_t, uint8_t *, ResponseHandler &, void *, bool = false);

  void initResourceBorrow(uint16_t);
  void initResourceLend(uint16_t, uint8_t, uint16_t, uint16_t, uint8_t);

  void cancelResourceBorrow(uint16_t, uint8_t);
  void cancelResourceLend(uint16_t);
  void creditorCancelLend(uint16_t, uint8_t);

  BorrowRelation *pickBorrowRelation(DebtorHandler &);

  bool shouldCopyBack(uint64_t, uint64_t);
  void doCopyBackWriteDebtor(DebtorHandler &);
  void doCopyBackReadCreditor(CreditorHandler&);
  void submitCopyBackIO(BIL::BIO_TYPE, uint64_t, uint64_t);

  void handleCQEntry(uint16_t iv, uint32_t cqdata[4], bool &);

 public:
  Driver(Engine &, Host *, SimpleSSD::ConfigReader &, SimpleSSD::CPU::CPU *, uint8_t, std::string);
  ~Driver();

  // BIL::DriverInterface
  void init(std::function<void()> &) override;
  void getInfo(uint64_t &, uint32_t &) override;
  void submitIO(BIL::BIO &) override;

  void initStats(std::vector<SimpleSSD::Stats> &) override;
  void getStats(std::vector<double> &) override;

  // SimpleSSD::DMAInterface
  void dmaRead(uint64_t, uint64_t, uint8_t *, SimpleSSD::DMAFunction &,
               void * = nullptr) override;
  void dmaWrite(uint64_t, uint64_t, uint8_t *, SimpleSSD::DMAFunction &,
                void * = nullptr) override;

  // SimpleSSD::HIL::NVMe::Interface
  void updateInterrupt(uint16_t, bool) override;
  void getVendorID(uint16_t &, uint16_t &) override;
  void debtorCancelBorrowOne(uint8_t) override;
};

}  // namespace NVMe

}  // namespace SIL

#endif
