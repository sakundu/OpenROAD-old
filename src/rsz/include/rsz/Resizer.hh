/////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2019, The Regents of the University of California
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <string>

#include "SteinerTree.hh"
#include "BufferedNet.hh"

#include "utl/Logger.h"
#include "stt/SteinerTreeBuilder.h"
#include "db_sta/dbSta.hh"
#include "sta/UnorderedSet.hh"
#include "sta/Path.hh"

namespace grt {
class GlobalRouter;
class IncrementalGRoute;
}

namespace sta {
class PathExpanded;
}

namespace rsz {

using std::array;
using std::string;
using std::vector;

using ord::OpenRoad;
using utl::Logger;
using gui::Gui;

using odb::Rect;
using odb::dbDatabase;
using odb::dbNet;
using odb::dbMaster;
using odb::dbBlock;
using odb::dbTechLayer;

using stt::SteinerTreeBuilder;

using grt::GlobalRouter;
using grt::IncrementalGRoute;

using sta::StaState;
using sta::Sta;
using sta::dbSta;
using sta::dbNetwork;
using sta::Vector;
using sta::Map;
using sta::UnorderedSet;
using sta::MinMax;
using sta::Net;
using sta::NetSeq;
using sta::Instance;
using sta::InstanceSeq;
using sta::InstanceSet;
using sta::Pin;
using sta::PinSet;
using sta::Cell;
using sta::LibertyLibrary;
using sta::LibertyLibrarySeq;
using sta::LibertyCell;
using sta::LibertyCellSeq;
using sta::LibertyCellSet;
using sta::LibertyPort;
using sta::TimingArc;
using sta::RiseFall;
using sta::Vertex;
using sta::VertexSeq;
using sta::VertexSet;
using sta::Delay;
using sta::Slew;
using sta::ArcDelay;
using sta::Required;
using sta::Slack;
using sta::Corner;
using sta::DcalcAnalysisPt;
using sta::ParasiticAnalysisPt;
using sta::GateTimingModel;
using sta::Pvt;
using sta::Parasitics;
using sta::Parasitic;
using sta::ParasiticNode;
using sta::PathRef;
using sta::PathExpanded;

class SteinerRenderer;

class NetHash
{
public:
  size_t operator()(const Net *net) const { return hashPtr(net); }
};

typedef Map<LibertyCell*, float> CellTargetLoadMap;
typedef Map<Vertex*, float> VertexWeightMap;
typedef Vector<Vector<Pin*>> GroupedPins;
typedef array<Slew, RiseFall::index_count> TgtSlews;
typedef Slack Slacks[RiseFall::index_count][MinMax::index_count];
typedef Vector<BufferedNet*> BufferedNetSeq;

enum class ParasiticsSrc { none, placement, global_routing };

class Resizer : public StaState
{
public:
  Resizer();
  void init(OpenRoad *openroad,
            Tcl_Interp *interp,
            Logger *logger,
            Gui *gui,
            dbDatabase *db,
            dbSta *sta,
            SteinerTreeBuilder *stt_builder,
            GlobalRouter *global_router);

  void setLayerRC(dbTechLayer *layer,
                  const Corner *corner,
                  double res,
                  double cap);
  void layerRC(dbTechLayer *layer,
               const Corner *corner,
               // Return values.
               double &res,
               double &cap);
  // Set the resistance and capacitance used for parasitics on signal nets.
  void setWireSignalRC(const Corner *corner,
                       double res, // ohms/meter
                       double cap); // farads/meter
  // Set the resistance and capacitance used for parasitics on clock nets.
  void setWireClkRC(const Corner *corner,
                    double res,
                    double cap); // farads/meter
  // ohms/meter
  double wireSignalResistance(const Corner *corner);
  double wireClkResistance(const Corner *corner);
  // farads/meter
  double wireSignalCapacitance(const Corner *corner);
  double wireClkCapacitance(const Corner *corner);
  void estimateParasitics(ParasiticsSrc src);
  void estimateWireParasitics();
  void estimateWireParasitic(const Net *net);
  void estimateWireParasitic(const Pin *drvr_pin,
                             const Net *net);
  bool haveEstimatedParasitics() const;
  void parasiticsInvalid(const Net *net);
  void parasiticsInvalid(const dbNet *net);

