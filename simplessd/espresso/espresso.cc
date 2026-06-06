#include "espresso.hh"
#include "pal/pal.hh"
#include "ftl/ftl.hh"
#include "hil/nvme/controller.cc"
#include "ftl/page_mapping.hh"

#include <algorithm>
#include <vector>

namespace SimpleSSD {

namespace ESPRESSO {

std::unordered_map<uint8_t, ESPRESSO*> globalEspressoMap;

ESPRESSO::ESPRESSO(ConfigReader &c, CPU::CPU *pCPU, HIL::NVMe::Controller* pController, uint8_t id, std::string od) : conf(c), pCPU(pCPU), pController(pController), ssdID(id), output_dir(od) {
  enable = conf.readBoolean(CONFIG_ESPRESSO, ESPRESSO_ENABLE);
  simpleSolution = conf.readBoolean(CONFIG_ESPRESSO, ESPRESSO_SIMPLE_SOLUTION);
  checkBusyInterval = conf.readUint(CONFIG_ESPRESSO, ESPRESSO_CHECK_BUSY_INTERVAL);
  maxComputeBorrowIn = (uint8_t)conf.readUint(CONFIG_ESPRESSO, ESPRESSO_MAX_COMPUTE_BORROW_IN);
  maxComputeLendOut = (uint8_t)conf.readUint(CONFIG_ESPRESSO, ESPRESSO_MAX_COMPUTE_LEND_OUT);
  enableDRAMHarvest = enable && !simpleSolution;

  if (maxComputeBorrowIn < 1 || maxComputeLendOut < 1) {
    panic("MaxComputeBorrowIn and MaxComputeLendOut must be >= 1");
  }
  
  initPeriodFunc();

  pRemoteDRAM = new DRAM::RemoteDRAM(conf);
  
  lendResDescriptors.resize(maxComputeLendOut);
  for (int i = 0; i < maxComputeLendOut; ++i) {
    lendResDescriptors[i].creditorSQID = 1 + 1 + i;
  }

  globalEspressoMap[ssdID] = this;

  std::string busyRatioFileName = output_dir + "/espresso_busy_ratio" + std::to_string(ssdID) + ".txt";
  busyRatioFile.open(busyRatioFileName);
  if (!busyRatioFile.is_open()) {
    panic("Cannot open busy ratio log file for espresso %d", ssdID);
  }

  std::string resourceFileName = output_dir + "/espresso_resource" + std::to_string(ssdID) + ".txt";
  resourceFile.open(resourceFileName);
  if (!resourceFile.is_open()) {
    panic("Cannot open resource log file for espresso %d", ssdID);
  }
  
}

ESPRESSO::~ESPRESSO() {
  delete pRemoteDRAM;
  busyRatioFile.close();
  resourceFile.close();
}

void ESPRESSO::initPeriodFunc() {
  espressoPeriodFunc = [this](uint64_t tick, void *) { periodFunc(tick); };
  espressoPeriodEvent = allocate([this](uint64_t tick) { 
    pCPU->execute(CPU::ESPRESSO, CPU::CHECK_BUSY, espressoPeriodFunc);
    schedule(espressoPeriodEvent, tick + checkBusyInterval * 1000000ULL);
  });
  
  schedule(espressoPeriodEvent, getTick() + checkBusyInterval * 1000000ULL);
}

void ESPRESSO::periodFunc(uint64_t tick) {
  handleBusy(tick);

  if (enableDRAMHarvest) {
    handleDRAMHarvest(tick);
    handleGhostCacheRebalance();
  }
}

double ESPRESSO::checkSelfReqRatio() {
  if (selfReqCnt + remoteReqCnt == 0) return 0;

  double ratio =  (double) selfReqCnt / (selfReqCnt + remoteReqCnt);
  selfReqCnt = 0;
  remoteReqCnt = 0;

  return ratio;
}

uint8_t ESPRESSO::activeBorrowCount() const {
  return (uint8_t)(activeBorrows.size() + pendingBorrowSetups.size() +
                   awaitingHostConfirm.size());
}

uint8_t ESPRESSO::activeLendCount() const {
  uint8_t count = 0;
  for (const auto &desc : lendResDescriptors) {
    if (desc.borrowed) {
      count++;
    }
  }
  return count;
}

bool ESPRESSO::isBorrowingFrom(uint8_t creditorSSDID) const {
  for (const auto &info : activeBorrows) {
    if (info.creditorSSDID == creditorSSDID) {
      return true;
    }
  }
  for (const auto &info : pendingBorrowSetups) {
    if (info.creditorSSDID == creditorSSDID) {
      return true;
    }
  }
  for (const auto &info : awaitingHostConfirm) {
    if (info.creditorSSDID == creditorSSDID) {
      return true;
    }
  }
  return false;
}

bool ESPRESSO::hasFreeLendSlot() const {
  for (const auto &desc : lendResDescriptors) {
    if (!desc.lendable && !desc.borrowed) {
      return true;
    }
  }
  return false;
}

bool ESPRESSO::borrowSetupPending() const {
  return borrowProbeInFlight || !pendingBorrowSetups.empty() ||
         !awaitingHostConfirm.empty();
}

void ESPRESSO::updatePendingState() {
  pending = borrowSetupPending() || !pendingCancelLendSQIDs.empty();
}

void ESPRESSO::updateBorrowingState() {
  borrowing = !activeBorrows.empty();
}

void ESPRESSO::updateLendingState() {
  lending = activeLendCount() > 0;
  for (const auto &desc : lendResDescriptors) {
    if (desc.lendable) {
      lending = true;
      break;
    }
  }
}

void ESPRESSO::updateDRAMLendingState() {
  dramLending = false;
  for (const auto &desc : lendResDescriptors) {
    if (desc.dramBorrowed || desc.dramLendable) {
      dramLending = true;
      break;
    }
  }
}

bool ESPRESSO::hasPendingBorrowSetup() const {
  return !pendingBorrowSetups.empty();
}

bool ESPRESSO::hasPendingCancelLend() const {
  return !pendingCancelLendSQIDs.empty();
}

BorrowSettingInfo ESPRESSO::peekPendingBorrowSetup() const {
  return pendingBorrowSetups.front();
}

uint16_t ESPRESSO::peekPendingCancelLendSQID() const {
  return pendingCancelLendSQIDs.front();
}

uint8_t ESPRESSO::peekPendingCancelLendDebtorSSDID() const {
  return pendingCancelLendDebtorSSDIDs.front();
}

void ESPRESSO::popPendingCancelLend() {
  pendingCancelLendSQIDs.pop_front();
  pendingCancelLendDebtorSSDIDs.pop_front();
  updatePendingState();
}

void ESPRESSO::handleBusy(uint64_t tick) {
  double frontBusyRatio = pCPU->checkBusy();
  double backBusyRatio = pPAL->checkBusy();

  busyRatioFile << tick << "\t" << frontBusyRatio << "\t" << backBusyRatio << endl;
  if (!enable) return;
  
  selfReqRatio = checkSelfReqRatio();
  
  if (frontBusyRatio < 0.5) frontBusyStatus = IDLE;
  else if (frontBusyRatio < 0.8) frontBusyStatus = NORMAL;
  else frontBusyStatus = BUSY;

  if (backBusyRatio < 0.5) backBusyStatus = IDLE;
  else if (backBusyRatio < 0.8) backBusyStatus = NORMAL;
  else backBusyStatus = BUSY;

  frontBusyValue = std::round(frontBusyRatio * 10);
  frontBusyValue = frontBusyValue > 10 ? 10 : frontBusyValue;

  bool needBorrow = false;
  bool canLend = false;

  if (pending) {
    return;
  }

  if (frontBusyStatus == BUSY && backBusyStatus == BUSY) {
    needBorrow = false;
    canLend = false;
  }
  else if (frontBusyStatus == BUSY && backBusyStatus == NORMAL) {
    if (selfReqRatio * (double) frontBusyValue > 4) {
      needBorrow = true;
    }
    canLend = false;
  }
  else if (frontBusyStatus == BUSY && backBusyStatus == IDLE) {
    if (selfReqRatio * (double) frontBusyValue > 4) {
      needBorrow = true;
    }
    canLend = false;
  }
  else if (frontBusyStatus == NORMAL && backBusyStatus == BUSY) {
    needBorrow = false;
    canLend = false;
  }
  else if (frontBusyStatus == NORMAL && backBusyStatus == NORMAL) {
    if (selfReqRatio * (double) frontBusyValue > 4) {
      needBorrow = true;
    }
    canLend = false;
  }
  else if (frontBusyStatus == NORMAL && backBusyStatus == IDLE) {
    if (selfReqRatio * (double) frontBusyValue > 4) {
      needBorrow = true;
    }
    canLend = true;
  }
  else if (frontBusyStatus == IDLE && backBusyStatus == BUSY) {
    if (borrowing) {
      if (frontBusyValue <= 2) {
        needBorrow = false;
        canLend = true;
      }
    }
    else {
      needBorrow = false;
      canLend = true;
    }
  }
  else if (frontBusyStatus == IDLE && backBusyStatus == NORMAL) {
    if (borrowing) {
      if (frontBusyValue <= 2) {
        needBorrow = false;
        canLend = true;
      }
    }
    else {
      needBorrow = false;
      canLend = true;
    }
  }
  else if (frontBusyStatus == IDLE && backBusyStatus == IDLE) {
    if (borrowing) {
      if (frontBusyValue <= 2) {
        needBorrow = false;
        canLend = true;
      }
    }
    else {
      needBorrow = false;
      canLend = true;
    }
  }
  
  if (needBorrow) {
    if (lending) {
      cancelLendOne(tick);
    }
    else if (activeBorrowCount() < maxComputeBorrowIn) {
      tryBorrowOne(tick);
    }
  }
  else if (canLend) {
    if (!activeBorrows.empty()) {
      cancelBorrowOne(tick);
    }
    else if (activeLendCount() < maxComputeLendOut && hasFreeLendSlot()) {
      setLendOne();
    }
  }
}

void ESPRESSO::handleDRAMHarvest(uint64_t tick) {
  if (!pGhostCache || dramPending) return;

  if (!pGhostCache->hasMRCData()) return;

  bool needBorrow = pGhostCache->needsStartDRAMBorrow();
  bool canLend = pGhostCache->canStartDRAMLend();
  bool shouldStopBorrow = pGhostCache->shouldStopDRAMBorrow();
  bool shouldStopLend = pGhostCache->shouldStopDRAMLend();

  if (needBorrow) {
    if (dramLending) cancelDRAMLend(tick);
    else if (!dramBorrowing) tryDRAMBorrow(tick);
  }
  else if (canLend) {
    if (dramBorrowing && shouldStopBorrow) {
      cancelDRAMBorrow(tick);
    }
    else if (dramLending && shouldStopLend) {
      cancelDRAMLend(tick);
    }
    else if (!dramLending) {
      setDRAMLend(tick);
    }
  }
  else {
    if (dramBorrowing && shouldStopBorrow) cancelDRAMBorrow(tick);
    if (dramLending && shouldStopLend) cancelDRAMLend(tick);
  }
}

void ESPRESSO::handleGhostCacheRebalance() {
  if (dramLending) {
    for (int i = 0; i < maxComputeLendOut; ++i) {
      if (lendResDescriptors[i].dramBorrowed) {
        uint8_t debtorSSDID = lendResDescriptors[i].dramBorrowSSDID;
        ESPRESSO* remoteEspresso = globalEspressoMap[debtorSSDID];

        std::vector<uint64_t> mrc_keys;
        std::unordered_map<uint64_t, double> mrc;
        pGhostCache->getMRC(mrc_keys, mrc);

        int adjust = remoteEspresso->pGhostCache->debtorRebalanceGhostCache(mrc_keys, mrc, pGhostCache->capacity);

        if (adjust > 0) {
          pGhostCache->shrinkCapacity(adjust, ssdID);
          remoteEspresso->pGhostCache->balloonCapacity(adjust, ssdID);
        }
        else if(adjust < 0) {
          if (remoteEspresso->pGhostCache->capacity + adjust < remoteEspresso->pGhostCache->origCapacity) {
            adjust = -remoteEspresso->pGhostCache->capacity + remoteEspresso->pGhostCache->origCapacity;
          }
          if (adjust != 0) {
            pGhostCache->balloonCapacity(-adjust, ssdID);
            remoteEspresso->pGhostCache->shrinkCapacity(-adjust, ssdID);
          }
        }
      }
    }
  } 
}

void ESPRESSO::tryBorrowOne(uint64_t tick) {
  if (borrowProbeInFlight || !pendingBorrowSetups.empty()) {
    return;
  }

  std::vector<uint8_t> candidates;
  for (const auto &iter : globalEspressoMap) {
    if (iter.second != this && !isBorrowingFrom(iter.first)) {
      candidates.push_back(iter.first);
    }
  }
  std::sort(candidates.begin(), candidates.end());

  for (uint8_t creditorSSDID : candidates) {
    ESPRESSO *remoteEspresso = globalEspressoMap[creditorSSDID];
    bool hasLendable = false;
    for (const auto &desc : remoteEspresso->lendResDescriptors) {
      if (desc.lendable && !desc.borrowed) {
        hasLendable = true;
        break;
      }
    }
    if (!hasLendable) {
      continue;
    }

    borrowProbeInFlight = true;
    updatePendingState();

    uint64_t finishAt = tick;
    DRAM::RemoteDRAM::RequestInfo req(0, pRemoteDRAM->id(), remoteEspresso->pRemoteDRAM->id());
    pRemoteDRAM->read(&req, sizeof(LendResDescriptor) * remoteEspresso->maxComputeLendOut, finishAt);

    if (finishAt == 0) finishAt = tick;

    uint64_t delay = finishAt - tick;

    CPUFuction handler = [this](uint64_t tick, void *context) {
      uint8_t id = (uint64_t)context;
      borrowProbeInFlight = false;
      handleRemoteLendResDescriptors(tick, id);
      updatePendingState();
    };
    pCPU->execute(CPU::ESPRESSO, CPU::READ_REMOTE_LENDABLE_RESOURCES_DESCRIPTOR, handler,
                  (void *)(uint64_t)creditorSSDID, delay);
    return;
  }
}

void ESPRESSO::handleRemoteLendResDescriptors(uint64_t tick, uint8_t creditorSSDID) {
  if (activeBorrowCount() >= maxComputeBorrowIn || isBorrowingFrom(creditorSSDID)) {
    return;
  }

  ESPRESSO* remoteEspresso = globalEspressoMap[creditorSSDID];

  for (auto &iter : remoteEspresso->lendResDescriptors) {
    if (iter.lendable && !iter.borrowed) {
      iter.borrowed = true;
      iter.borrowSSDID = ssdID;
      remoteEspresso->pending = true;
      remoteEspresso->updateLendingState();

      BorrowSettingInfo info;
      info.debtorFrontBusyValue = frontBusyValue;
      info.creditorSSDID = creditorSSDID;
      info.creditorSQID = iter.creditorSQID;
      info.creditorFrontBusyValue = iter.creditorFrontBusyValue;
      info.tenancy = iter.tenancy;

      borrowSettingDebtorSQID = 1;
      pendingBorrowSetups.push_back(info);
      updatePendingState();

      resourceFile << "tick "<< tick << ": SSD " <<  std::to_string(ssdID)
                   <<" set borrow from SSD " << std::to_string(creditorSSDID) << endl;

      break;
    }
  }
}

void ESPRESSO::tryDRAMBorrow(uint64_t tick) {
  dramPending = true;
  uint64_t finishAt = tick;
  for (auto iter : globalEspressoMap) {
    if (iter.second != this) {
      DRAM::RemoteDRAM::RequestInfo req(0, pRemoteDRAM->id(), iter.second->pRemoteDRAM->id());

      pRemoteDRAM->read(&req, sizeof(LendResDescriptor) * iter.second->maxComputeLendOut, finishAt);

      if (finishAt == 0) finishAt = tick;

      uint64_t delay = finishAt - tick;

      CPUFuction handler = [this](uint64_t tick, void *context) {
        uint8_t creditorSSDID = (uint64_t)context;
        handleRemoteDramLendDescriptors(tick, creditorSSDID);
      };
      pCPU->execute(CPU::ESPRESSO, CPU::READ_REMOTE_LENDABLE_RESOURCES_DESCRIPTOR, handler, (void *)(uint64_t)iter.second->ssdID, delay);
    }
  }
}

void ESPRESSO::handleRemoteDramLendDescriptors(uint64_t tick, uint8_t creditorSSDID) {
  if (dramBorrowing) {
    dramPending = false;
    return;
  }

  ESPRESSO *remoteEspresso = globalEspressoMap[creditorSSDID];

  for (auto &iter : remoteEspresso->lendResDescriptors) {
    if (iter.dramLendable && !iter.dramBorrowed &&
        iter.lendableGhostCacheCapacity > 0) {
      iter.dramBorrowed = true;
      iter.dramBorrowSSDID = ssdID;
      dramCreditorSSDID = creditorSSDID;

      setGhostCacheBorrow(creditorSSDID, iter);

      iter.dramLendable = false;
      dramBorrowing = true;
      dramPending = false;

      resourceFile << "tick " << tick << ": SSD " << std::to_string(ssdID)
                   << " set DRAM borrow from SSD " << std::to_string(creditorSSDID)
                   << " pages " << iter.lendableGhostCacheCapacity << endl;
      return;
    }
  }

  dramPending = false;
}

void ESPRESSO::cancelLendOne(uint64_t tick) {
  for (int i = maxComputeLendOut - 1; i >= 0; --i) {
    if (lendResDescriptors[i].borrowed) {
      uint8_t debtorSSDID = lendResDescriptors[i].borrowSSDID;

      pendingCancelLendSQIDs.push_back(i + 2);
      pendingCancelLendDebtorSSDIDs.push_back(debtorSSDID);
      globalEspressoMap[debtorSSDID]->pending = true;

      lendResDescriptors[i].borrowed = false;
      lendResDescriptors[i].borrowSSDID = 0xFF;
      lendResDescriptors[i].lendable = false;
      updateLendingState();
      updatePendingState();

      resourceFile << "tick "<< tick << ": SSD "<< std::to_string(ssdID)
                   <<" cancel lend with SSD "
                   << std::to_string(debtorSSDID) << endl;

      return;
    }
  }
}

void ESPRESSO::cancelBorrowOne(uint64_t tick) {
  if (activeBorrows.empty()) {
    return;
  }

  BorrowSettingInfo info = activeBorrows.back();
  activeBorrows.pop_back();
  updateBorrowingState();

  ESPRESSO* remoteEspresso = globalEspressoMap[info.creditorSSDID];
  for (int i = 0; i < maxComputeLendOut; ++i) {
    if (remoteEspresso->lendResDescriptors[i].borrowed &&
        remoteEspresso->lendResDescriptors[i].borrowSSDID == ssdID) {
      remoteEspresso->lendResDescriptors[i].borrowed = false;
      remoteEspresso->lendResDescriptors[i].borrowSSDID = 0xFF;
      remoteEspresso->lendResDescriptors[i].lendable = false;
      remoteEspresso->updateLendingState();
      break;
    }
  }

  remoteEspresso->pending = true;
  pending = true;

  resourceFile << "tick "<< tick << ": SSD " << std::to_string(ssdID)
               <<" cancel borrow from SSD " << std::to_string(info.creditorSSDID) << endl;

  pController->cancelBorrowOne(info.creditorSSDID);
}

void ESPRESSO::setLendOne() {
  for (int i = 0; i < maxComputeLendOut; ++i) {
    if (!lendResDescriptors[i].lendable && !lendResDescriptors[i].borrowed) {
      lendResDescriptors[i].creditorFrontBusyValue = frontBusyValue;
      lendResDescriptors[i].borrowed = false;
      lendResDescriptors[i].lendable = true;
      lendResDescriptors[i].tenancy = (10 - frontBusyValue) * 64;
      updateLendingState();
      return;
    }
  }
}

void ESPRESSO::setDRAMLend(uint64_t tick) {
  lendableGhostCacheCapacity = pGhostCache->computeLendableGhostCacheCapcity();
  if (lendableGhostCacheCapacity == 0) return;

  for (int i = 0; i < maxComputeLendOut; ++i) {
    lendResDescriptors[i].dramBorrowed = false;
    lendResDescriptors[i].dramLendable = true;
    lendResDescriptors[i].lendableGhostCacheCapacity =
        lendableGhostCacheCapacity / maxComputeLendOut;
  }

  dramLending = true;

  resourceFile << "tick " << tick << ": SSD " << std::to_string(ssdID)
               << " set DRAM lend pages " << lendableGhostCacheCapacity << endl;
}

void ESPRESSO::cancelDRAMLend(uint64_t tick) {
  for (int i = 0; i < maxComputeLendOut; ++i) {
    if (lendResDescriptors[i].dramBorrowed) {
      uint8_t debtorSSDID = lendResDescriptors[i].dramBorrowSSDID;
      ESPRESSO *remoteEspresso = globalEspressoMap[debtorSSDID];

      cancelGhostCacheLend(lendResDescriptors[i]);

      lendResDescriptors[i].dramBorrowed = false;
      lendResDescriptors[i].dramLendable = false;
      lendResDescriptors[i].dramBorrowSSDID = 0xFF;
      remoteEspresso->dramBorrowing = false;
      remoteEspresso->dramCreditorSSDID = 0xFF;
      updateDRAMLendingState();

      resourceFile << "tick " << tick << ": SSD " << std::to_string(ssdID)
                   << " cancel DRAM lend with SSD "
                   << std::to_string(debtorSSDID) << endl;
      return;
    }
  }

  if (dramLending) {
    for (int i = 0; i < maxComputeLendOut; ++i) {
      lendResDescriptors[i].dramLendable = false;
    }
    updateDRAMLendingState();

    resourceFile << "tick " << tick << ": SSD " << std::to_string(ssdID)
                 << " stop DRAM lend" << endl;
  }
}

void ESPRESSO::cancelDRAMBorrow(uint64_t tick) {
  if (!dramBorrowing) return;

  ESPRESSO *remoteEspresso = globalEspressoMap[dramCreditorSSDID];
  for (int i = 0; i < maxComputeLendOut; ++i) {
    if (remoteEspresso->lendResDescriptors[i].dramBorrowed &&
        remoteEspresso->lendResDescriptors[i].dramBorrowSSDID == ssdID) {
      remoteEspresso->cancelGhostCacheLend(remoteEspresso->lendResDescriptors[i]);
      remoteEspresso->lendResDescriptors[i].dramBorrowed = false;
      remoteEspresso->lendResDescriptors[i].dramLendable = false;
      remoteEspresso->updateDRAMLendingState();
      break;
    }
  }

  dramBorrowing = false;
  dramCreditorSSDID = 0xFF;

  resourceFile << "tick " << tick << ": SSD " << std::to_string(ssdID)
               << " cancel DRAM borrow" << endl;
}

void ESPRESSO::setGhostCacheBorrow(uint8_t creditorSSDID, LendResDescriptor &descriptor) {
  assert(ssdID == descriptor.dramBorrowSSDID);

  ESPRESSO* remoteEspresso = globalEspressoMap[creditorSSDID];

  pGhostCache->balloonCapacity(descriptor.lendableGhostCacheCapacity, creditorSSDID);
  remoteEspresso->pGhostCache->shrinkCapacity(descriptor.lendableGhostCacheCapacity, creditorSSDID);
}

void ESPRESSO::cancelGhostCacheLend(LendResDescriptor &descriptor) {
  if (descriptor.dramBorrowSSDID == 0xFF) {
    return;
  }

  ESPRESSO* remoteEspresso = globalEspressoMap[descriptor.dramBorrowSSDID];

  uint64_t num = remoteEspresso->pGhostCache->debtorGetNumBorrowedPage(ssdID);
  if (num == 0) {
    return;
  }

  pGhostCache->balloonCapacity(num, ssdID);
  remoteEspresso->pGhostCache->shrinkCapacity(num, ssdID);

  assert(remoteEspresso->pGhostCache->debtorGetNumBorrowedPage(ssdID) == 0);
}

void ESPRESSO::getStatList(std::vector<Stats> &list, std::string prefix) {
  Stats temp;

  temp.name = prefix + "None";
  temp.desc = "None";
  list.push_back(temp);
}

void ESPRESSO::getStatValues(std::vector<double> &values)  {
  values.push_back(0);
}

void ESPRESSO::resetStatValues() {
  
}

uint64_t ESPRESSO::getRemoteDRAMID(uint8_t remoteSSDID) {
  return globalEspressoMap[remoteSSDID]->pRemoteDRAM->id();
}

uint8_t ESPRESSO::getDebtorSSDID(uint16_t sqid) {
  return lendResDescriptors[sqid-2].borrowSSDID;
}

void ESPRESSO::submitRemotePALReq(uint8_t debtorSSDID, PAL::Request &req, uint64_t &tick) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];

