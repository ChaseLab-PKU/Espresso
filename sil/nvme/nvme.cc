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

#include "sil/nvme/nvme.hh"

#include "simplessd/hil/nvme/controller.hh"
#include "simplessd/hil/nvme/def.hh"
#include "simplessd/util/algorithm.hh"
#include "nvme.hh"

namespace SIL {

namespace NVMe {

std::unordered_map<uint8_t, Driver *> globalEspressoDriverMap;

Driver::Driver(Engine &e, Host *ph, SimpleSSD::ConfigReader &conf, SimpleSSD::CPU::CPU *pCPU, uint8_t id, std::string od)
    : BIL::DriverInterface(e),
      pHost(ph),
      pCPU(pCPU),
      dmaReadPending(false),
      dmaWritePending(false),
      adminSQ(nullptr),
      adminCQ(nullptr),
      ioSQ(nullptr),
      ioCQ(nullptr),
      ssdID(id),
      output_dir(od) {
  pcieGen = (SimpleSSD::PCIExpress::PCIE_GEN)conf.readInt(
      SimpleSSD::CONFIG_NVME, SimpleSSD::HIL::NVMe::NVME_PCIE_GEN);
  pcieLane = (uint8_t)conf.readUint(SimpleSSD::CONFIG_NVME,
                                    SimpleSSD::HIL::NVMe::NVME_PCIE_LANE);
  
  simpleSolution = conf.readBoolean(SimpleSSD::CONFIG_ESPRESSO,
                                    SimpleSSD::ESPRESSO::ESPRESSO_SIMPLE_SOLUTION);

  pController = new SimpleSSD::HIL::NVMe::Controller(this, conf, pCPU, id, simpleSolution, output_dir);

  dmaReadEvent = engine.allocateEvent([this](uint64_t) { dmaReadDone(); });
  dmaWriteEvent = engine.allocateEvent([this](uint64_t) { dmaWriteDone(); });

  maxComputeLendOut = (uint8_t)conf.readUint(SimpleSSD::CONFIG_ESPRESSO,
                                    SimpleSSD::ESPRESSO::ESPRESSO_MAX_COMPUTE_LEND_OUT);
  maxComputeBorrowIn = (uint8_t)conf.readUint(SimpleSSD::CONFIG_ESPRESSO,
                                    SimpleSSD::ESPRESSO::ESPRESSO_MAX_COMPUTE_BORROW_IN);
  copyBackMethod = conf.readUint(SimpleSSD::CONFIG_ESPRESSO,
                                 SimpleSSD::ESPRESSO::ESPRESSO_COPYBACK_METHOD);
  copyBackInterval = conf.readUint(SimpleSSD::CONFIG_ESPRESSO,
                                   SimpleSSD::ESPRESSO::ESPRESSO_COPYBACK_INTERVAL) * 1000000ULL;  // convert to ticks
  copyBackCapThreshold = conf.readUint(SimpleSSD::CONFIG_ESPRESSO,
                                       SimpleSSD::ESPRESSO::ESPRESSO_COPYBACK_CAP_THRESHOLD) * 1024 * 1024ULL;  // convert to bytes                     

  creditorSQs.resize(maxComputeLendOut);
  creditorCQs.resize(maxComputeLendOut);
  creditorHandlers.resize(maxComputeLendOut);

  debtorHandlers.resize(1);

  globalEspressoDriverMap[ssdID] = this;

  // Set random engine
  // ESFUTURE: get in configuration  file
  randseed = 10270314;
  randmax = 10000;
  randengine.seed(randseed);
  randgen = std::uniform_int_distribution<uint64_t>(0, randmax);
}

Driver::~Driver() {
  delete pController;
  delete adminSQ;
  delete adminCQ;
  delete ioSQ;
  delete ioCQ;

  for (int i = 0; i < maxComputeLendOut; ++i) {
    delete creditorSQs[i];
    delete creditorCQs[i];
  }

  std::cout << "PCIe Busy: " << pcieBusy << std::endl;
  std::cout << "PCIe Load: " << pcieLoad << std::endl;
}

void Driver::init(std::function<void()> &func) {
  beginFunction = func;

  // NVMe Initialization process (Register)
  // See Section 7.6.1. Initialization of NVMe 1.3c
  union {
    uint64_t value;
    uint8_t buffer[8];
  } temp;
  uint64_t tick = 0;

  // Step 1. Read CAP
  pController->readRegister(SimpleSSD::HIL::NVMe::REG_CONTROLLER_CAPABILITY, 8,
                            temp.buffer, tick);

  // MPSMAX/MIN is setted to 4KB
  // DSTRD is setted to 0 (4bytes)
  // Check MQES
  maxQueueEntries = (temp.value & 0xFFFF) + 1;

  // Step 2. Wait for CSTS.RDY = 0
  // Step 3. Configure admin queue
  // Step 3-1. Set admin queue entry size
  uint16_t entries = QUEUE_ENTRY_ADMIN;

  if (entries > maxQueueEntries) {
    entries = maxQueueEntries;
  }

  temp.value = entries - 1;
  temp.value |= (entries - 1) << 16;

  pController->writeRegister(SimpleSSD::HIL::NVMe::REG_ADMIN_QUEUE_ATTRIBUTE, 4,
                             temp.buffer, tick);

  adminSQ = new Queue(entries, 64);
  adminCQ = new Queue(entries, 16);

  // Step 3-2. Write base addresses
  adminSQ->getBaseAddress(temp.value);
  pController->writeRegister(SimpleSSD::HIL::NVMe::REG_ADMIN_SQUEUE_BASE_ADDR,
                             8, temp.buffer, tick);
  adminCQ->getBaseAddress(temp.value);
  pController->writeRegister(SimpleSSD::HIL::NVMe::REG_ADMIN_CQUEUE_BASE_ADDR,
                             8, temp.buffer, tick);

  // Step 4. Configure controller
  // Step 5. Enable controller
  temp.value = 1;            // Round Robin, 4K page, NVM command set, Enable
  temp.value |= 0x00460000;  // 64B SQEntry, 16B CQEntry
  pController->writeRegister(SimpleSSD::HIL::NVMe::REG_CONTROLLER_CONFIG, 4,
                             temp.buffer, tick);

  // Step 6. Wait for CSTS.RDY = 1
  // Step 7. Send Identify
  // Step 7-1. Submit Identify Controller
  uint32_t cmd[16];
  PRP *prp = new PRP(4096);
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    _init0(status, context);
  };

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_IDENTIFY;  // CID, FUSE, OPC
  prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  cmd[10] = SimpleSSD::HIL::NVMe::CNS_IDENTIFY_CONTROLLER;          // CNS

