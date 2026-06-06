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

#ifndef __FTL_FTL__
#define __FTL_FTL__

#include <list>
#include <utility>

#include "dram/abstract_dram.hh"
#include "pal/pal.hh"
#include "util/simplessd.hh"
#include "cpu/cpu.hh"
#include "espresso/espresso.hh"

namespace SimpleSSD {

namespace FTL {

class AbstractFTL;

typedef struct {
  uint64_t totalPhysicalBlocks;  //!< (PAL::Parameter::superBlock)
  uint64_t totalLogicalBlocks;
  uint64_t pagesInBlock;  //!< (PAL::Parameter::page)
  uint32_t pageSize;      //!< Mapping unit (PAL::Parameter::superPageSize)
  uint32_t ioUnitInPage;  //!< # smallest I/O unit in one page
  uint32_t pageCountToMaxPerf;  //!< # pages to fully utilize internal parallism
  uint32_t subpageInUnit;  //!< smallest mapping unit
} Parameter;

class FTL : public StatObject {
 private:
  Parameter param;
  PAL::PAL *pPAL;

  ConfigReader &conf;
  AbstractFTL *pFTL;
  DRAM::AbstractDRAM *pDRAM;
  CPU::CPU *pCPU = nullptr;
  ESPRESSO::ESPRESSO *pESPRESSO = nullptr;

  typedef std::vector<PAL::Request> _PALReqList;
  /// AYDNOTICE: temp queue to store un-sent reqs.
  std::list<Request> receiveQueue;
  std::list<std::pair<Request, _PALReqList>> submitQueue;
  std::list<Request> commitQueue;

  std::function<void()> commitCallback;

 public:
  FTL(ConfigReader &, DRAM::AbstractDRAM *, CPU::CPU *, ESPRESSO::ESPRESSO *);
  ~FTL();
 
  void setCommitCallback(std::function<void()> f) { commitCallback = f; }

  void updateSubmit();
  void updateCommit();
  void Commit();
  bool commitToUpper(Request &);

  void read(Request &, uint64_t &);
  void write(Request &, uint64_t &);
  void trim(Request &, uint64_t &);

  void format(LPNRange &, uint64_t &);

  Parameter *getInfo();
  uint64_t getUsedPageCount(uint64_t, uint64_t);

  void submitRemotePALReq(PAL::Request &, uint64_t &);
  void triggerRemoteUpdateSubmit();
  void commitRemotePALReq(PAL::Request &);
  void commitRemoteFTLReq(PAL::Request);
  bool conductRemoteFTLMapFind(uint64_t, uint64_t &, void *, bool);
  bool conductRemoteFTLBlocksFind(uint32_t, void *);
  void conductRemoteFTLMapModify(uint64_t, uint64_t &);
  void conductRemoteFTLMapEmplaceEmpty(uint64_t);
  bool conductRemoteFTLGetBlockToWrite(uint32_t, void *);

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif
