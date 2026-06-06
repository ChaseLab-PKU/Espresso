#ifndef __DRAM_REMOTE__
#define __DRAM_REMOTE__

#include <functional>
#include <list>
#include <map>
#include <string>

#include "dram/Xerxes/bus.hh"
#include "dram/Xerxes/def.hh"
#include "dram/Xerxes/device.hh"
#include "dram/Xerxes/simulation.hh"
#include "dram/Xerxes/snoop.hh"
#include "dram/abstract_dram.hh"
#include "dram/simple.hh"

namespace xerxes {
class EspressoInferface : public Device {
  ::SimpleSSD::DRAM::SimpleDRAM *dram;
  std::function<void(uint64_t &)> callback;

 public:
  EspressoInferface(Simulation *sim, std::string name,
                    ::SimpleSSD::ConfigReader &p)
      : Device(sim, name) {
    dram = new ::SimpleSSD::DRAM::SimpleDRAM(p);
  }

  void set_callback(std::function<void(uint64_t &)> cb) { callback = cb; }
  void read(uint64_t src, uint64_t dst, uint64_t addr, uint64_t size,
            uint64_t send_tick);
  void write(uint64_t src, uint64_t dst, uint64_t addr, uint64_t size,
             uint64_t send_tick);

  void transit() override;

  void setScheduling(bool enable) { dram->setScheduling(enable); }
  bool isScheduling() { return dram->isScheduling(); }

  void getStatList(std::vector<SimpleSSD::Stats> &list, std::string prefix) {
    dram->getStatList(list, prefix);
  }
  void getStatValues(std::vector<double> &values) {
    dram->getStatValues(values);
  }
  void resetStatValues() { dram->resetStatValues(); }
};

// AYD: On creation, first new Simulation, then init_sim, then parse_config.
// AYD: Example config files are under lib/Xerxes.configs.
// AYD: RemoteDRAM has the same interface as SimpleDRAM.
void init_sim(Simulation *sim);
void default_logger(const Packet &pkt);
void set_pkt_logger(std::ostream &os, XerxesLogLevel level,
                    Packet::XerxesLoggerFunc pkt_logger = default_logger);

Tick step();
bool events_empty();
Simulation *get_glb_sim();

void add_device(Device *dev);
void add_edge(TopoID from, TopoID to);
void build_route();
void init_xerxes_engine();
void create_switch();
}  // namespace xerxes

namespace SimpleSSD {

namespace DRAM {

class RemoteDRAM : public AbstractDRAM {
 private:
  xerxes::Simulation *xerxes;
  xerxes::Snoop *snoop;
  xerxes::DuplexBus *bus;
  xerxes::EspressoInferface *interface;
  uint64_t busy = 0;
  uint64_t load = 0;

 public:
  struct RequestInfo {
    uint64_t addr;
    uint64_t src_id;
    uint64_t dst_id;
    uint64_t magic = 0xffefccecaaaabbbb;
    RequestInfo(uint64_t addr, uint64_t src, uint64_t dst)
        : addr(addr), src_id(src), dst_id(dst){};
  };

  RemoteDRAM(ConfigReader &p);
  ~RemoteDRAM();

  xerxes::TopoID id() { return interface->id(); }

  void read(void *, uint64_t, uint64_t &) override;
  void write(void *, uint64_t, uint64_t &) override;

  void setScheduling(bool enable) override { interface->setScheduling(enable); }
  bool isScheduling() override { return interface->isScheduling(); }

  void getStatList(std::vector<Stats> &list, std::string prefix) override {
    interface->getStatList(list, prefix);
  }
  void getStatValues(std::vector<double> &values) override {
    interface->getStatValues(values);
  }
  void resetStatValues() override { interface->resetStatValues(); }
};

}  // namespace DRAM
}  // namespace SimpleSSD

#endif