  // Core area (meters).
  double coreArea() const;
  // 0.0 - 1.0 (100%) of core size.
  double utilization();
  // Maximum utilizable area (core area * utilization)
  double maxArea() const;

  void setDontUse(LibertyCellSeq *dont_use);  
  bool dontUse(LibertyCell *cell);

  void setMaxUtilization(double max_utilization);
  void resizePreamble();
  // Remove all buffers from the netlist.
  void removeBuffers();
  void bufferInputs();
  void bufferOutputs();
  // Resize all instances in the network.
  // resizerPreamble() required.
  void resizeToTargetSlew();
  // Resize inst to target slew (public for testing).
  // resizerPreamble() required.
  // Return true if resized.
  bool resizeToTargetSlew(const Pin *drvr_pin,
                          bool update_count);

  Slew targetSlew(const RiseFall *tr);
  float targetLoadCap(LibertyCell *cell);
  void repairHold(float slack_margin,
                  bool allow_setup_violations,
                  // Max buffer count as percent of design instance count.
                  float max_buffer_percent);
  void repairHold(Pin *end_pin,
                  LibertyCell *buffer_cell,
                  float slack_margin,
                  bool allow_setup_violations,
                  float max_buffer_percent);
  void repairSetup(float slack_margin,
                   int max_passes);
  // For testing.
  void repairSetup(Pin *drvr_pin);
  // Area of the design in meter^2.
  double designArea();
  // Increment design_area
  void designAreaIncr(float delta);
  // Caller owns return value.
  NetSeq *findFloatingNets();
  void repairTieFanout(LibertyPort *tie_port,
                       double separation, // meters
                       bool verbose);
  void bufferWireDelay(LibertyCell *buffer_cell,
                       double wire_length, // meters
                       Delay &delay,
                       Slew &slew);
  void cellWireDelay(LibertyPort *drvr_port,
                     LibertyPort *load_port,
                     double wire_length, // meters
                     // Return values.
                     Delay &delay,
                     Slew &slew);
  float bufferSelfDelay(LibertyCell *buffer_cell);
  float bufferSelfDelay(LibertyCell *buffer_cell,
                        const RiseFall *rf);
  // Repair long wires, max fanout violations.
  void repairDesign(double max_wire_length, // max_wire_length zero for none (meters)
                    double slew_margin, // 0.0-1.0
                    double cap_margin); // 0.0-1.0
  // repairDesign but restricted to clock network and
  // no max_fanout/max_cap checks.
  void repairClkNets(double max_wire_length); // max_wire_length zero for none (meters)
  // Clone inverters next to the registers they drive to remove them
  // from the clock network.
  // yosys is too stupid to use the inverted clock registers
  // and TritonCTS is too stupid to balance clock networks with inverters.
  void repairClkInverters();
  // for debugging
  void repairNet(Net *net,
                 double max_wire_length, // meters
                 double slew_margin,
                 double cap_margin);
  void reportLongWires(int count,
                       int digits);
  // Find the max wire length before it is faster to split the wire
  // in half with a buffer (in meters).
  double findMaxWireLength();
  double findMaxWireLength(LibertyCell *buffer_cell,
                           const Corner *corner);
  double findMaxWireLength(LibertyPort *drvr_port,
                           const Corner *corner);
  // Find the max wire length with load slew < max_slew (in meters).
  double findMaxSlewWireLength(LibertyPort *drvr_port,
                               LibertyPort *load_port,
                               double max_slew,
                               const Corner *corner);
  // Longest driver to load wire (in meters).
  double maxLoadManhattenDistance(const Net *net);

  // Rebuffer one net (for testing).
  // resizerPreamble() required.
  void rebuffer1(const Pin *drvr_pin);

  ////////////////////////////////////////////////////////////////
  // Slack API for timing driven placement.
  // Each pass (findResizeSlacks)
  //  estiimate parasitics
  //  repair design
  //  save slacks
  //  remove inserted buffers
  //  restore resized gates
  // Preamble must be called before the first findResizeSlacks.
  void resizeSlackPreamble();
  void findResizeSlacks();
  // Return 10% of nets with worst slack.
  NetSeq &resizeWorstSlackNets();
  // Return net slack.
  Slack resizeNetSlack(const Net *net);
  // db flavor
  vector<dbNet*> resizeWorstSlackDbNets();
  Slack resizeNetSlack(const dbNet *db_net);

  ////////////////////////////////////////////////////////////////
  // API for logic resynthesis
  PinSet findFaninFanouts(PinSet *end_pins);
  PinSet findFanins(PinSet *end_pins);

  ////////////////////////////////////////////////////////////////
  void highlightSteiner(const Pin *drvr);

  dbNetwork *getDbNetwork() { return db_network_; }
  double dbuToMeters(int dist) const;
  int metersToDbu(double dist) const;

protected:
  void init();
  void ensureBlock();
  void ensureDesignArea();
  void ensureLevelDrvrVertices();
  Instance *bufferInput(const Pin *top_pin,
                        LibertyCell *buffer_cell);
  void bufferOutput(Pin *top_pin,
                    LibertyCell *buffer_cell);
  bool hasTristateDriver(const Net *net);
  bool isTristateDriver(const Pin *pin);
  void makeEquivCells();
  void findBuffers();
  bool isLinkCell(LibertyCell *cell);
  void findTargetLoads();
  float findTargetLoad(LibertyCell *cell);
  float findTargetLoad(LibertyCell *cell,
                       TimingArc *arc,
                       Slew in_slew,
                       Slew out_slew);
  Slew gateSlewDiff(LibertyCell *cell,
                    TimingArc *arc,
                    GateTimingModel *model,
                    Slew in_slew,
                    float load_cap,
                    Slew out_slew);
  void findBufferTargetSlews();
  void findBufferTargetSlews(LibertyCell *buffer,
                             const Pvt *pvt,
                             // Return values.
                             Slew slews[],
                             int counts[]);
  bool hasMultipleOutputs(const Instance *inst);
  void findLongWires(VertexSeq &drvrs);
  void findLongWiresSteiner(VertexSeq &drvrs);
  int findMaxSteinerDist(Vertex *drvr);
  int findMaxSteinerDist(SteinerTree *tree);
  int findMaxSteinerDist(SteinerTree *tree,
                         SteinerPt pt,
                         int dist_from_drvr);
  void repairDesign(double max_wire_length, // zero for none (meters)
                    double slew_margin,
                    double cap_margin,
                    int &repair_count,
                    int &slew_violations,
                    int &cap_violations,
                    int &fanout_violations,
                    int &length_violations);
  void repairNet(Net *net,
                 const Pin *drvr_pin,
                 Vertex *drvr,
                 double slew_margin,
                 double cap_margin,
                 bool check_slew,
                 bool check_cap,
                 bool check_fanout,
                 int max_length, // dbu
                 bool resize_drvr,
                 int &repair_count,
                 int &slew_violations,
                 int &cap_violations,
                 int &fanout_violations,
                 int &length_violations);
  bool checkLimits(const Pin *drvr_pin,
                   double slew_margin,
                   double cap_margin,
                   bool check_slew,
                   bool check_cap,
                   bool check_fanout);
  void checkSlew(const Pin *drvr_pin,
                 double slew_margin,
                 // Return values.
                 Slew &slew,
                 float &limit,
                 float &slack,
                 const Corner *&corner);
  void checkLoadSlews(const Pin *drvr_pin,
                      double slew_margin,
                      // Return values.
                      Slew &slew,
                      float &limit,
                      float &slack,
                      const Corner *&corner);
  void repairNet(SteinerTree *tree,
                 SteinerPt pt,
                 SteinerPt prev_pt,
                 Net *net,
                 const Pin *drvr_pin,
                 float max_load_slew,
                 float max_cap,
                 float max_fanout,
                 int max_length,
                 const Corner *corner,
                 int level,
                 // Return values.
                 int &wire_length,
                 float &pin_cap,
                 float &fanout,
                 PinSeq &load_pins);
  double findSlewLoadCap(LibertyPort *drvr_port,
                         double slew,
                         const Corner *corner);
  double gateSlewDiff(LibertyPort *drvr_port,
                      double load_cap,
                      double slew,
                      const DcalcAnalysisPt *dcalc_ap);
  void makeRepeater(const char *where,
                    SteinerTree *tree,
                    SteinerPt pt,
                    LibertyCell *buffer_cell,
                    int level,
                    int &wire_length,
                    float &pin_cap,
                    float &fanout,
                    PinSeq &load_pins);
  void makeRepeater(const char *where,
                    int x,
                    int y,
                    LibertyCell *buffer_cell,
                    int level,
                    int &wire_length,
                    float &pin_cap,
                    float &fanout,
                    PinSeq &load_pins);
  float driveResistance(const Pin *drvr_pin);
  // Max distance from driver to load (in dbu).
  int maxLoadManhattenDistance(Vertex *drvr);

  float portFanoutLoad(LibertyPort *port);
  float pinCapacitance(const Pin *pin,
                       const DcalcAnalysisPt *dcalc_ap);
  float bufferInputCapacitance(LibertyCell *buffer_cell,
                               const DcalcAnalysisPt *dcalc_ap);
  void gateDelays(LibertyPort *drvr_port,
                  float load_cap,
                  const DcalcAnalysisPt *dcalc_ap,
                  // Return values.
                  ArcDelay delays[RiseFall::index_count],
                  Slew slews[RiseFall::index_count]);
  ArcDelay gateDelay(LibertyPort *drvr_port,
                     float load_cap,
                     const DcalcAnalysisPt *dcalc_ap);
  ArcDelay gateDelay(LibertyPort *drvr_port,
                     const RiseFall *rf,
                     float load_cap,
                     const DcalcAnalysisPt *dcalc_ap);
  float bufferDelay(LibertyCell *buffer_cell,
                    float load_cap,
                    const DcalcAnalysisPt *dcalc_ap);
  float bufferDelay(LibertyCell *buffer_cell,
                    const RiseFall *rf,
                    float load_cap,
                    const DcalcAnalysisPt *dcalc_ap);
  void makeWireParasitic(Net *net,
                         Pin *drvr_pin,
                         Pin *load_pin,
                         double wire_length, // meters
                         const Corner *corner,
                         Parasitics *parasitics);
  string makeUniqueNetName();
  Net *makeUniqueNet();
  string makeUniqueInstName(const char *base_name);
  string makeUniqueInstName(const char *base_name,
                            bool underscore);
  bool overMaxArea();
  bool bufferBetweenPorts(Instance *buffer);
  bool hasPort(const Net *net);
  bool hasInputPort(const Net *net);
  bool hasOutputPort(const Net *net);
  Point location(Instance *inst);
  void setLocation(Instance *inst,
                   Point pt);
  double area(dbMaster *master);
  double area(Cell *cell);
  double splitWireDelayDiff(double wire_length,
                            LibertyCell *buffer_cell);
  double maxSlewWireDiff(LibertyPort *drvr_port,
                         LibertyPort *load_port,
                         double wire_length,
                         double max_slew);
  LibertyCell *findHoldBuffer();
  float bufferHoldDelay(LibertyCell *buffer);
  void bufferHoldDelays(LibertyCell *buffer,
                        // Return values.
                        Delay delays[RiseFall::index_count]);
  void repairHold(VertexSeq &ends,
                  LibertyCell *buffer_cell,
                  float slack_margin,
                  bool allow_setup_violations,
                  int max_buffer_count);
  int repairHoldPass(VertexSeq &ends,
                     LibertyCell *buffer_cell,
                     float slack_margin,
                     bool allow_setup_violations,
                     int max_buffer_count);
  void findHoldViolations(VertexSeq &ends,
                          float slack_margin,
                          // Return values.
                          Slack &worst_slack,
                          VertexSeq &hold_violations);
  VertexSet findHoldFanins(VertexSeq &ends);
  VertexSeq sortHoldFanins(VertexSet &fanins);
  void makeHoldDelay(Vertex *drvr,
                     int buffer_count,
                     PinSeq &load_pins,
                     bool loads_have_out_port,
                     LibertyCell *buffer_cell);
  Point findCenter(PinSeq &pins);
  Slack slackGap(Vertex *vertex);
  Slack slackGap(Slacks &slacks);
  int fanout(Vertex *vertex);
  void findCellInstances(LibertyCell *cell,
                         // Return value.
                         InstanceSeq &insts);
  int fanout(Pin *drvr_pin);
  void findLoads(Pin *drvr_pin,
                 PinSeq &loads);
  bool isFuncOneZero(const Pin *drvr_pin);
  bool hasPins(Net *net);
  Point tieLocation(Pin *load,
                    int separation);
  bool hasFanout(Vertex *drvr);
  InstanceSeq findClkInverters();
  void cloneClkInverter(Instance *inv);

