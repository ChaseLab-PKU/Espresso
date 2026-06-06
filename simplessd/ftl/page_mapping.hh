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

#ifndef __FTL_PAGE_MAPPING__
#define __FTL_PAGE_MAPPING__

#include <cinttypes>
#include <unordered_map>
#include <vector>

#include "ftl/abstract_ftl.hh"
#include "ftl/common/block.hh"
#include "ftl/ftl.hh"
#include "pal/pal.hh"
#include "cpu/cpu.hh"
#include "lib/SHARDS-C/SHARDS.hh"
#include <glib.h>

namespace SimpleSSD {

namespace FTL {

typedef struct _MappingInfo{
  uint32_t blockIndex;
  uint32_t idx;
  uint32_t pageIndex;
  uint32_t subIdx;
  bool operator==(const _MappingInfo &);
  bool operator<(const _MappingInfo &);
  bool operator>(const _MappingInfo &);

  _MappingInfo(): blockIndex(0), idx(0), pageIndex(0), subIdx(0){}
  _MappingInfo(PAL::Request &r):
    blockIndex(r.blockIndex),
    pageIndex(r.pageIndex),
    subIdx(0){
      idx = 0;
      for(uint32_t i = 0; i < r.ioFlag.size(); i++){
        if(r.ioFlag.test(i)){
          idx = i;
          break;
        }
      }
    }
} MappingInfo;

struct TranslationPage {
  uint64_t no;
  uint32_t numDirty;     // num of dirty entries in this translation page
  std::vector<bool> dirtyTable;
  uint8_t ssdID;        // in which SSD's DRAM this page is stored

  TranslationPage(uint64_t, uint32_t, uint8_t);
};

// LRU cache
class LRU {
private:
  uint8_t ssdID;
  
  std::list<TranslationPage> cache;
  std::unordered_map<uint64_t, std::list<TranslationPage>::iterator> hashMap;

  double ratio;
  uint64_t readLat;       // latency of flash read
  uint64_t writeLat;      // latency of flash write
  uint32_t numEntryinDir; // 4 K / 4 B = 1024
  
  uint64_t lentCapacity = 0;
  uint64_t borrowedCapacity = 0;

  SHARDS::SHARDS *shards = nullptr;

  double firstLendMissRatio;  // start threshold (borrow/lend)
  double missRatioStopBorrowThreshold;
  double missRatioStopLendThreshold;

  std::ofstream outFile;

  uint64_t selfFreePage;
  std::unordered_map<uint8_t, uint64_t> remoteFreePage;

  uint64_t hitCount = 0;
  uint64_t missCount = 0;
  uint64_t dirtyWriteBackCount = 0;
  uint64_t feedMRCCount = 0;

  bool hit(uint64_t);
  void update(uint64_t);
  bool insert(uint64_t);
  uint8_t getFreePage();
  void outputMRC();

public:
  uint64_t origCapacity;
  uint64_t capacity;

  void init(uint8_t, uint64_t, double, uint32_t, uint64_t, uint64_t, double,
            double, double, std::string);
  ~LRU();

  void read(uint64_t, uint64_t &, bool);
  void write(uint64_t, uint64_t &);

  void shrinkCapacity(uint64_t, uint8_t);
  void balloonCapacity(uint64_t, uint8_t);
  void getMRC(std::vector<uint64_t>&, std::unordered_map<uint64_t, double>&) ;
  int  debtorRebalanceGhostCache(std::vector<uint64_t>&, std::unordered_map<uint64_t, double>&, uint64_t);
  uint64_t debtorGetNumBorrowedPage(uint8_t);
  uint64_t getTotalBorrowedPages() const;

  uint64_t getMinCapacityForMissRatio(double threshold) const;
  uint64_t computeLendableGhostCacheCapcity() const;
  bool hasMRCData() const;
  bool needsStartDRAMBorrow() const;
  bool canStartDRAMLend() const;
  bool shouldStopDRAMBorrow() const;
  bool shouldStopDRAMLend() const;
};

class PageMapping : public AbstractFTL {
 private:
  ConfigReader &conf;

  std::unordered_map<uint64_t, MappingInfo> table;
  
  
  LRU ghost_cache;