  submitCommand(0, (uint8_t *)cmd, callback, prp);
}

void Driver::_init0(uint16_t, void *context) {
  PRP *prp = (PRP *)context;

  // Step 7-2. Send Identify Active Namespace List
  // Reuse PRP here
  uint32_t cmd[16];
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    _init1(status, context);
  };

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_IDENTIFY;  // CID, FUSE, OPC
  prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  cmd[10] = SimpleSSD::HIL::NVMe::CNS_ACTIVE_NAMESPACE_LIST;        // CNS

  submitCommand(0, (uint8_t *)cmd, callback, prp);
}

void Driver::_init1(uint16_t, void *context) {
  PRP *prp = (PRP *)context;

  // Step 7-3. Check active Namespace
  // We will perform I/O on first Namespace
  uint32_t count = 0;
  uint32_t nsid = 0;

  for (count = 0; count < 1024; count++) {
    prp->readData(count * 4, 4, (uint8_t *)&nsid);

    if (nsid == 0) {
      break;
    }

    if (count == 0) {
      namespaceID = nsid;
    }
  }

  if (count == 0) {
    SimpleSSD::panic("This NVMe SSD does not have any namespaces.");
  }
  else if (count > 1) {
    SimpleSSD::warn("This NVMe SSD has %u namespaces.", count);
    SimpleSSD::warn("All I/O will performed on namespace ID %u.", namespaceID);
  }

  // Step 7-4. Send Identify Namespace
  // Reuse PRP here
  uint32_t cmd[16];
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    _init2(status, context);
  };

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_IDENTIFY;  // CID, FUSE, OPC
  cmd[1] = namespaceID;                            // NSID
  prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  cmd[10] = SimpleSSD::HIL::NVMe::CNS_IDENTIFY_NAMESPACE;           // CNS

  submitCommand(0, (uint8_t *)cmd, callback, prp);
}

void Driver::_init2(uint16_t, void *context) {
  union {
    uint64_t value;
    uint8_t buffer[8];
  } temp;
  PRP *prp = (PRP *)context;
  uint8_t nFormat, currentFormat;
  uint32_t formatData;

  // Step 7-4. Check structures
  prp->readData(0, 8, temp.buffer);
  capacity = temp.value;

  prp->readData(25, 1, &nFormat);
  nFormat++;

  prp->readData(26, 1, &currentFormat);
  prp->readData(128 + currentFormat * 4ull, 4, (uint8_t *)&formatData);

  LBAsize = (uint32_t)powf(2.f, (float)((formatData >> 16) & 0xFF));
  capacity *= LBAsize;

  delete prp;

  SimpleSSD::info("SIL::NVMe::Driver: Total SSD capacity: %" PRIu64 " bytes",
                  capacity);
  SimpleSSD::info("SIL::NVMe::Driver: Logical Block Size: %" PRIu32 " bytes",
                  LBAsize);

  // Step 8. Determine I/O queue count
  // Step 8-1. Send Set Feature
  uint32_t cmd[16];
  ResponseHandler callback = [this](uint16_t status, uint32_t dw0,
                                    void *context) {
    _init3(status, dw0, context);
  };

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_SET_FEATURES;             // CID, FUSE, OPC
  cmd[10] = SimpleSSD::HIL::NVMe::FEATURE_NUMBER_OF_QUEUES;       // FID
  cmd[11] = (maxComputeLendOut) << 16 | maxComputeLendOut;

  submitCommand(0, (uint8_t *)cmd, callback, nullptr);
}

