/*
 * Basically copy all implementation from xerxes_standalone.cc.
 * However, parser_config should be slightly modified.
 *
 */

#include <fstream>
#include <iostream>
#include <utility>

#include "dram/Xerxes/bus.hh"
#include "dram/Xerxes/def.hh"
#include "dram/Xerxes/device.hh"
#include "dram/Xerxes/snoop.hh"
#include "dram/Xerxes/switch.hh"
#include "dram/Xerxes/utils.hh"
#include "dram/remote.hh"

namespace xerxes {

Simulation *glb_sim = nullptr;
std::fstream *glb_log = nullptr;

void default_logger(const Packet &pkt) {
  static bool first = true;
  if (first) {
    first = false;
    XerxesLogger::info()
        << "id,type,memid,addr,send,arrive,bus_queuing,bus_time,"
           "switch_queuing,switch_time,snoop_evict,host_inv,"
           "dram_queuing,dram_time,total_time"
        << std::endl;
  }
  XerxesLogger::info() << pkt.id << "," << TypeName::of(pkt.type) << ","
                       << pkt.src << "," << std::hex << pkt.addr << std::dec
                       << "," << pkt.sent << "," << pkt.arrive << ","
                       << pkt.get_stat(NormalStatType::BUS_QUEUE_DELAY) << ","
                       << pkt.get_stat(NormalStatType::BUS_TIME) << ","
                       << pkt.get_stat(NormalStatType::SWITCH_QUEUE_DELAY)
                       << "," << pkt.get_stat(NormalStatType::SWITCH_TIME)
                       << "," << pkt.get_stat(NormalStatType::SNOOP_EVICT_DELAY)
                       << "," << pkt.get_stat(NormalStatType::HOST_INV_DELAY)
                       << ","
                       << pkt.get_stat(
                              NormalStatType::DRAM_INTERFACE_QUEUING_DELAY)
                       << "," << pkt.get_stat(NormalStatType::DRAM_TIME) << ","
                       << pkt.arrive - pkt.sent << std::endl;
}

void init_sim(Simulation *sim) {
  glb_sim = sim;
  glb_log = new std::fstream("../../output/xerxes.log", std::ios::out);
  set_pkt_logger(*glb_log, XerxesLogLevel::INFO, default_logger);
}

void set_pkt_logger(std::ostream &os, XerxesLogLevel level,
                    Packet::XerxesLoggerFunc pkt_logger) {
  XerxesLogger::set(os, level);
  Packet::pkt_logger(true, pkt_logger);
}

class EventEngine {
  std::multimap<Tick, EventFunc> events;

 public:
  EventEngine() {}

  static EventEngine *glb(EventEngine *n = nullptr) {
    static EventEngine *engine = nullptr;
    if (n != nullptr)
      engine = n;
    return engine;
  }

  void add(Tick tick, EventFunc f) { events.insert(std::make_pair(tick, f)); }

  Tick step() {
    if (!events.empty()) {
      auto tick = events.begin()->first;
      auto event = events.begin()->second;
      events.erase(events.begin());
      event();
      return tick;
    }
    return 0;
  }

  bool empty() { return events.empty(); }
} glb_engine;

void Device::sched_transit(Tick tick) {
  glb_engine.add(tick, [this]() { this->transit(); });
}

void xerxes_schedule(EventFunc f, uint64_t tick) {
  glb_engine.add(tick, f);
}

bool xerxes_events_empty() {
  return glb_engine.empty();
}

Tick step() {
  return glb_engine.step();
}

bool events_empty() {
  return glb_engine.empty();
}

Simulation *get_glb_sim() {
  return glb_sim;
}

void add_device(Device *dev) {
  glb_sim->system()->add_dev(dev);
}

void add_edge(TopoID from, TopoID to) {
  glb_sim->topology()->add_edge(from, to);
}

void build_route() {
  glb_sim->topology()->build_route();
}

void EspressoInferface::read(uint64_t src, uint64_t dst, uint64_t addr,
                             uint64_t size, uint64_t send_tick) {
  auto pkt = PktBuilder()
                 .src(src)
                 .dst(dst)
                 .addr(addr)
                 .sent(send_tick)
                 .payload(size)
                 .burst(1)
                 .type(PacketType::RD)
                 .build();
  send_pkt(pkt);
}

void EspressoInferface::write(uint64_t src, uint64_t dst, uint64_t addr,
                              uint64_t size, uint64_t send_tick) {
  auto pkt = PktBuilder()
                 .src(src)
                 .dst(dst)
                 .addr(addr)
                 .sent(send_tick)
                 .payload(size)
                 .burst(1)
                 .type(PacketType::WT)
                 .build();
  send_pkt(pkt);
}

void EspressoInferface::transit() {
  auto pkt = receive_pkt();
  if (pkt.dst == self) {
    if (!pkt.is_rsp) {
      pkt.is_rsp = true;
      pkt.dst = pkt.src;
      pkt.src = self;
      if (pkt.type == PacketType::RD)
        dram->read(nullptr, pkt.payload, pkt.arrive);
      else
        dram->write(nullptr, pkt.payload, pkt.arrive);
      send_pkt(pkt);
    }
    else {
      callback(pkt.arrive);
    }
    return;
  }
  send_pkt(pkt);
}
}  // namespace xerxes
