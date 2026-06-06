#pragma once

#ifndef __ESPRESSO_ESPRESSO__
#define __ESPRESSO_ESPRESSO__

#include <deque>
#include <vector>

#include "sim/statistics.hh"
#include "sim/config_reader.hh"
#include "sim/simulator.hh"
#include "cpu/cpu.hh"
#include "dram/remote.hh"
#include "util/def.hh"

namespace SimpleSSD {

namespace HIL {class HIL;
namespace NVMe {class Controller;}
}
namespace ICL {class ICL;}
namespace FTL {class FTL;
class LRU;
}
namespace PAL {class PAL;}

namespace ESPRESSO {

enum BusyStatus {
  IDLE = 0,
  NORMAL = 1,
  BUSY = 2,
};

struct BorrowSettingInfo {
  uint8_t debtorFrontBusyValue = 0xFF;
  uint8_t creditorSSDID = 0xFF;
  uint16_t creditorSQID = 0xFFFF;
  uint8_t creditorFrontBusyValue = 0xFF;
  uint32_t tenancy = 0;

  BorrowSettingInfo() {};
};

// Lendable Resource Descriptor
struct LendResDescriptor {
  uint16_t  creditorSQID = 0xFFFF;
  uint8_t   creditorFrontBusyValue = 0xFF;
  uint8_t   borrowSSDID = 0xFF;  // Borrowed by Which SSD, 0xFF means none
  bool      borrowed = false;
  bool      lendable = false;
  uint32_t  tenancy = 0;  // help handle how huch request
  uint64_t  lendableGhostCacheCapacity = 0;
  uint8_t   dramBorrowSSDID = 0xFF;
  bool      dramBorrowed = false;
  bool      dramLendable = false;
};

typedef std::function<void(uint64_t, void *)> CPUFuction;

class ESPRESSO : public StatObject {
 private:
  ConfigReader &conf;

  CPU::CPU *pCPU;
  HIL::NVMe::Controller *pController;

  HIL::HIL *pHIL = nullptr;
  ICL::ICL *pICL = nullptr;
  FTL::FTL *pFTL = nullptr;
  PAL::PAL *pPAL = nullptr;

  FTL::LRU *pGhostCache = nullptr;

  Event espressoPeriodEvent;
  CPUFuction espressoPeriodFunc;

  BusyStatus frontBusyStatus = IDLE;
  BusyStatus backBusyStatus = IDLE;

  bool enable = false;
  bool enableDRAMHarvest = false;
  bool simpleSolution = false;
  uint64_t checkBusyInterval;

  std::vector<LendResDescriptor> lendResDescriptors;
  uint64_t lendableGhostCacheCapacity = 0;

  std::vector<BorrowSettingInfo> activeBorrows;
  std::deque<BorrowSettingInfo> pendingBorrowSetups;
  std::deque<BorrowSettingInfo> awaitingHostConfirm;
  std::deque<uint16_t> pendingCancelLendSQIDs;
  std::deque<uint8_t> pendingCancelLendDebtorSSDIDs;
  bool borrowProbeInFlight = false;

  std::ofstream busyRatioFile;
  std::ofstream resourceFile;

  void initPeriodFunc();
  void periodFunc(uint64_t);
  double checkSelfReqRatio();
  void handleBusy(uint64_t);
  void handleDRAMHarvest(uint64_t);
  void handleGhostCacheRebalance();
  void tryBorrowOne(uint64_t);
  void tryDRAMBorrow(uint64_t);
  void handleRemoteLendResDescriptors(uint64_t, uint8_t);
  void handleRemoteDramLendDescriptors(uint64_t, uint8_t);
  void cancelLendOne(uint64_t);
  void cancelBorrowOne(uint64_t);
  void cancelDRAMLend(uint64_t);
  void cancelDRAMBorrow(uint64_t);
  void setLendOne();
  void setDRAMLend(uint64_t);
  void setGhostCacheBorrow(uint8_t, LendResDescriptor &);
  void cancelGhostCacheLend(LendResDescriptor &);
  void updateDRAMLendingState();
  uint8_t activeBorrowCount() const;
  uint8_t activeLendCount() const;
  bool isBorrowingFrom(uint8_t creditorSSDID) const;
  bool hasFreeLendSlot() const;
  bool borrowSetupPending() const;

 public:
  uint8_t ssdID;
  uint8_t maxComputeBorrowIn = 1;
  uint8_t maxComputeLendOut = 1;

  DRAM::AbstractDRAM *pDRAM;
  DRAM::RemoteDRAM *pRemoteDRAM;

  uint16_t borrowSettingDebtorSQID = UINT16_MAX;

  bool lending = false;
  bool borrowing = false;
  bool pending = false;

  bool dramLending = false;
  bool dramBorrowing = false;
  bool dramPending = false;
  uint8_t dramCreditorSSDID = 0xFF;

  uint8_t frontBusyValue = 0;

  uint32_t selfReqCnt = 0;
  uint32_t remoteReqCnt = 0;
  double selfReqRatio = 1;

  std::string output_dir;

  ESPRESSO(ConfigReader &, CPU::CPU *, HIL::NVMe::Controller*, uint8_t, std::string);
  ~ESPRESSO();
  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;

  void updatePendingState();
  void updateBorrowingState();
  void updateLendingState();

  uint64_t getRemoteDRAMID(uint8_t);
  uint8_t getDebtorSSDID(uint16_t sqid);
  void submitRemotePALReq(uint8_t, PAL::Request &, uint64_t &);
  void triggerRemoteUpdateSubmit(uint8_t);
  void commitRemotePALReq(uint8_t, PAL::Request &);
  void commitRemoteFTLReq(uint8_t, PAL::Request);
  void submitRemoteDMAWrite(uint8_t, uint64_t, uint64_t, uint8_t *, DMAFunction &, void *);
  void submitRemoteDMARead(uint8_t, uint64_t, uint64_t, uint8_t *, DMAFunction &, void *);
  void conductRemoteDRAMWrite(uint8_t, void *, uint64_t, uint64_t &);
  void conductRemoteDRAMRead(uint8_t, void *, uint64_t, uint64_t &);
  bool conductRemoteFTLMapFind(uint8_t, uint64_t, uint64_t &, void *, bool);
  bool conductRemoteFTLBlocksFind(uint8_t, uint32_t, void *);
  void conductRemoteFTLMapModify(uint8_t, uint64_t, uint64_t &);
  void conductRemoteFTLMapEmplaceEmpty(uint8_t, uint64_t);
  bool conductRemoteFTLGetBlockToWrite(uint8_t, uint32_t, void *);

  BorrowSettingInfo getBorrowSettingInfo(uint16_t);
  bool hasPendingBorrowSetup() const;
  bool notifyBorrowSetupOnCQ(uint8_t &creditorSSDID);
  bool hasPendingCancelLend() const;
  BorrowSettingInfo peekPendingBorrowSetup() const;
  uint16_t peekPendingCancelLendSQID() const;
  uint8_t peekPendingCancelLendDebtorSSDID() const;
  void popPendingCancelLend();

  void setHIL(HIL::HIL *p) {pHIL = p;}
  void setICL(ICL::ICL *p) {pICL = p;}
  void setFTL(FTL::FTL *p) {pFTL = p;}
  void setPAL(PAL::PAL *p) {pPAL = p;}
  void setDRAM(DRAM::AbstractDRAM *p) {pDRAM = p;}
  void setGhostCache(FTL::LRU *p) {pGhostCache = p;}
};

}  // namespace ESPRESSO

}  // namespace SimpleSSD

#endif