void Driver::_init3(uint16_t, uint32_t dw0, void *) {
  // Step 8-2. Check response
  if (dw0 != (uint32_t)((maxComputeLendOut) << 16 | maxComputeLendOut)) {
    SimpleSSD::panic("NVMe SSD responsed too many I/O queue");
  }

  // Step 9. Allocate I/O Completion Queue
  // Step 9-1. Send Create I/O Completion Queue
  uint32_t cmd[16];
  uint16_t entries = QUEUE_ENTRY_IO;

  createdCreditorResources = 0;
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    // Step 9-2. Check result
    if (status != 0) {
      SimpleSSD::panic("Failed to create I/O Completion Queue");
    }

    if (++createdCreditorResources == maxComputeLendOut + 1) {
      _init4(context);
    }
  };

  if (entries > maxQueueEntries) {
    entries = maxQueueEntries;
  }

  ioCQ = new Queue(entries, 16);

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_CREATE_IO_CQUEUE;  // CID, FUSE, OPC
  ioCQ->getBaseAddress(*(uint64_t *)(cmd + 6));            // DPTR.PRP1
  cmd[10] = ((uint32_t)(entries - 1) << 16) | 0x0001;      // QSIZE, QID
  cmd[11] = 0x00010003;                                    // IV, IEN, IS_CREDITORQUEUE (ESPRESSO), PC

  submitCommand(0, (uint8_t *)cmd, callback, nullptr);

  for(int i = 0; i < maxComputeLendOut; ++i) {
    creditorCQs[i] = new Queue(entries, 16);
    memset(cmd, 0, 64);
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_CREATE_IO_CQUEUE;             // CID, FUSE, OPC
    creditorCQs[i]->getBaseAddress(*(uint64_t *)(cmd + 6));             // DPTR.PRP1
    cmd[10] = ((uint32_t)(entries - 1) << 16) | (uint16_t)(i + 2);      // QSIZE, QID
    cmd[11] = (uint16_t) (i + 2) << 16 | 0x0007;                        // IV, IEN, IS_CREDITORQUEUE (ESPRESSO), PC
    submitCommand(0, (uint8_t *)cmd, callback, nullptr);
  }
}

void Driver::_init4(void *) {
  // Step 10. Allocate I/O Submission Queue
  // Step 10-1. Send Create I/O Submission Queue
  uint32_t cmd[16];
  uint16_t entries = QUEUE_ENTRY_IO;

  createdCreditorResources = 0;
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    // Step 10-2. Check result
    if (status != 0) {
      SimpleSSD::panic("Failed to create I/O Submission Queue");
    }

    if (++createdCreditorResources == maxComputeLendOut + 1) {
      _init5(context);
    }
  };

  if (entries > maxQueueEntries) {
    entries = maxQueueEntries;
  }

  ioSQ = new Queue(entries, 64);

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_CREATE_IO_SQUEUE;  // CID, FUSE, OPC
  ioSQ->getBaseAddress(*(uint64_t *)(cmd + 6));            // DPTR.PRP1
  cmd[10] = ((uint32_t)(entries - 1) << 16) | 0x0001;      // QSIZE, QID
  cmd[11] = 0x00010001;                                    // CQID, QPRIO, PC

  submitCommand(0, (uint8_t *)cmd, callback, nullptr);

  for(int i = 0; i < maxComputeLendOut; ++i) {
    creditorSQs[i] = new Queue(entries, 64);
    memset(cmd, 0, 64);
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_CREATE_IO_SQUEUE;             // CID, FUSE, OPC
    creditorSQs[i]->getBaseAddress(*(uint64_t *)(cmd + 6));             // DPTR.PRP1
    cmd[10] = ((uint32_t)(entries - 1) << 16) | (uint16_t)(i + 2);      // QSIZE, QID
    cmd[11] = (uint16_t) (i + 2) << 16 | 0x0001;                        // CQID, QPRIO, PC
    submitCommand(0, (uint8_t *)cmd, callback, nullptr);
  }
}

void Driver::_init5(void *) {
  SimpleSSD::info("SIL::NVMe::Driver: Initialization finished");

  // Now we initialized NVMe SSD
  beginFunction();
}