  std::unordered_map<uint32_t, Block> blocks;
  std::list<Block> freeBlocks;
  uint32_t nFreeBlocks;  // For some libraries which std::list::size() is O(n)
  std::vector<uint32_t> lastFreeBlock;
  uint32_t lastFreeBlockFreePage;
  uint32_t lastFreeBlockIndex;

  bool bReclaimMore;
  bool bRandomTweak;
  uint32_t bitsetSize;
  uint32_t subpageInUnit;

  struct {
    uint64_t gcCount;
    uint64_t reclaimedBlocks;
    uint64_t validSuperPageCopies;
    uint64_t validPageCopies;
  } stat;

  float freeBlockRatio();
  uint32_t convertBlockIdx(uint32_t);
  uint32_t getFreeBlock(uint32_t);
  uint32_t getLastFreeBlock(uint32_t);
  void calculateVictimWeight(std::vector<std::pair<uint32_t, float>> &,
                             const EVICT_POLICY, uint64_t);
  void selectVictimBlock(std::vector<uint32_t> &, uint64_t &);
  void doGarbageCollection(std::vector<uint32_t> &, uint64_t &);

  float calculateWearLeveling();
  void calculateTotalPages(uint64_t &, uint64_t &);
  uint64_t getTableIndex(ReqInfo &);
  bool FTLMapFind(uint64_t, uint64_t &, void *, bool);
  bool FTLBlocksFind(uint32_t, void *);
  void FTLMapModify(uint64_t, uint64_t &);
  void FTLMapEmplaceEmpty(uint64_t);
  bool FTLGetBlockToWrite(uint32_t, void *);
  
  void readInternal(std::vector<PAL::Request> *, Request &, uint64_t &);
  void writeInternal(std::vector<PAL::Request> *, Request &, uint64_t &, bool = true);
  void trimInternal(std::vector<PAL::Request> *, Request &, uint64_t &);
  void eraseInternal(PAL::Request &, uint64_t &, bool);

  typedef struct {
    uint64_t reqID;
    uint64_t reqSubID;
    uint64_t ftlReqID;
    uint8_t creditorSSDID;
  } CoalescedReq;
  uint64_t mappingTo64(MappingInfo &);
  std::unordered_map<uint64_t, std::vector<CoalescedReq>> PALReadCoalesceMap;
  /// @brief Check if a PAL Read request can be coalesced to a
  /// previous one. If so, coalesced it, otherwise record it to
  /// let succeeding Reads can be coalesced.
  /// @param read PAL Read Request.
  /// @return bool. `true` if can be coalesced, the request will
  /// be coalesced; `false` if cannot be coalesced, the request 
  /// will be inserted into the coalescing recording map.
  bool coalesce_CheckOrInsert(PAL::Request &);

  /// @brief Once a PAL Read request is commited, this function
  /// check if there is any PAL Read being coalesced to this. 
  /// If there is, remove it from the outstandingQueue
  /// Update context if needed.
  /// @param commit PAL Read Request that being commited.
  void coalesce_PALCommit(PAL::Request &);

  void multiplane_Schedule();

 public:
  PageMapping(ConfigReader &, Parameter &, PAL::PAL *, ESPRESSO::ESPRESSO *, DRAM::AbstractDRAM *, DRAM::RemoteDRAM *, CPU::CPU *);
  ~PageMapping();

  void updateSubmit() override;
  void Commit() override;

  bool initialize() override;

  void read(std::vector<PAL::Request> *, Request &, uint64_t &) override;
  void write(std::vector<PAL::Request> *, Request &, uint64_t &) override;
  void trim(std::vector<PAL::Request> *, Request &, uint64_t &) override;

  void format(LPNRange &, uint64_t &) override;

  void submitRemotePALReq(PAL::Request &, uint64_t &) override;
  void triggerRemoteUpdateSubmit() override;
  void commitRemotePALReq(PAL::Request &) override;
  void commitRemoteFTLReq(PAL::Request) override;
  bool conductRemoteFTLMapFind(uint64_t, uint64_t &, void *, bool) override;
  bool conductRemoteFTLBlocksFind(uint32_t, void *) override;
  void conductRemoteFTLMapModify(uint64_t, uint64_t &) override;
  void conductRemoteFTLMapEmplaceEmpty(uint64_t) override;
  bool conductRemoteFTLGetBlockToWrite(uint32_t, void *) override;
  

  Status *getStatus(uint64_t, uint64_t) override;

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif
