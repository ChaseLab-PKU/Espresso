#include "espresso/config.hh"

namespace SimpleSSD {

namespace ESPRESSO {
  
  const char NAME_ENABLE_ESPRESSO[] = "EnableESPRESSO";
  const char NAME_CHECK_BUSY_INTERVAL[] = "CheckBusyInterval";
  const char NAME_MAX_COMPUTE_BORROW_IN[] = "MaxComputeBorrowIn";
  const char NAME_MAX_COMPUTE_LEND_OUT[] = "MaxComputeLendOut";
  const char NAME_SIMPLE_SOLUTION[] = "SimpleSolution";
  const char NAME_COPYBACK_METHOD[] = "CopyBackMethod";
  const char NAME_COPYBACK_INTERVAL[] = "CopyBackInterval";
  const char NAME_COPYBACK_CAP_THRESHOLD[] = "CopyBackCapThreshold";
  const char NAME_DRAM_RATIO[] = "DRAMRatio";
  const char NAME_GHOST_CACHE_MISS_RATIO_THRESHOLD[] = "GhostCacheMissRatioThreshold";
  const char NAME_GHOST_CACHE_MISS_RATIO_STOP_BORROW[] =
      "GhostCacheMissRatioStopBorrow";
  const char NAME_GHOST_CACHE_MISS_RATIO_STOP_LEND[] =
      "GhostCacheMissRatioStopLend";

  Config::Config() {
    enableESPRESSO = false;
    checkBusyInterval = 0;
    maxComputeBorrowIn = 1;
    maxComputeLendOut = 1;
    ghostCacheMissRatioThreshold = 0.1f;
    ghostCacheMissRatioStopBorrow = 0.08f;
    ghostCacheMissRatioStopLend = 0.12f;
  }

  bool Config::setConfig(const char *name, const char *value) {
    bool ret = true;

    if (MATCH_NAME(NAME_ENABLE_ESPRESSO)) {
      enableESPRESSO = convertBool(value);
    }
    else if (MATCH_NAME(NAME_CHECK_BUSY_INTERVAL)) {
      checkBusyInterval = strtoul(value, nullptr, 10);
    }
    else if (MATCH_NAME(NAME_DRAM_RATIO)) {
      dramRatio = strtof(value, nullptr);
    }
    else if (MATCH_NAME(NAME_GHOST_CACHE_MISS_RATIO_THRESHOLD)) {
      ghostCacheMissRatioThreshold = strtof(value, nullptr);
    }
    else if (MATCH_NAME(NAME_GHOST_CACHE_MISS_RATIO_STOP_BORROW)) {
      ghostCacheMissRatioStopBorrow = strtof(value, nullptr);
    }
    else if (MATCH_NAME(NAME_GHOST_CACHE_MISS_RATIO_STOP_LEND)) {
      ghostCacheMissRatioStopLend = strtof(value, nullptr);
    }
    else if (MATCH_NAME(NAME_MAX_COMPUTE_BORROW_IN)) {
      maxComputeBorrowIn = (uint8_t)strtoul(value, nullptr, 10);
    }
    else if (MATCH_NAME(NAME_MAX_COMPUTE_LEND_OUT)) {
      maxComputeLendOut = (uint8_t)strtoul(value, nullptr, 10);
    }
    else if (MATCH_NAME(NAME_SIMPLE_SOLUTION)) {
      simpleSolution = convertBool(value);
    }
    else if (MATCH_NAME(NAME_COPYBACK_METHOD)) {
      copyBackMethod = (uint8_t)strtoul(value, nullptr, 10);
    }
    else if (MATCH_NAME(NAME_COPYBACK_INTERVAL)) {
      copyBackInterval = strtoul(value, nullptr, 10);
    }
    else if (MATCH_NAME(NAME_COPYBACK_CAP_THRESHOLD)) {
      copyBackCapThreshold = strtoul(value, nullptr, 10);
    }
    else {
      ret = false;
    }

    return ret;
  }

  uint64_t Config::readUint(uint32_t idx) {
    uint64_t ret = 0;

    switch (idx) {
      case ESPRESSO_CHECK_BUSY_INTERVAL:
        ret = checkBusyInterval;
        break;
      case ESPRESSO_MAX_COMPUTE_BORROW_IN:
        ret = maxComputeBorrowIn;
        break;
      case ESPRESSO_MAX_COMPUTE_LEND_OUT:
        ret = maxComputeLendOut;
        break;
      case ESPRESSO_COPYBACK_METHOD:
        ret = copyBackMethod;
        break;
      case ESPRESSO_COPYBACK_INTERVAL:
        ret = copyBackInterval;
        break;
      case ESPRESSO_COPYBACK_CAP_THRESHOLD:
        ret = copyBackCapThreshold;
    }

    return ret;
  }


  bool Config::readBoolean(uint32_t idx) {
    bool ret = false;

    switch (idx) {
      case ESPRESSO_ENABLE:
        ret = enableESPRESSO;
        break;
      case ESPRESSO_SIMPLE_SOLUTION:
        ret = simpleSolution;
        break;
    }

    return ret;
  }

  float Config::readFloat(uint32_t idx) {
    float ret = 0.f;

    switch (idx) {
      case ESPRESSO_DRAM_RATIO:
        ret = dramRatio;
        break;
      case ESPRESSO_GHOST_CACHE_MISS_RATIO_THRESHOLD:
        ret = ghostCacheMissRatioThreshold;
        break;
      case ESPRESSO_GHOST_CACHE_MISS_RATIO_STOP_BORROW:
        ret = ghostCacheMissRatioStopBorrow;
        break;
      case ESPRESSO_GHOST_CACHE_MISS_RATIO_STOP_LEND:
        ret = ghostCacheMissRatioStopLend;
        break;
    }

    return ret;
  }

}  // namespace ESPRESSO

}  // namespace SimpleSSD