  DRAM::RemoteDRAM::RequestInfo rdReq(0x0, pRemoteDRAM->id(), pRemoteESPRESSO->pRemoteDRAM->id());
  uint64_t tick_copy = tick;
  pRemoteDRAM->write(&rdReq, 16, tick);

  if(tick == 0) tick = tick_copy;

  if (req.reqType == READ || req.reqType == WRITE) {
    pRemoteESPRESSO->pFTL->submitRemotePALReq(req, tick);
  }
  else assert(0);

  pCPU->applyLatency(CPU::ESPRESSO, CPU::SUBMIT_REMOTE_REQ, tick);
}

void ESPRESSO::triggerRemoteUpdateSubmit(uint8_t ssdID) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[ssdID];

  pRemoteESPRESSO->pFTL->triggerRemoteUpdateSubmit();
}

void ESPRESSO::commitRemotePALReq(uint8_t creditorSSDID, PAL::Request &req) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[creditorSSDID];

  DRAM::RemoteDRAM::RequestInfo rdReq(0x0, pRemoteDRAM->id(), pRemoteESPRESSO->pRemoteDRAM->id());
  uint64_t tick_copy = req.finishAt;
  pRemoteDRAM->write(&rdReq, 4, req.finishAt);

  if(req.finishAt == 0) req.finishAt = tick_copy;
  
  pRemoteESPRESSO->pFTL->commitRemotePALReq(req);
}