  void incrementalParasiticsBegin();
  void incrementalParasiticsEnd();
  void ensureParasitics();
  void updateParasitics();
  void ensureWireParasitic(const Pin *drvr_pin);
  void ensureWireParasitic(const Pin *drvr_pin,
                           const Net *net);
  void estimateWireParasiticSteiner(const Pin *drvr_pin,
                                    const Net *net);
  void makePadParasitic(const Net *net);
  bool isPadNet(const Net *net) const;
  bool isPadPin(const Pin *pin) const;
  bool isPad(const Instance *inst) const;
  void net2Pins(const Net *net,
                const Pin *&pin1,
                const Pin *&pin2) const;
  void parasiticNodeConnectPins(Parasitic *parasitic,
                                ParasiticNode *node,
                                SteinerTree *tree,
                                SteinerPt pt,
                                const ParasiticAnalysisPt *parasitics_ap);
  double wireSignalCapacitance(const Pin *drvr_pin,
                               const Net *net,
                               const Corner *corner);
  float pinCap(const Pin *drvr_pin,
               const Corner *corner);
  double grouteLength(const Net *net);

  bool repairSetup(PathRef &path,
                   Slack path_slack);
  bool upsizeDrvr(PathRef *drvr_path,
                  int drvr_index,
                  PathExpanded *expanded);
  void splitLoads(PathRef *drvr_path,
                  int drvr_index,
                  Slack drvr_slack,
                  PathExpanded *expanded);
  LibertyCell *upsizeCell(LibertyPort *in_port,
                          LibertyPort *drvr_port,
                          float load_cap,
                          float prev_drive,
                          const DcalcAnalysisPt *dcalc_ap);
  bool replaceCell(Instance *inst,
                   LibertyCell *cell,
                   bool journal);

  int rebuffer(const Pin *drvr_pin);
  BufferedNetSeq rebufferBottomUp(SteinerTree *tree,
                                  SteinerPt k,
                                  SteinerPt prev,
                                  int level,
                                  double wire_signal_cap);
  void rebufferTopDown(BufferedNet *choice,
                       Net *net,
                       int level);
  BufferedNetSeq
  addWireAndBuffer(BufferedNetSeq Z,
                   SteinerTree *tree,
                   SteinerPt k,
                   SteinerPt prev,
                   int level,
                   double wire_signal_cap);
  BufferedNet *makeBufferedNetSteiner(const Pin *drvr_pin);
  BufferedNet *makeBufferedNet(SteinerTree *tree,
                               SteinerPt k,
                               SteinerPt prev,
                               int level);
  BufferedNet *makeBufferedNetWire(SteinerTree *tree,
                                   SteinerPt from,
                                   SteinerPt to,
                                   int level);
  // BufferedNet factory.
  BufferedNet *makeBufferedNet(BufferedNetType type,
                               Point location,
                               float cap,
                               Pin *load_pin,
                               PathRef req_path,
                               Delay req_delay,
                               LibertyCell *buffer_cell,
                               BufferedNet *ref,
                               BufferedNet *ref2);
  bool hasTopLevelOutputPort(Net *net);
  void findResizeSlacks1();
  void removeBuffer(Instance *buffer);
  Instance *makeInstance(LibertyCell *cell,
                         const char *name,
                         Instance *parent);
  LibertyCell *findTargetCell(LibertyCell *cell,
                              float load_cap,
                              bool revisiting_inst);