void Driver::submitCommand(uint16_t iv, uint8_t *cmd, ResponseHandler &func,
                           void *context, bool isCopyBack) {
  uint16_t cid = 0;
  uint16_t opcode = cmd[0];
  uint16_t tail = 0;
  uint64_t tick = engine.getCurrentTick();
  Queue *queue = nullptr;

  if(isCopyBack) assert(simpleSolution);

  // Push to queue
  if (iv == 0) {
    assert(!isCopyBack);

    increaseCommandID(adminCommandID);
    cid = adminCommandID;
    queue = adminSQ;

    // Push to pending cmd list
    pendingCommandList.push_back(CommandEntry(iv, opcode, cid, context, func));
  }
  else if (iv == 1 && ioSQ) {
    increaseCommandID(ioCommandID);
    cid = ioCommandID; 

    DebtorHandler &dhandler = debtorHandlers[0];
    if (!dhandler.relations.empty() && !isCopyBack) {
      float sumCreditorBusy = 0;
      for (const auto &rel : dhandler.relations) {
        sumCreditorBusy += globalEspressoDriverMap[rel.creditorSSDID]->frontBusyValue;
      }

      float ratio = (float) frontBusyValue /
                    (frontBusyValue + sumCreditorBusy);
      float rand = (float) randgen(randengine) / randmax;
      bool redirect = false;
      BorrowRelation *selectedRel = nullptr;

      if (simpleSolution) {
        pHost->pCPU->applyLatency(SimpleSSD::CPU::ESPRESSO, SimpleSSD::CPU::SIMPLE_SOLUTION_HOST_REDIRECT, tick);
        uint64_t offset_l = ((uint32_t*)cmd)[11];
        uint64_t offset_r = ((uint32_t*)cmd)[10];
        uint64_t offset = offset_l << 32 | offset_r;
        uint64_t length = ((uint32_t*)cmd)[12] + 1;

        assert(length >= 1);

        if (opcode == 1) {  // write
          if (rand <= ratio) {
            redirect = true;
            selectedRel = pickBorrowRelation(dhandler);
            if (selectedRel) {
              for (uint64_t i = offset; i < offset + length; ++i) {
                dhandler.debtStorage.insert(i);
              }
            }
          }
        }
        else if (opcode == 2) { // read
          redirect = true;
          selectedRel = pickBorrowRelation(dhandler);
          if (selectedRel) {
            for (uint64_t i = offset; i < offset + length; ++i) {
              if (dhandler.debtStorage.find(i) == dhandler.debtStorage.end()) {
                redirect = false;
                selectedRel = nullptr;
                break;
              }
            }
          }
        }
      }
      else {
        pHost->pCPU->applyLatency(SimpleSSD::CPU::ESPRESSO, SimpleSSD::CPU::ESPRESSO_HOST_REDIRECT, tick);
        if (rand <= ratio) {
          redirect = true;
          selectedRel = pickBorrowRelation(dhandler);
        }
      }

      if (redirect && selectedRel) {
        pendingCommandList.push_back(CommandEntry(iv, opcode, cid, context, func, true, simpleSolution, isCopyBack, ssdID, iv, selectedRel->creditorSSDID, selectedRel->creditorSQID));
        return globalEspressoDriverMap[selectedRel->creditorSSDID]->submitCommand(selectedRel->creditorIV, cmd, func, context);
      }
      else {
        queue = ioSQ;
        pendingCommandList.push_back(CommandEntry(iv, opcode, cid, context, func));
      }
    }
    else {
      queue = ioSQ;
      // Push to pending cmd list
      pendingCommandList.push_back(CommandEntry(iv, opcode, cid, context, func, false, false, isCopyBack, UINT8_MAX, UINT16_MAX, UINT8_MAX, UINT16_MAX));
    }
  }
  else if (iv < maxComputeLendOut + 2 && creditorSQs[iv - 2]){
    assert(!isCopyBack);

    CreditorHandler& chandler = creditorHandlers[iv - 2];

    if (simpleSolution && opcode == 1) {
      uint64_t offset_l = ((uint32_t*)cmd)[11];
      uint64_t offset_r = ((uint32_t*)cmd)[10];
      uint64_t offset = offset_l << 32 | offset_r;
      uint64_t length = ((uint32_t*)cmd)[12] + 1;

      for (uint64_t i = offset; i < offset + length; ++i) {
        chandler.creditStorage.insert(i);
      }
    }
    
    cid = globalEspressoDriverMap[chandler.debtorSSDID]->ioCommandID;

    queue = creditorSQs[iv - 2];
    // Push to pending cmd list
    pendingCommandList.push_back(CommandEntry(iv, opcode, cid, context, func, true, simpleSolution, isCopyBack, chandler.debtorSSDID, chandler.debtorSQID, ssdID, iv));
  }
  else {
    SimpleSSD::panic("I/O Submission Queue is not initialized");
  }

  memcpy(cmd + 2, &cid, 2);
  queue->setData(cmd, 64);
  tail = queue->getTail();

  // Ring doorbell
  pController->ringSQTailDoorbell(iv, tail, tick);
  queue->incrHead();

  // check if we need to copy back
  if (simpleSolution && !isCopyBack) {
    if (iv == 1) {
      DebtorHandler &dhandler = debtorHandlers[0];
      if (shouldCopyBack(tick, dhandler.debtStorage.size())) {
        doCopyBackWriteDebtor(dhandler);
      }
    }
    else if (iv > 1 && iv < maxComputeLendOut + 2) {
      CreditorHandler& chandler = creditorHandlers[iv - 2];
      if (shouldCopyBack(tick, chandler.creditStorage.size())) {
        doCopyBackReadCreditor(chandler);
      }
    }
  }
}