void ESPRESSO::commitRemoteFTLReq(uint8_t creditorSSDID, PAL::Request req) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[creditorSSDID];

  DRAM::RemoteDRAM::RequestInfo rdReq(0x0, pRemoteDRAM->id(), pRemoteESPRESSO->pRemoteDRAM->id());
  uint64_t tick_copy = req.finishAt;
  pRemoteDRAM->write(&rdReq, 4, req.finishAt);

  if(req.finishAt == 0) req.finishAt = tick_copy;

  pRemoteESPRESSO->pFTL->commitRemoteFTLReq(req);
}

void ESPRESSO::submitRemoteDMAWrite(uint8_t debtorSSDID, uint64_t addr, uint64_t size, uint8_t *buffer, DMAFunction &func, void *context) {

  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];

  uint64_t now = getTick();
  uint64_t finishAt = now;

  DRAM::RemoteDRAM::RequestInfo rdReq(0x10, pRemoteDRAM->id(), pRemoteESPRESSO->pRemoteDRAM->id());
  pRemoteDRAM->write(&rdReq, 16, finishAt);

  if (finishAt == 0) {
    finishAt = now;
  }

  uint64_t delay = finishAt - now;

  CPUFuction handler = [this, addr, size, buffer, &func, context, pRemoteESPRESSO](uint64_t, void *) { 
    pRemoteESPRESSO->pController->submitRemoteDMAWrite(addr, size, buffer, func, context);
  };
  pCPU->execute(CPU::ESPRESSO, CPU::SUBMIT_REMOTE_REQ, handler, nullptr, delay);
}

