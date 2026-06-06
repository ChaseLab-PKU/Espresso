#include "dram/remote.hh"

#include <cassert>

#include "dram/Xerxes/bus.hh"
#include "dram/Xerxes/snoop.hh"
#include "dram/Xerxes/switch.hh"

namespace xerxes {
static bool has_init_sim = false;
static Switch *glb_switch = nullptr;

void init_xerxes_engine() {
  assert(has_init_sim == false);
  xerxes::init_sim(new xerxes::Simulation());
  xerxes::has_init_sim = true;
}

void create_switch() {
  assert(glb_switch == nullptr);
  SwitchConfig config{};
  uint64_t bw = 16;  // GB/s
  // bw = Flit / delay, delay = Flit / bw
  // Flit = 256B, delay = 256B / <bw>GB/s = (256/bw) ns
  config.delay = (256 / bw) * 1000;  // 1ns = 1000ps
  glb_switch = new Switch(xerxes::get_glb_sim(), config, "GlobalSwitch");
  add_device(xerxes::glb_switch);
}
}  // namespace xerxes

namespace SimpleSSD {
namespace DRAM {
RemoteDRAM::RemoteDRAM(ConfigReader &p) : AbstractDRAM(p) {
  // xerxes::init_xerxes_engine() & create_switch should be called once and at
  // last. For simplicity, we call it in main.cc

  xerxes::SnoopConfig snoop_config{};
  snoop_config.line_num = 1024;  // size = 64B * line (1024 --> 64KB)
  snoop_config.assoc = 8;
  snoop_config.max_burst_inv = 8;
  snoop_config.eviction = "LRU";

  xerxes::DuplexBusConfig bus_config{};
  bus_config.width = 32;  // 32bits
  bus_config.framing_time =
      20 * 1000;                // compute time to frame payload into flit
  bus_config.frame_size = 256;  // 256B flit
  bus_config.width = 32;        // 32bits = 8B
  // bw = payload / delay,
  // delay = payload / bw = payload / byte_width * delay_per_Trans
  // delay_per_T = byte_width / bw = width / (bw * 8) [ns]
  uint64_t bw = 4 * 4;  // GB/s, *4 for payload in flit
  bus_config.delay_per_T = bus_config.width / (bw * 8);
  bus_config.delay_per_T *= 1000;  // 1ns = 1000ps

  snoop = new xerxes::Snoop(xerxes::get_glb_sim(), snoop_config, "RemoteSnoop");
  bus = new xerxes::DuplexBus(xerxes::get_glb_sim(), bus_config, "RemoteBus");
  interface =
      new xerxes::EspressoInferface(xerxes::get_glb_sim(), "RemoteDRAM", p);
  xerxes::add_device(interface);
  xerxes::add_device(snoop);
  xerxes::add_device(bus);
  xerxes::add_edge(bus->id(), xerxes::glb_switch->id());
  xerxes::add_edge(snoop->id(), bus->id());
  xerxes::add_edge(interface->id(), snoop->id());
  snoop->add_snooping_id(interface->id());

  // xerxes::build_route() should be called once and at last.
  // For simplicity, we call it in main.cc
}

RemoteDRAM::~RemoteDRAM() {
  // DO NOTHING"
  std::cout << "RemoteDRAM Busy:" << busy << std::endl;
  std::cout << "RemoteDRAM Load:" << load << std::endl;
}

void RemoteDRAM::read(void *req_info, uint64_t size, uint64_t &tick) {
  RequestInfo *info = (RequestInfo *)req_info;
  if (info->magic != 0xffefccecaaaabbbb) {
    panic("Invalid magic number in RemoteDRAM::read");
  }
  uint64_t send_tick = tick;
  uint64_t recv_tick = 0;
  auto cb = [this, &recv_tick](uint64_t &t) { recv_tick = t; };
  interface->set_callback(cb);
  interface->read(info->src_id, info->dst_id, info->addr, size, send_tick);
  while (!xerxes::events_empty())
    xerxes::step();

  tick = recv_tick;
  if (recv_tick > send_tick) {
    busy += recv_tick - send_tick;
  }
  load += size;
  // Local dram latency
  // dram->read(req_info, size, tick);
}

void RemoteDRAM::write(void *req_info, uint64_t size, uint64_t &tick) {
  RequestInfo *info = (RequestInfo *)req_info;
  if (info->magic != 0xffefccecaaaabbbb) {
    panic("Invalid magic number in RemoteDRAM::write");
  }
  uint64_t send_tick = tick;
  uint64_t recv_tick = 0;
  auto cb = [this, &recv_tick](uint64_t &t) { recv_tick = t; };
  interface->set_callback(cb);
  interface->write(info->src_id, info->dst_id, info->addr, size, send_tick);
  while (!xerxes::events_empty())
    xerxes::step();
  tick = recv_tick;
  if (recv_tick > send_tick) {
    busy += recv_tick - send_tick;
  }
  load += size;
  // Local dram latency
  // dram->write(req_info, size, tick);
}

}  // namespace DRAM
}  // namespace SimpleSSD