BorrowRelation *Driver::pickBorrowRelation(DebtorHandler &dhandler) {
  if (dhandler.relations.empty()) {
    return nullptr;
  }

  uint64_t totalWeight = 0;
  for (const auto &rel : dhandler.relations) {
    uint8_t busy = globalEspressoDriverMap[rel.creditorSSDID]->frontBusyValue;
    totalWeight += (10 - busy);
  }

  if (totalWeight == 0) {
    return &dhandler.relations[0];
  }

  uint64_t pick = randgen(randengine) % totalWeight;
  uint64_t cumulative = 0;

  for (auto &rel : dhandler.relations) {
    uint8_t busy = globalEspressoDriverMap[rel.creditorSSDID]->frontBusyValue;
    cumulative += (10 - busy);
    if (pick < cumulative) {
      return &rel;
    }
  }

  return &dhandler.relations.back();
}

void Driver::initResourceBorrow(uint16_t debtorSQID) {
  SimpleSSD::ESPRESSO::BorrowSettingInfo borrowInfo = pController->getBorrowSettingInfo(debtorSQID);

  DebtorHandler &handler = debtorHandlers[debtorSQID-1];

  BorrowRelation rel;
  rel.creditorIV = borrowInfo.creditorSQID;
  rel.creditorSSDID = borrowInfo.creditorSSDID;
  rel.creditorSQID = borrowInfo.creditorSQID;
  rel.tenancy = borrowInfo.tenancy;
  handler.relations.push_back(rel);

  globalEspressoDriverMap[rel.creditorSSDID]->initResourceLend(borrowInfo.creditorSQID, ssdID, debtorSQID, debtorSQID, borrowInfo.creditorFrontBusyValue);

  pController->updateBorrowingState();
  pController->setPending(false);

  frontBusyValue = borrowInfo.debtorFrontBusyValue;
}

void Driver::initResourceLend(uint16_t creditorSQID, uint8_t debtorSSDID, uint16_t debtorIV, uint16_t debtorSQID, uint8_t creditorFrontBusyBalue) {
  CreditorHandler &handler = creditorHandlers[creditorSQID - 2];
  handler.debtorSSDID = debtorSSDID;
  handler.debtorIV = debtorIV;
  handler.debtorSQID = debtorSQID;
  
  pController->updateLendingState();
  pController->setPending(false);

  frontBusyValue = creditorFrontBusyBalue;
}

void Driver::cancelResourceBorrow(uint16_t debtorSQID, uint8_t creditorSSDID) {
  DebtorHandler &handler = debtorHandlers[debtorSQID - 1];

  for (auto iter = handler.relations.begin(); iter != handler.relations.end(); ++iter) {
    if (iter->creditorSSDID == creditorSSDID) {
      globalEspressoDriverMap[creditorSSDID]->cancelResourceLend(iter->creditorSQID);
      handler.relations.erase(iter);
      break;
    }
  }

  pController->updateBorrowingState();
  pController->setPending(false);
}

void Driver::cancelResourceLend(uint16_t creditorSQID) {
  CreditorHandler &handler = creditorHandlers[creditorSQID - 2];

  handler.debtorSSDID = UINT8_MAX;
  handler.debtorIV = UINT16_MAX;
  handler.debtorSQID = UINT16_MAX;

  pController->updateLendingState();
  pController->setPending(false);

  // if (simpleSolution && copyBackMethod != 0) doCopyBackReadCreditor(handler);
}

void Driver::debtorCancelBorrowOne(uint8_t creditorSSDID) {
  DebtorHandler &handler = debtorHandlers[0];

  for (auto iter = handler.relations.begin(); iter != handler.relations.end(); ++iter) {
    if (iter->creditorSSDID == creditorSSDID) {
      globalEspressoDriverMap[creditorSSDID]->cancelResourceLend(iter->creditorSQID);
      handler.relations.erase(iter);
      break;
    }
  }

  pController->updateBorrowingState();
  pController->setPending(false);
}

void Driver::creditorCancelLend(uint16_t creditorSQID, uint8_t debtorSSDID) {
  CreditorHandler &handler = creditorHandlers[creditorSQID - 2];

  globalEspressoDriverMap[debtorSSDID]->cancelResourceBorrow(handler.debtorSQID, ssdID);
  
  handler.debtorSSDID = UINT8_MAX;
  handler.debtorIV = UINT16_MAX;
  handler.debtorSQID = UINT16_MAX;

  pController->updateLendingState();
  pController->setPending(false);

  // if (simpleSolution && copyBackMethod != 0) doCopyBackReadCreditor(handler);
}