void ESPRESSO::submitRemoteDMARead(uint8_t debtorSSDID, uint64_t addr, uint64_t size, uint8_t *buffer, DMAFunction &func, void *context) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];

  uint64_t now = getTick();
  uint64_t finishAt = now;
  DRAM::RemoteDRAM::RequestInfo rdReq(0x20, pRemoteDRAM->id(), pRemoteESPRESSO->pRemoteDRAM->id());
  pRemoteDRAM->write(&rdReq, 16, finishAt);

  if(finishAt == 0) {
    finishAt = now;
  }

  uint64_t delay = finishAt - now;

  CPUFuction handler = [this, addr, size, buffer, &func, context, pRemoteESPRESSO](uint64_t, void *) { 
    pRemoteESPRESSO->pController->submitRemoteDMARead(addr, size, buffer, func, context);
  };
  pCPU->execute(CPU::ESPRESSO, CPU::SUBMIT_REMOTE_REQ, handler, nullptr, delay);
}

void ESPRESSO::conductRemoteDRAMWrite(uint8_t debtorSSDID, void *req_info, uint64_t size, uint64_t &tick) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];
  pRemoteESPRESSO->pDRAM->write(req_info, size, tick);
}

void ESPRESSO::conductRemoteDRAMRead(uint8_t debtorSSDID, void *req_info, uint64_t size, uint64_t &tick) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];
  pRemoteESPRESSO->pDRAM->read(req_info, size, tick);
}

