#pragma once

#ifndef __ESPRESSO_CONFIG__
#define __ESPRESSO_CONFIG__

#include "sim/base_config.hh"

namespace SimpleSSD {

namespace ESPRESSO {

typedef enum {
  ESPRESSO_ENABLE,
  ESPRESSO_CHECK_BUSY_INTERVAL,
  ESPRESSO_DRAM_RATIO,
  ESPRESSO_GHOST_CACHE_MISS_RATIO_THRESHOLD,
  ESPRESSO_GHOST_CACHE_MISS_RATIO_STOP_BORROW,
  ESPRESSO_GHOST_CACHE_MISS_RATIO_STOP_LEND,
  ESPRESSO_MAX_COMPUTE_BORROW_IN,
  ESPRESSO_MAX_COMPUTE_LEND_OUT,
  ESPRESSO_SIMPLE_SOLUTION,
  ESPRESSO_COPYBACK_METHOD,
  ESPRESSO_COPYBACK_INTERVAL,
  ESPRESSO_COPYBACK_CAP_THRESHOLD
} ESPRESSO_CONFIG;

class Config : public BaseConfig {
 private:
  bool enableESPRESSO;
  uint64_t checkBusyInterval;
  float dramRatio; // 1 means totally cache (i.e., 1 GB / 1 TB)
  float ghostCacheMissRatioThreshold;       // borrow/lend start (default 0.10)
  float ghostCacheMissRatioStopBorrow;      // borrow stop (default 0.08)
  float ghostCacheMissRatioStopLend;        // lend stop (default 0.12)
  uint8_t maxComputeBorrowIn;
  uint8_t maxComputeLendOut;
  bool simpleSolution;
  uint8_t copyBackMethod;
  uint64_t copyBackInterval;
  uint64_t copyBackCapThreshold;

 public:
  Config();

  bool setConfig(const char *, const char *) override;
  uint64_t readUint(uint32_t) override;
  bool readBoolean(uint32_t) override;
  float readFloat(uint32_t) override;
};

}  // namespace ESPRESSO

}  // namespace SimpleSSD

#endif