bool Driver::shouldCopyBack(uint64_t tick, uint64_t cumulatedSize) {
  assert(simpleSolution);

  if(copyBackMethod == 0) { // ideal
    return false;
  }
  else if(copyBackMethod == 1) { // interval
    if (lastCopyBackTick == 0) {
      lastCopyBackTick = tick;
      return false;
    }
    else {
      // ESTODO: uses absolute time now; reset timer when borrowing starts?
      uint64_t interval = tick - lastCopyBackTick;

      if (interval > copyBackInterval) {
        lastCopyBackTick = tick;
        return true;
      }
      else {
        return false;
      }
    }
  }
  else if(copyBackMethod == 2) { // cumulative
    if (cumulatedSize * LBAsize > copyBackCapThreshold) {
      return true;
    }
    else {
      return false;
    }
  }
  else if(copyBackMethod == 3) { // worst
    return true;
  }

  SimpleSSD::panic("Invalid copy back method");

  return false;
}

void Driver::doCopyBackWriteDebtor(DebtorHandler &dhandler) {
  assert(simpleSolution);
  
  auto it = dhandler.debtStorage.begin();
  uint64_t cur_offset = *it;
  uint64_t cur_length = 0;
  uint64_t tick = engine.getCurrentTick();
  vector<uint64_t> io_offsets;
  vector<uint64_t> io_lengths;
  
  for (; it != dhandler.debtStorage.end();) { // Coalesce contiguous I/O (max LBA size * 256)
    while(it != dhandler.debtStorage.end() && *it == cur_offset + cur_length && cur_length < 256) {
      cur_length++;
      it++;
    }
    if (cur_length > 0) {
      pHost->pCPU->applyLatency(SimpleSSD::CPU::ESPRESSO, SimpleSSD::CPU::COPY_BACK_WRITE_DEBTOR, tick);
      io_offsets.push_back(cur_offset * LBAsize);
      io_lengths.push_back(cur_length * LBAsize);
      cur_offset = *it;
      cur_length = 0;
    }
  }

  dhandler.debtStorage.clear();

  for (size_t i = 0; i < io_offsets.size(); ++i) {
    submitCopyBackIO(BIL::BIO_WRITE, io_offsets[i], io_lengths[i]);
  }
}

void Driver::doCopyBackReadCreditor(CreditorHandler &chandler) {
  assert(simpleSolution);

  auto it = chandler.creditStorage.begin();
  uint64_t cur_offset = *it;
  uint64_t cur_length = 0;
  uint64_t tick = engine.getCurrentTick();
  vector<uint64_t> io_offsets;
  vector<uint64_t> io_lengths;
  
  for (; it != chandler.creditStorage.end();) { // Coalesce contiguous I/O (max LBA size * 256)
    while(it != chandler.creditStorage.end() && *it == cur_offset + cur_length && cur_length < 256) {
      cur_length++;
      it++;
    }
    if (cur_length > 0) {
      pHost->pCPU->applyLatency(SimpleSSD::CPU::ESPRESSO, SimpleSSD::CPU::COPY_BACK_READ_CREDITOR, tick);
      io_offsets.push_back(cur_offset * LBAsize);
      io_lengths.push_back(cur_length * LBAsize);
      cur_offset = *it;
      cur_length = 0;
    }
  }

  chandler.creditStorage.clear();

  for (size_t i = 0; i < io_offsets.size(); ++i) {
    submitCopyBackIO(BIL::BIO_READ, io_offsets[i], io_lengths[i]);
  }
}

void Driver::submitCopyBackIO(BIL::BIO_TYPE type, uint64_t offset, uint64_t length) {
  assert(simpleSolution);

  uint32_t cmd[16];
  PRP *prp = nullptr;
  static ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    IOWrapper *wrapper = (IOWrapper *)context;
    PRP *prp = wrapper->prp;
  
    if (status != 0) {
      SimpleSSD::warn("CopyBack I/O error: %04X", status);
    }
  
    delete prp;
    delete wrapper;
  };

  memset(cmd, 0, 64);

  uint64_t slba = offset / LBAsize;
  uint32_t nlb = (uint32_t)DIVCEIL(length, LBAsize);

  cmd[1] = namespaceID;  // NSID

  if (type == BIL::BIO_READ) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_READ;  // CID, FUSE, OPC
    cmd[10] = (uint32_t)slba;
    cmd[11] = slba >> 32;
    cmd[12] = nlb - 1;  // LR, FUA, PRINFO, NLB

    prp = new PRP(length);
    prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  }
  else if (type == BIL::BIO_WRITE) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_WRITE;  // CID, FUSE, OPC
    cmd[10] = (uint32_t)slba;
    cmd[11] = slba >> 32;
    cmd[12] = nlb - 1;  // LR, FUA, PRINFO, DTYPE, NLB

    prp = new PRP(length);
    prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  }
  else SimpleSSD::panic("Invalid copy back I/O type");
  
  std::function<void(uint64_t, bool)> fakeCallback = [](uint64_t, bool) {};
  submitCommand(1, (uint8_t *)cmd, callback, new IOWrapper(UINT64_MAX, prp, fakeCallback), true);
}

void Driver::increaseCommandID(uint16_t &id) {
  static const uint16_t maxID = 32767;

  id++;

  if (id > maxID) {
    id = 1;
  }
}

void Driver::getInfo(uint64_t &bytesize, uint32_t &minbs) {
  bytesize = capacity;
  minbs = LBAsize;
}

void Driver::submitIO(BIL::BIO &bio) {
  uint32_t cmd[16];
  PRP *prp = nullptr;
  static ResponseHandler callback = [this](uint16_t status, uint32_t,
                                           void *context) {
    _io(status, context);
  };

  memset(cmd, 0, 64);

  uint64_t slba = bio.offset / LBAsize;
  uint32_t nlb = (uint32_t)DIVCEIL(bio.length, LBAsize);

  cmd[1] = namespaceID;  // NSID

  if (bio.type == BIL::BIO_READ) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_READ;  // CID, FUSE, OPC
    cmd[10] = (uint32_t)slba;
    cmd[11] = slba >> 32;
    cmd[12] = nlb - 1;  // LR, FUA, PRINFO, NLB

    prp = new PRP(bio.length);
    prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  }
  else if (bio.type == BIL::BIO_WRITE) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_WRITE;  // CID, FUSE, OPC
    cmd[10] = (uint32_t)slba;
    cmd[11] = slba >> 32;
    cmd[12] = nlb - 1;  // LR, FUA, PRINFO, DTYPE, NLB

    prp = new PRP(bio.length);
    prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  }
  else if (bio.type == BIL::BIO_FLUSH) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_FLUSH;  // CID, FUSE, OPC
  }
  else if (bio.type == BIL::BIO_TRIM) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_DATASET_MANAGEMEMT;  // CID, FUSE, OPC
    cmd[10] = 0;                                               // NR
    cmd[11] = 0x04;                                            // AD

    prp = new PRP(16);
    prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR

    // Fill range definition
    uint8_t data[16];

    memset(data, 0, 16);
    memcpy(data + 4, &nlb, 4);
    memcpy(data + 8, &slba, 8);

    prp->writeData(0, 16, data);
  }

  submitCommand(1, (uint8_t *)cmd, callback,
                new IOWrapper(bio.id, prp, bio.callback2));
}

void Driver::_io(uint16_t status, void *context) {
  IOWrapper *wrapper = (IOWrapper *)context;
  PRP *prp = wrapper->prp;

  if (status != 0) {
    SimpleSSD::warn("I/O error: %04X", status);
  }

  wrapper->bioCallback(wrapper->id, wrapper->readValidPage);

  delete prp;
  delete wrapper;
}

void Driver::initStats(std::vector<SimpleSSD::Stats> &list) {
  pController->getStatList(list, "");
  pCPU->getStatList(list, "cpu");
}

void Driver::getStats(std::vector<double> &values) {
  pController->getStatValues(values);
  pCPU->getStatValues(values);
}

void Driver::dmaRead(uint64_t addr, uint64_t size, uint8_t *buffer,
                     SimpleSSD::DMAFunction &func, void *context) {
  if (size == 0) {
    SimpleSSD::warn("nvme_interface: zero-size DMA read request. Ignore.");

    return;
  }

  dmaReadQueue.push(DMAEntry(func));

  auto &iter = dmaReadQueue.back();
  iter.addr = addr;
  iter.size = size;
  iter.buffer = buffer;
  iter.context = context;

  if (!dmaReadPending) {
    submitDMARead();
  }
}

void Driver::dmaReadDone() {
  auto &iter = dmaReadQueue.front();
  uint64_t tick = engine.getCurrentTick();

  if (tick < iter.finishedAt) {
    engine.scheduleEvent(dmaReadEvent, iter.finishedAt);

    return;
  }

  iter.func(tick, iter.context);
  dmaReadQueue.pop();
  dmaReadPending = false;

  if (dmaReadQueue.size() > 0) {
    submitDMARead();
  }
}

void Driver::submitDMARead() {
  auto &iter = dmaReadQueue.front();

  dmaReadPending = true;

  iter.beginAt = engine.getCurrentTick();
  iter.finishedAt = iter.beginAt + SimpleSSD::PCIExpress::calculateDelay(
                                       pcieGen, pcieLane, iter.size);
  
  pcieBusy += iter.finishedAt - iter.beginAt;
  pcieLoad += SimpleSSD::PCIExpress::calculatePhyLoad(iter.size);

  if (iter.buffer) {
    memcpy(iter.buffer, (uint8_t *)iter.addr, iter.size);
  }

  engine.scheduleEvent(dmaReadEvent, iter.finishedAt);
}

void Driver::dmaWrite(uint64_t addr, uint64_t size, uint8_t *buffer,
                      SimpleSSD::DMAFunction &func, void *context) {
  if (size == 0) {
    SimpleSSD::warn("nvme_interface: zero-size DMA write request. Ignore.");

    return;
  }

  dmaWriteQueue.push(DMAEntry(func));

  auto &iter = dmaWriteQueue.back();
  iter.addr = addr;
  iter.size = size;
  iter.buffer = buffer;
  iter.context = context;

  if (!dmaWritePending) {
    submitDMAWrite();
  }
}

void Driver::dmaWriteDone() {
  auto &iter = dmaWriteQueue.front();
  uint64_t tick = engine.getCurrentTick();

  if (tick < iter.finishedAt) {
    engine.scheduleEvent(dmaWriteEvent, iter.finishedAt);

    return;
  }

  iter.func(tick, iter.context);
  dmaWriteQueue.pop();
  dmaWritePending = false;

  if (dmaWriteQueue.size() > 0) {
    submitDMAWrite();
  }
}

void Driver::submitDMAWrite() {
  auto &iter = dmaWriteQueue.front();

  dmaWritePending = true;

  iter.beginAt = engine.getCurrentTick();
  iter.finishedAt = iter.beginAt + SimpleSSD::PCIExpress::calculateDelay(
                                       pcieGen, pcieLane, iter.size);
  
  pcieBusy += iter.finishedAt - iter.beginAt;
  pcieLoad += SimpleSSD::PCIExpress::calculatePhyLoad(iter.size);

  if (iter.buffer) {
    memcpy((uint8_t *)iter.addr, iter.buffer, iter.size);
  }

  engine.scheduleEvent(dmaWriteEvent, iter.finishedAt);
}

void Driver::handleCQEntry(uint16_t iv, uint32_t cqdata[4], bool& found) 
{
  // Search pending command list
  for (auto iter = pendingCommandList.begin(); iter != pendingCommandList.end(); iter++) {
    if (iter->iv == iv && iter->cid == (cqdata[3] & 0xFFFF)) {
      
      if (iv == 0) {
        assert(iter->isRemoteReq == false);
        assert(iter->isCopyBackIO == false);

        iter->callback((uint16_t)(cqdata[3] >> 17), cqdata[0], iter->context);

        pendingCommandList.erase(iter);
        found = true;
      }
      else if (iv == 1) {
        IOWrapper *temporaryContext = (IOWrapper *)iter->context;
        if (iter->opcode == SimpleSSD::HIL::NVMe::OPCODE_READ && cqdata[0] == 1){
          temporaryContext->readValidPage = false;
        }

        
        if (!iter->isRemoteReq) {
          if (cqdata[1] & 0x80000000U) {
            initResourceBorrow(cqdata[2] >> 16);
          }
          else if ((cqdata[1] & 0xC0000000U) == 0 && cqdata[1] != 0) {
            frontBusyValue = cqdata[1] & 0xFF;
          }

          assert((cqdata[1] & 0x40000000U) == 0);
        }

        iter->callback((uint16_t)(cqdata[3] >> 17), cqdata[0], iter->context);
        pendingCommandList.erase(iter);
        found = true;
      }
      else if (iv < 2 + maxComputeLendOut) {
        assert(iter->isRemoteReq == true);
        assert((cqdata[1] & 0x80000000U) == 0);

        if (cqdata[1] & 0x40000000U) {
          uint8_t debtorSSDID = (cqdata[1] >> 8) & 0xFF;
          creditorCancelLend(cqdata[2] >> 16, debtorSSDID);
        }
        else if ((cqdata[1] & 0xC0000000U) == 0 && cqdata[1] != 0) {
          frontBusyValue = cqdata[1] & 0xFF;
        }

        bool debtorfound = false;

        globalEspressoDriverMap[iter->debtorSSDID]->handleCQEntry(iter->debtorSQID, cqdata, debtorfound);

        assert(debtorfound);

        found = true;
        pendingCommandList.erase(iter);
      }
      else {
        assert(0);
      }

      break;
    }
  }
}

void Driver::updateInterrupt(uint16_t iv, bool post) {
  uint32_t cqdata[4];

  if (post) {
    uint64_t tick = engine.getCurrentTick();
    uint16_t count = 0;
    Queue *queue = nullptr;

    if (iv == 0) {
      queue = adminCQ;
    }
    else if (iv == 1 && ioCQ) {
      queue = ioCQ;
    }
    else if (iv < maxComputeLendOut + 2 && creditorCQs[iv - 2]) {
      queue = creditorCQs[iv - 2];
    }
    else {
      SimpleSSD::panic("I/O Completion Queue is not initialized");
    }

    // Peek queue for count how many requests are finished
    while (true) {
      queue->peekData((uint8_t *)cqdata, 16);

      // Check phase tag
      if (((cqdata[3] >> 16) & 0x01) == queue->phase) {
        bool found = false;

        queue->incrTail();
        count++;

        handleCQEntry(iv, cqdata, found);

        if (found) {
          queue->incrHead();

          if (queue->getHead() == 0) {
            // Inverted
            queue->phase = !queue->phase;
          }
        }
        else {
          SimpleSSD::panic("Invalid interrupt");
        }
      }
      else {
        if (count > 0) {
          pController->ringCQHeadDoorbell(iv, queue->getHead(), tick);
        }

        break;
      }
    }
  }
}

void Driver::getVendorID(uint16_t &vid, uint16_t &ssvid) {
  // Copied from SimpleSSD-FullSystem
  vid = 0x144D;
  ssvid = 0x8086;
}

}  // namespace NVMe

}  // namespace SIL