bool ESPRESSO::conductRemoteFTLMapFind(uint8_t debtorSSDID, uint64_t idx, uint64_t &tick, void *iter, bool feedMRC) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];
  return pRemoteESPRESSO->pFTL->conductRemoteFTLMapFind(idx, tick, iter, feedMRC);
}

bool ESPRESSO::conductRemoteFTLBlocksFind(uint8_t debtorSSDID, uint32_t idx, void *iter) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];
  return pRemoteESPRESSO->pFTL->conductRemoteFTLBlocksFind(idx, iter);
}

void ESPRESSO::conductRemoteFTLMapModify(uint8_t debtorSSDID, uint64_t idx, uint64_t &tick) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];
  pRemoteESPRESSO->pFTL->conductRemoteFTLMapModify(idx, tick);
}

void ESPRESSO::conductRemoteFTLMapEmplaceEmpty(uint8_t debtorSSDID, uint64_t idx) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];
  pRemoteESPRESSO->pFTL->conductRemoteFTLMapEmplaceEmpty(idx);
}

bool ESPRESSO::conductRemoteFTLGetBlockToWrite(uint8_t debtorSSDID, uint32_t nPage, void *iter) {
  ESPRESSO* pRemoteESPRESSO = globalEspressoMap[debtorSSDID];
  return pRemoteESPRESSO->pFTL->conductRemoteFTLGetBlockToWrite(nPage, iter);
}

bool ESPRESSO::notifyBorrowSetupOnCQ(uint8_t &creditorSSDID) {
  if (pendingBorrowSetups.empty()) {
    return false;
  }

  BorrowSettingInfo info = pendingBorrowSetups.front();
  pendingBorrowSetups.pop_front();
  awaitingHostConfirm.push_back(info);
  creditorSSDID = info.creditorSSDID;
  updatePendingState();
  return true;
}

BorrowSettingInfo ESPRESSO::getBorrowSettingInfo(uint16_t debtorSQID) {
  assert(borrowSettingDebtorSQID == debtorSQID);
  assert(!awaitingHostConfirm.empty());

  BorrowSettingInfo info = awaitingHostConfirm.front();
  awaitingHostConfirm.pop_front();
  activeBorrows.push_back(info);
  updateBorrowingState();
  updatePendingState();

  return info;
}

}  // namespace ESPRESSO

}  // namespace SimpleSSD