  ////////////////////////////////////////////////////////////////
  // Jounalling support for checkpointing and backing out changes
  // during repair timing.
  void journalBegin();
  void journalInstReplaceCellBefore(Instance *inst);
  void journalMakeBuffer(Instance *buffer);
  void journalRestore();

  ////////////////////////////////////////////////////////////////
  // API for logic resynthesis
  VertexSet findFaninFanouts(VertexSet &ends);
  VertexSet findFaninRoots(VertexSet &ends);
  VertexSet findFanouts(VertexSet &roots);
  bool isRegOutput(Vertex *vertex);
  bool isRegister(Vertex *vertex);

  ////////////////////////////////////////////////////////////////
  Logger *logger() { return logger_; }

  // These are command args values.
  // Layer RC per wire length indexed by layer->getNumber(), corner->index
  vector<vector<double>> layer_res_; // ohms/meter
  vector<vector<double>> layer_cap_; // Farads/meter
  // Signal wire RC indexed by corner->index
  vector<double> wire_signal_res_;  // ohms/metre
  vector<double> wire_signal_cap_;  // Farads/meter
  // Clock wire RC.
  vector<double> wire_clk_res_;     // ohms/metre
  vector<double> wire_clk_cap_;     // Farads/meter
  LibertyCellSet dont_use_;
  double max_area_;

  OpenRoad *openroad_;
  Logger *logger_;
  SteinerTreeBuilder *stt_builder_;
  GlobalRouter *global_router_;
  IncrementalGRoute *incr_groute_;

  Gui *gui_;
  dbSta *sta_;
  dbNetwork *db_network_;
  dbDatabase *db_;
  dbBlock *block_;
  Rect core_;
  bool core_exists_;

  ParasiticsSrc parasitics_src_;
  UnorderedSet<const Net*, NetHash> parasitics_invalid_;

  double design_area_;
  const MinMax *max_;
  LibertyCellSeq buffer_cells_;
  LibertyCell *buffer_lowest_drive_;
  LibertyCell *buffer_med_drive_;
  LibertyCell *buffer_highest_drive_;

  CellTargetLoadMap *target_load_map_;
  VertexSeq level_drvr_vertices_;
  bool level_drvr_vertices_valid_;
  TgtSlews tgt_slews_;
  Corner *tgt_slew_corner_;
  const DcalcAnalysisPt *tgt_slew_dcalc_ap_;
  // Instances with multiple output ports that have been resized.
  InstanceSet resized_multi_output_insts_;
  int unique_net_index_;
  int unique_inst_index_;
  int resize_count_;
  int inserted_buffer_count_;
  // Slack map variables.
  float max_wire_length_;
  Map<const Net*, Slack> net_slack_map_;
  NetSeq worst_slack_nets_;
  SteinerRenderer *steiner_renderer_;

  int rebuffer_net_count_;
  BufferedNetSeq rebuffer_options_;

  // Journal to roll back changes (OpenDB not up to the task).
  Map<Instance*, LibertyCell*> resized_inst_map_;
  InstanceSet inserted_buffers_;

  // "factor debatable"
  static constexpr float tgt_slew_load_cap_factor = 10.0;
  static constexpr int repair_setup_decreasing_slack_passes_allowed_ = 50;
  static constexpr int rebuffer_max_fanout_ = 20;
  static constexpr double rebuffer_buffer_penalty = .005;
  static constexpr int split_load_min_fanout_ = 8;
  // Prim/Dijkstra gets out of hand with bigger nets.
  static constexpr int max_steiner_pin_count_ = 100000;

  friend class BufferedNet;
};

} // namespace
