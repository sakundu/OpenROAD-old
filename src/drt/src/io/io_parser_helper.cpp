/* Authors: Lutong Wang and Bangqi Xu */
/*
 * Copyright (c) 2019, The Regents of the University of California
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <boost/graph/connected_components.hpp>
#include <boost/polygon/polygon.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include "frBaseTypes.h"
#include "frProfileTask.h"
#include "global.h"
#include "io/io.h"

using namespace std;
using namespace fr;
using namespace boost::polygon::operators;

using Rectangle = boost::polygon::rectangle_data<int>;

void io::Parser::initDefaultVias()
{
  for (auto& uViaDef : tech->getVias()) {
    auto viaDef = uViaDef.get();
    tech->getLayer(viaDef->getCutLayerNum())->addViaDef(viaDef);
  }

  std::map<frLayerNum, std::map<int, std::map<viaRawPriorityTuple, frViaDef*>>>
      layerNum2ViaDefs;
  for (auto layerNum = design->getTech()->getBottomLayerNum();
       layerNum <= design->getTech()->getTopLayerNum();
       ++layerNum) {
    if (design->getTech()->getLayer(layerNum)->getType()
        != dbTechLayerType::CUT) {
      continue;
    }
    for (auto& viaDef : design->getTech()->getLayer(layerNum)->getViaDefs()) {
      int cutNum = int(viaDef->getCutFigs().size());
      viaRawPriorityTuple priority;
      getViaRawPriority(viaDef, priority);
      layerNum2ViaDefs[layerNum][cutNum][priority] = viaDef;
    }
    if (!layerNum2ViaDefs[layerNum][1].empty()) {
      auto defaultSingleCutVia
          = (layerNum2ViaDefs[layerNum][1].begin())->second;
      tech->getLayer(layerNum)->setDefaultViaDef(defaultSingleCutVia);
    } else {
      if (layerNum >= BOTTOM_ROUTING_LAYER) {
        logger->error(DRT,
                      234,
                      "{} does not have single-cut via.",
                      tech->getLayer(layerNum)->getName());
      }
    }
    // generate via if default via enclosure is not along pref dir
    if (ENABLE_VIA_GEN && layerNum >= BOTTOM_ROUTING_LAYER) {
      auto techDefautlViaDef = tech->getLayer(layerNum)->getDefaultViaDef();
      frVia via(techDefautlViaDef);
      Rect layer1Box;
      Rect layer2Box;
      frLayerNum layer1Num;
      frLayerNum layer2Num;
      via.getLayer1BBox(layer1Box);
      via.getLayer2BBox(layer2Box);
      layer1Num = techDefautlViaDef->getLayer1Num();
      layer2Num = techDefautlViaDef->getLayer2Num();
      bool isLayer1Square = (layer1Box.xMax() - layer1Box.xMin())
                            == (layer1Box.yMax() - layer1Box.yMin());
      bool isLayer2Square = (layer2Box.xMax() - layer2Box.xMin())
                            == (layer2Box.yMax() - layer2Box.yMin());
      bool isLayer1EncHorz = (layer1Box.xMax() - layer1Box.xMin())
                             > (layer1Box.yMax() - layer1Box.yMin());
      bool isLayer2EncHorz = (layer2Box.xMax() - layer2Box.xMin())
                             > (layer2Box.yMax() - layer2Box.yMin());
      bool isLayer1Horz
          = (tech->getLayer(layer1Num)->getDir() == dbTechLayerDir::HORIZONTAL);
      bool isLayer2Horz
          = (tech->getLayer(layer2Num)->getDir() == dbTechLayerDir::HORIZONTAL);
      bool needViaGen = false;
      if ((!isLayer1Square && (isLayer1EncHorz != isLayer1Horz))
          || (!isLayer2Square && (isLayer2EncHorz != isLayer2Horz))) {
        needViaGen = true;
      }

      // generate new via def if needed
      if (needViaGen) {
        string viaDefName
            = tech->getLayer(techDefautlViaDef->getCutLayerNum())->getName();
        viaDefName += string("_FR");
        logger->warn(DRT,
                     160,
                     "Warning: {} does not have viaDef aligned with layer "
                     "direction, generating new viaDef {}.",
                     tech->getLayer(layer1Num)->getName(),
                     viaDefName);
        // routing layer shape
        // rotate if needed
        if (isLayer1EncHorz != isLayer1Horz) {
          layer1Box.init(layer1Box.yMin(),
                         layer1Box.xMin(),
                         layer1Box.yMax(),
                         layer1Box.xMax());
        }
        if (isLayer2EncHorz != isLayer2Horz) {
          layer2Box.init(layer2Box.yMin(),
                         layer2Box.xMin(),
                         layer2Box.yMax(),
                         layer2Box.xMax());
        }

        unique_ptr<frShape> uBotFig = make_unique<frRect>();
        auto botFig = static_cast<frRect*>(uBotFig.get());
        unique_ptr<frShape> uTopFig = make_unique<frRect>();
        auto topFig = static_cast<frRect*>(uTopFig.get());

        botFig->setBBox(layer1Box);
        topFig->setBBox(layer2Box);
        botFig->setLayerNum(layer1Num);
        topFig->setLayerNum(layer2Num);

        // cut layer shape
        unique_ptr<frShape> uCutFig = make_unique<frRect>();
        auto cutFig = static_cast<frRect*>(uCutFig.get());
        Rect cutBox;
        via.getCutBBox(cutBox);
        cutFig->setBBox(cutBox);
        cutFig->setLayerNum(techDefautlViaDef->getCutLayerNum());

        // create via
        auto viaDef = make_unique<frViaDef>(viaDefName);
        viaDef->addLayer1Fig(std::move(uBotFig));
        viaDef->addLayer2Fig(std::move(uTopFig));
        viaDef->addCutFig(std::move(uCutFig));
        viaDef->setAddedByRouter(true);
        tech->getLayer(layerNum)->setDefaultViaDef(viaDef.get());
        tech->addVia(std::move(viaDef));
      }
    }
  }
}

// initialize secondLayerNum for rules that apply, reset the samenet rule if
// corresponding diffnet rule does not exist
void io::Parser::initConstraintLayerIdx()
{
  for (auto layerNum = design->getTech()->getBottomLayerNum();
       layerNum <= design->getTech()->getTopLayerNum();
       ++layerNum) {
    auto layer = design->getTech()->getLayer(layerNum);
    // non-LEF58
    auto& interLayerCutSpacingConstraints
        = layer->getInterLayerCutSpacingConstraintRef(false);
    // diff-net
    if (interLayerCutSpacingConstraints.empty()) {
      interLayerCutSpacingConstraints.resize(
          design->getTech()->getTopLayerNum() + 1, nullptr);
    }
    for (auto& [secondLayerName, con] :
         layer->getInterLayerCutSpacingConstraintMap(false)) {
      auto secondLayer = design->getTech()->getLayer(secondLayerName);
      if (secondLayer == nullptr) {
        logger->warn(
            DRT, 235, "Second layer {} does not exist.", secondLayerName);
        continue;
      }
      auto secondLayerNum
          = design->getTech()->getLayer(secondLayerName)->getLayerNum();
      con->setSecondLayerNum(secondLayerNum);
      logger->info(DRT,
                   236,
                   "Updating diff-net cut spacing rule between {} and {}.",
                   design->getTech()->getLayer(layerNum)->getName(),
                   design->getTech()->getLayer(secondLayerNum)->getName());
      interLayerCutSpacingConstraints[secondLayerNum] = con;
    }
    // same-net
    auto& interLayerCutSpacingSamenetConstraints
        = layer->getInterLayerCutSpacingConstraintRef(true);
    if (interLayerCutSpacingSamenetConstraints.empty()) {
      interLayerCutSpacingSamenetConstraints.resize(
          design->getTech()->getTopLayerNum() + 1, nullptr);
    }
    for (auto& [secondLayerName, con] :
         layer->getInterLayerCutSpacingConstraintMap(true)) {
      auto secondLayer = design->getTech()->getLayer(secondLayerName);
      if (secondLayer == nullptr) {
        logger->warn(
            DRT, 237, "Second layer {} does not exist.", secondLayerName);
        continue;
      }
      auto secondLayerNum
          = design->getTech()->getLayer(secondLayerName)->getLayerNum();
      con->setSecondLayerNum(secondLayerNum);
      logger->info(DRT,
                   238,
                   "Updating same-net cut spacing rule between {} and {}.",
                   design->getTech()->getLayer(layerNum)->getName(),
                   design->getTech()->getLayer(secondLayerNum)->getName());
      interLayerCutSpacingSamenetConstraints[secondLayerNum] = con;
    }
    // reset same-net if diff-net does not exist
    for (int i = 0; i < (int) interLayerCutSpacingConstraints.size(); i++) {
      if (interLayerCutSpacingConstraints[i] == nullptr) {
        // cout << i << endl << flush;
        interLayerCutSpacingSamenetConstraints[i] = nullptr;
      } else {
        interLayerCutSpacingConstraints[i]->setSameNetConstraint(
            interLayerCutSpacingSamenetConstraints[i]);
      }
    }
    // LEF58
    // diff-net
    for (auto& con : layer->getLef58CutSpacingConstraints(false)) {
      if (con->hasSecondLayer()) {
        frLayerNum secondLayerNum = design->getTech()
                                        ->getLayer(con->getSecondLayerName())
                                        ->getLayerNum();
        con->setSecondLayerNum(secondLayerNum);
      }
    }
    // same-net
    for (auto& con : layer->getLef58CutSpacingConstraints(true)) {
      if (con->hasSecondLayer()) {
        frLayerNum secondLayerNum = design->getTech()
                                        ->getLayer(con->getSecondLayerName())
                                        ->getLayerNum();
        con->setSecondLayerNum(secondLayerNum);
      }
    }
  }
}

// initialize cut layer width for cut OBS DRC check if not specified in LEF
void io::Parser::initCutLayerWidth()
{
  for (auto layerNum = design->getTech()->getBottomLayerNum();
       layerNum <= design->getTech()->getTopLayerNum();
       ++layerNum) {
    if (design->getTech()->getLayer(layerNum)->getType()
        != dbTechLayerType::CUT) {
      continue;
    }
    auto layer = design->getTech()->getLayer(layerNum);
    // update cut layer width is not specifed in LEF
    if (layer->getWidth() == 0) {
      // first check default via size, if it is square, use that size
      auto viaDef = layer->getDefaultViaDef();
      if (viaDef) {
        auto cutFig = viaDef->getCutFigs()[0].get();
        if (cutFig->typeId() != frcRect) {
          logger->error(DRT, 239, "Non-rectangular shape in via definition.");
        }
        auto cutRect = static_cast<frRect*>(cutFig);
        auto viaWidth = cutRect->width();
        layer->setWidth(viaWidth);
        if (viaDef->getNumCut() == 1) {
          if (cutRect->width() != cutRect->length()) {
            logger->warn(DRT,
                         240,
                         "CUT layer {} does not have square single-cut via, "
                         "cut layer width may be set incorrectly.",
                         layer->getName());
          }
        } else {
          logger->warn(DRT,
                       241,
                       "CUT layer {} does not have single-cut via, cut layer "
                       "width may be set incorrectly.",
                       layer->getName());
        }
      } else {
        if (layerNum >= BOTTOM_ROUTING_LAYER) {
          logger->error(DRT,
                        242,
                        "CUT layer {} does not have default via.",
                        layer->getName());
        }
      }
    } else {
      auto viaDef = layer->getDefaultViaDef();
      int cutLayerWidth = layer->getWidth();
      if (viaDef) {
        auto cutFig = viaDef->getCutFigs()[0].get();
        if (cutFig->typeId() != frcRect) {
          logger->error(DRT, 243, "Non-rectangular shape in via definition.");
        }
        auto cutRect = static_cast<frRect*>(cutFig);
        int viaWidth = cutRect->width();
        if (cutLayerWidth < viaWidth) {
          logger->warn(DRT,
                       244,
                       "CUT layer {} has smaller width defined in LEF compared "
                       "to default via.",
                       layer->getName());
        }
      }
    }
  }
}

void io::Parser::getViaRawPriority(frViaDef* viaDef,
                                   viaRawPriorityTuple& priority)
{
  bool isNotDefaultVia = !(viaDef->getDefault());
  bool isNotUpperAlign = false;
  bool isNotLowerAlign = false;
  using PolygonSet = std::vector<boost::polygon::polygon_90_data<int>>;
  PolygonSet viaLayerPS1;

  for (auto& fig : viaDef->getLayer1Figs()) {
    Rect bbox;
    fig->getBBox(bbox);
    Rectangle bboxRect(bbox.xMin(), bbox.yMin(), bbox.xMax(), bbox.yMax());
    viaLayerPS1 += bboxRect;
  }
  Rectangle layer1Rect;
  extents(layer1Rect, viaLayerPS1);
  bool isLayer1Horz
      = (xh(layer1Rect) - xl(layer1Rect)) > (yh(layer1Rect) - yl(layer1Rect));
  frCoord layer1Width = std::min((xh(layer1Rect) - xl(layer1Rect)),
                                 (yh(layer1Rect) - yl(layer1Rect)));
  isNotLowerAlign = (isLayer1Horz
                     && (tech->getLayer(viaDef->getLayer1Num())->getDir()
                         == dbTechLayerDir::VERTICAL))
                    || (!isLayer1Horz
                        && (tech->getLayer(viaDef->getLayer1Num())->getDir()
                            == dbTechLayerDir::HORIZONTAL));

  PolygonSet viaLayerPS2;
  for (auto& fig : viaDef->getLayer2Figs()) {
    Rect bbox;
    fig->getBBox(bbox);
    Rectangle bboxRect(bbox.xMin(), bbox.yMin(), bbox.xMax(), bbox.yMax());
    viaLayerPS2 += bboxRect;
  }
  Rectangle layer2Rect;
  extents(layer2Rect, viaLayerPS2);
  bool isLayer2Horz
      = (xh(layer2Rect) - xl(layer2Rect)) > (yh(layer2Rect) - yl(layer2Rect));
  frCoord layer2Width = std::min((xh(layer2Rect) - xl(layer2Rect)),
                                 (yh(layer2Rect) - yl(layer2Rect)));
  isNotUpperAlign = (isLayer2Horz
                     && (tech->getLayer(viaDef->getLayer2Num())->getDir()
                         == dbTechLayerDir::VERTICAL))
                    || (!isLayer2Horz
                        && (tech->getLayer(viaDef->getLayer2Num())->getDir()
                            == dbTechLayerDir::HORIZONTAL));

  frCoord layer1Area = area(viaLayerPS1);
  frCoord layer2Area = area(viaLayerPS2);

  priority = std::make_tuple(isNotDefaultVia,
                             layer1Width,
                             layer2Width,
                             isNotUpperAlign,
                             layer2Area,
                             layer1Area,
                             isNotLowerAlign,
                             viaDef->getName());
}

// 13M_3Mx_2Cx_4Kx_2Hx_2Gx_LB
void io::Parser::initDefaultVias_GF14(const string& node)
{
  for (int layerNum = 1; layerNum < (int) tech->getLayers().size();
       layerNum += 2) {
    for (auto& uViaDef : tech->getVias()) {
      auto viaDef = uViaDef.get();
      if (viaDef->getCutLayerNum() == layerNum
          && node == "GF14_13M_3Mx_2Cx_4Kx_2Hx_2Gx_LB") {
        switch (layerNum) {
          case 3:  // VIA1
            if (viaDef->getName() == "V1_0_15_0_25_VH_Vx") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 5:  // VIA2
            if (viaDef->getName() == "V2_0_25_0_25_HV_Vx") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 7:  // VIA3
            if (viaDef->getName() == "J3_0_25_4_40_VH_Jy") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 9:  // VIA4
            if (viaDef->getName() == "A4_0_50_0_50_HV_Ax") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 11:  // VIA5
            if (viaDef->getName() == "CK_23_28_0_26_VH_CK") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 13:  // VIA6
            if (viaDef->getName() == "U1_0_26_0_26_HV_Ux") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 15:  // VIA7
            if (viaDef->getName() == "U2_0_26_0_26_VH_Ux") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 17:  // VIA8
            if (viaDef->getName() == "U3_0_26_0_26_HV_Ux") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 19:  // VIA9
            if (viaDef->getName() == "KH_18_45_0_45_VH_KH") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 21:  // VIA10
            if (viaDef->getName() == "N1_0_45_0_45_HV_Nx") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 23:  // VIA11
            if (viaDef->getName() == "HG_18_72_18_72_VH_HG") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 25:  // VIA12
            if (viaDef->getName() == "T1_18_72_18_72_HV_Tx") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 27:  // VIA13
            if (viaDef->getName() == "VV_450_450_450_450_XX_VV") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          default:;
        }
      }
    }
  }
}

// 11m_2xa1xd3xe2y2r
void io::Parser::initDefaultVias_N16(const string& node)
{
  for (int layerNum = 1; layerNum < (int) tech->getLayers().size();
       layerNum += 2) {
    for (auto& uViaDef : tech->getVias()) {
      auto viaDef = uViaDef.get();
      if (viaDef->getCutLayerNum() == layerNum
          && node == "N16_11m_2xa1xd3xe2y2r_utrdl") {
        switch (layerNum) {
          case 1:  // VIA1
            if (viaDef->getName() == "NR_VIA1_PH") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 3:  // VIA2
            if (viaDef->getName() == "VIA2_0_25_3_32_HV_Vxa") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 5:  // VIA3
            if (viaDef->getName() == "VIA3_3_34_4_35_VH_Vxd") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 7:  // VIA4
            if (viaDef->getName() == "VIA4_0_50_0_50_HV_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 9:  // VIA5
            if (viaDef->getName() == "VIA5_0_50_0_50_VH_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 11:  // VIA6
            if (viaDef->getName() == "VIA6_0_50_0_50_HV_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 13:  // VIA7
            if (viaDef->getName() == "NR_VIA7_VH") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 15:  // VIA8
            if (viaDef->getName() == "VIA8_0_27_0_27_HV_Vy") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 17:  // VIA9
            if (viaDef->getName() == "VIA9_18_72_18_72_VH_Vr") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 19:  // VIA10
            if (viaDef->getName() == "VIA10_18_72_18_72_HV_Vr") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          default:;
        }
      } else if (viaDef->getCutLayerNum() == layerNum
                 && node == "N16_9m_2xa1xd4xe1z_utrdl") {
        switch (layerNum) {
          case 1:  // VIA1
            if (viaDef->getName() == "NR_VIA1_PH") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 3:  // VIA2
            if (viaDef->getName() == "VIA2_0_25_3_32_HV_Vxa") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 5:  // VIA3
            if (viaDef->getName() == "VIA3_3_34_4_35_VH_Vxd") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 7:  // VIA4
            if (viaDef->getName() == "VIA4_0_50_0_50_HV_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 9:  // VIA5
            if (viaDef->getName() == "VIA5_0_50_0_50_VH_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 11:  // VIA6
            if (viaDef->getName() == "VIA6_0_50_0_50_HV_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 13:  // VIA7
            if (viaDef->getName() == "VIA7_0_50_0_50_VH_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 15:  // VIA8
            if (viaDef->getName() == "VIA8_18_72_18_72_HV_Vz") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 17:  // VIA9
            if (viaDef->getName() == "RV_450_450_450_450_XX_RV") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          default:;
        }
      } else if (viaDef->getCutLayerNum() == layerNum
                 && node == "N16_9m_2xa1xd3xe2z_utrdl") {
        switch (layerNum) {
          case 1:  // VIA1
            if (viaDef->getName() == "NR_VIA1_PH") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 3:  // VIA2
            if (viaDef->getName() == "VIA2_0_25_3_32_HV_Vxa") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 5:  // VIA3
            if (viaDef->getName() == "VIA3_3_34_4_35_VH_Vxd") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 7:  // VIA4
            if (viaDef->getName() == "VIA4_0_50_0_50_HV_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 9:  // VIA5
            if (viaDef->getName() == "VIA5_0_50_0_50_VH_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 11:  // VIA6
            if (viaDef->getName() == "VIA6_0_50_0_50_HV_Vxe") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 13:  // VIA7
            if (viaDef->getName() == "VIA7_18_72_18_72_VH_Vz") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 15:  // VIA8
            if (viaDef->getName() == "VIA8_18_72_18_72_HV_Vz") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          case 17:  // VIA9
            if (viaDef->getName() == "RV_450_450_450_450_XX_RV") {
              tech->getLayer(layerNum)->setDefaultViaDef(viaDef);
            }
            break;
          default:;
        }
      }
    }
  }
}

void io::Parser::postProcess()
{
  initDefaultVias();
  if (DBPROCESSNODE == "N16_11m_2xa1xd3xe2y2r_utrdl") {
    initDefaultVias_N16(DBPROCESSNODE);
  } else if (DBPROCESSNODE == "GF14_13M_3Mx_2Cx_4Kx_2Hx_2Gx_LB") {
    initDefaultVias_GF14(DBPROCESSNODE);
  } else {
    ;
  }
  initCutLayerWidth();
  initConstraintLayerIdx();
  tech->printDefaultVias(logger);

  instAnalysis();
  // init region query
  logger->info(DRT, 168, "Init region query.");
  design->getRegionQuery()->init();
  design->getRegionQuery()->print();
  design->getRegionQuery()->initDRObj();  // second init from FlexDR.cpp
}

void io::Parser::postProcessGuide(odb::dbDatabase* db)
{
  ProfileTask profile("IO:postProcessGuide");
  if (VERBOSE > 0) {
    logger->info(DRT, 169, "Post process guides.");
  }
  buildGCellPatterns(db);

  design->getRegionQuery()->initOrigGuide(tmpGuides);
  int cnt = 0;
  // for (auto &[netName, rects]:tmpGuides) {
  //   if (design->getTopBlock()->name2net.find(netName) ==
  //   design->getTopBlock()->name2net.end()) {
  //     cout <<"Error: postProcessGuide cannot find net" <<endl;
  //     exit(1);
  //   }
  for (auto& [net, rects] : tmpGuides) {
    genGuides(net, rects);
    cnt++;
    if (VERBOSE > 0) {
      if (cnt < 100000) {
        if (cnt % 10000 == 0) {
          logger->report("  complete {} nets.", cnt);
        }
      } else {
        if (cnt % 100000 == 0) {
          logger->report("  complete {} nets.", cnt);
        }
      }
    }
  }

  // global unique id for guides
  int currId = 0;
  for (auto& net : design->getTopBlock()->getNets()) {
    for (auto& guide : net->getGuides()) {
      guide->setId(currId);
      currId++;
    }
  }

  logger->info(DRT, 178, "Init guide query.");
  design->getRegionQuery()->initGuide();
  design->getRegionQuery()->printGuide();
  logger->info(DRT, 179, "Init gr pin query.");
  design->getRegionQuery()->initGRPin(tmpGRPins);

  if (OUTGUIDE_FILE == string("")) {
    if (VERBOSE > 0) {
      logger->warn(
          DRT, 245, "No output guide specified, skipped writing guide.");
    }
  } else {
    // if (VERBOSE > 0) {
    //   cout <<endl <<"start writing output guide" <<endl;
    // }
    writeGuideFile();
  }
}

// instantiate RPin and region query for RPin
void io::Parser::initRPin()
{
  if (VERBOSE > 0) {
    logger->info(DRT, 185, "Post process initialize RPin region query.");
  }
  initRPin_rpin();
  initRPin_rq();
}

void io::Parser::initRPin_rpin()
{
  for (auto& net : design->getTopBlock()->getNets()) {
    // instTerm
    for (auto& instTerm : net->getInstTerms()) {
      auto inst = instTerm->getInst();
      int pinIdx = 0;
      auto trueTerm = instTerm->getTerm();
      for (auto& pin : trueTerm->getPins()) {
        auto rpin = make_unique<frRPin>();
        rpin->setFrTerm(instTerm);
        rpin->addToNet(net.get());
        frAccessPoint* prefAp = (instTerm->getAccessPoints())[pinIdx];

        // MACRO does not go through PA
        if (prefAp == nullptr) {
          dbMasterType masterType = inst->getMaster()->getMasterType();
          if (masterType.isBlock() || masterType.isPad()
              || masterType == dbMasterType::RING) {
            prefAp = (pin->getPinAccess(inst->getPinAccessIdx())
                          ->getAccessPoints())[0]
                         .get();
          } else
            continue;
        }

        if (prefAp == nullptr) {
          logger->warn(DRT,
                       246,
                       "{}/{} from {} has nullptr as prefAP.",
                       instTerm->getInst()->getName(),
                       trueTerm->getName(),
                       net->getName());
        }

        rpin->setAccessPoint(prefAp);

        net->addRPin(rpin);

        pinIdx++;
      }
    }
    // term
    for (auto& term : net->getBTerms()) {
      int pinIdx = 0;
      auto trueTerm = term;
      for (auto& pin : trueTerm->getPins()) {
        auto rpin = make_unique<frRPin>();
        rpin->setFrTerm(term);
        rpin->addToNet(net.get());
        frAccessPoint* prefAp
            = (pin->getPinAccess(0)->getAccessPoints())[0].get();
        rpin->setAccessPoint(prefAp);

        net->addRPin(rpin);

        pinIdx++;
      }
    }
  }
}

void io::Parser::initRPin_rq()
{
  design->getRegionQuery()->initRPin();
}

void io::Parser::buildGCellPatterns_helper(frCoord& GCELLGRIDX,
                                           frCoord& GCELLGRIDY,
                                           frCoord& GCELLOFFSETX,
                                           frCoord& GCELLOFFSETY)
{
  buildGCellPatterns_getWidth(GCELLGRIDX, GCELLGRIDY);
  buildGCellPatterns_getOffset(
      GCELLGRIDX, GCELLGRIDY, GCELLOFFSETX, GCELLOFFSETY);
}

void io::Parser::buildGCellPatterns_getWidth(frCoord& GCELLGRIDX,
                                             frCoord& GCELLGRIDY)
{
  map<frCoord, int> guideGridXMap, guideGridYMap;
  // get GCell size information loop
  for (auto& [netName, rects] : tmpGuides) {
    for (auto& rect : rects) {
      frLayerNum layerNum = rect.getLayerNum();
      Rect guideBBox;
      rect.getBBox(guideBBox);
      frCoord guideWidth
          = (tech->getLayer(layerNum)->getDir() == dbTechLayerDir::HORIZONTAL)
                ? (guideBBox.yMax() - guideBBox.yMin())
                : (guideBBox.xMax() - guideBBox.xMin());
      if (tech->getLayer(layerNum)->getDir() == dbTechLayerDir::HORIZONTAL) {
        if (guideGridYMap.find(guideWidth) == guideGridYMap.end()) {
          guideGridYMap[guideWidth] = 0;
        }
        guideGridYMap[guideWidth]++;
      } else if (tech->getLayer(layerNum)->getDir()
                 == dbTechLayerDir::VERTICAL) {
        if (guideGridXMap.find(guideWidth) == guideGridXMap.end()) {
          guideGridXMap[guideWidth] = 0;
        }
        guideGridXMap[guideWidth]++;
      }
    }
  }
  frCoord tmpGCELLGRIDX = -1, tmpGCELLGRIDY = -1;
  int tmpGCELLGRIDXCnt = -1, tmpGCELLGRIDYCnt = -1;
  for (auto mapIt = guideGridXMap.begin(); mapIt != guideGridXMap.end();
       ++mapIt) {
    auto cnt = mapIt->second;
    if (cnt > tmpGCELLGRIDXCnt) {
      tmpGCELLGRIDXCnt = cnt;
      tmpGCELLGRIDX = mapIt->first;
    }
  }
  for (auto mapIt = guideGridYMap.begin(); mapIt != guideGridYMap.end();
       ++mapIt) {
    auto cnt = mapIt->second;
    if (cnt > tmpGCELLGRIDYCnt) {
      tmpGCELLGRIDYCnt = cnt;
      tmpGCELLGRIDY = mapIt->first;
    }
  }
  if (tmpGCELLGRIDX != -1) {
    GCELLGRIDX = tmpGCELLGRIDX;
  } else {
    logger->error(DRT, 170, "No GCELLGRIDX.");
  }
  if (tmpGCELLGRIDY != -1) {
    GCELLGRIDY = tmpGCELLGRIDY;
  } else {
    logger->error(DRT, 171, "No GCELLGRIDY.");
  }
}

void io::Parser::buildGCellPatterns_getOffset(frCoord GCELLGRIDX,
                                              frCoord GCELLGRIDY,
                                              frCoord& GCELLOFFSETX,
                                              frCoord& GCELLOFFSETY)
{
  std::map<frCoord, int> guideOffsetXMap, guideOffsetYMap;
  // get GCell offset information loop
  for (auto& [netName, rects] : tmpGuides) {
    for (auto& rect : rects) {
      // frLayerNum layerNum = rect.getLayerNum();
      Rect guideBBox;
      rect.getBBox(guideBBox);
      frCoord guideXOffset = guideBBox.xMin() % GCELLGRIDX;
      frCoord guideYOffset = guideBBox.yMin() % GCELLGRIDY;
      if (guideXOffset < 0) {
        guideXOffset = GCELLGRIDX - guideXOffset;
      }
      if (guideYOffset < 0) {
        guideYOffset = GCELLGRIDY - guideYOffset;
      }
      if (guideOffsetXMap.find(guideXOffset) == guideOffsetXMap.end()) {
        guideOffsetXMap[guideXOffset] = 0;
      }
      guideOffsetXMap[guideXOffset]++;
      if (guideOffsetYMap.find(guideYOffset) == guideOffsetYMap.end()) {
        guideOffsetYMap[guideYOffset] = 0;
      }
      guideOffsetYMap[guideYOffset]++;
    }
  }
  frCoord tmpGCELLOFFSETX = -1, tmpGCELLOFFSETY = -1;
  int tmpGCELLOFFSETXCnt = -1, tmpGCELLOFFSETYCnt = -1;
  for (auto mapIt = guideOffsetXMap.begin(); mapIt != guideOffsetXMap.end();
       ++mapIt) {
    auto cnt = mapIt->second;
    if (cnt > tmpGCELLOFFSETXCnt) {
      tmpGCELLOFFSETXCnt = cnt;
      tmpGCELLOFFSETX = mapIt->first;
    }
  }
  for (auto mapIt = guideOffsetYMap.begin(); mapIt != guideOffsetYMap.end();
       ++mapIt) {
    auto cnt = mapIt->second;
    if (cnt > tmpGCELLOFFSETYCnt) {
      tmpGCELLOFFSETYCnt = cnt;
      tmpGCELLOFFSETY = mapIt->first;
    }
  }
  if (tmpGCELLOFFSETX != -1) {
    GCELLOFFSETX = tmpGCELLOFFSETX;
  } else {
    logger->error(DRT, 172, "No GCELLGRIDX.");
  }
  if (tmpGCELLOFFSETY != -1) {
    GCELLOFFSETY = tmpGCELLOFFSETY;
  } else {
    logger->error(DRT, 173, "No GCELLGRIDY.");
  }
}

void io::Parser::buildGCellPatterns(odb::dbDatabase* db)
{
  // horizontal = false is gcell lines along y direction (x-grid)
  frGCellPattern xgp, ygp;
  frCoord GCELLOFFSETX, GCELLOFFSETY, GCELLGRIDX, GCELLGRIDY;
  auto gcellGrid = db->getChip()->getBlock()->getGCellGrid();
  if (gcellGrid != nullptr && gcellGrid->getNumGridPatternsX() == 1
      && gcellGrid->getNumGridPatternsY() == 1) {
    frCoord COUNTX, COUNTY;
    gcellGrid->getGridPatternX(0, GCELLOFFSETX, COUNTX, GCELLGRIDX);
    gcellGrid->getGridPatternY(0, GCELLOFFSETY, COUNTY, GCELLGRIDY);
    xgp.setStartCoord(GCELLOFFSETX);
    xgp.setSpacing(GCELLGRIDX);
    xgp.setCount(COUNTX);
    xgp.setHorizontal(false);

    ygp.setStartCoord(GCELLOFFSETY);
    ygp.setSpacing(GCELLGRIDY);
    ygp.setCount(COUNTY);
    ygp.setHorizontal(true);

  } else {
    Rect dieBox;
    design->getTopBlock()->getDieBox(dieBox);
    buildGCellPatterns_helper(
        GCELLGRIDX, GCELLGRIDY, GCELLOFFSETX, GCELLOFFSETY);
    xgp.setHorizontal(false);
    // find first coord >= dieBox.xMin()
    frCoord startCoordX
        = dieBox.xMin() / (frCoord) GCELLGRIDX * (frCoord) GCELLGRIDX
          + GCELLOFFSETX;
    if (startCoordX > dieBox.xMin()) {
      startCoordX -= (frCoord) GCELLGRIDX;
    }
    xgp.setStartCoord(startCoordX);
    xgp.setSpacing(GCELLGRIDX);
    if ((dieBox.xMax() - (frCoord) GCELLOFFSETX) / (frCoord) GCELLGRIDX < 1) {
      logger->error(DRT, 174, "GCell cnt x < 1.");
    }
    xgp.setCount((dieBox.xMax() - (frCoord) startCoordX)
                 / (frCoord) GCELLGRIDX);

    ygp.setHorizontal(true);
    // find first coord >= dieBox.yMin()
    frCoord startCoordY
        = dieBox.yMin() / (frCoord) GCELLGRIDY * (frCoord) GCELLGRIDY
          + GCELLOFFSETY;
    if (startCoordY > dieBox.yMin()) {
      startCoordY -= (frCoord) GCELLGRIDY;
    }
    ygp.setStartCoord(startCoordY);
    ygp.setSpacing(GCELLGRIDY);
    if ((dieBox.yMax() - (frCoord) GCELLOFFSETY) / (frCoord) GCELLGRIDY < 1) {
      logger->error(DRT, 175, "GCell cnt y < 1.");
    }
    ygp.setCount((dieBox.yMax() - startCoordY) / (frCoord) GCELLGRIDY);
  }

  if (VERBOSE > 0 || logger->debugCheck(DRT, "autotuner", 1)) {
    logger->info(DRT,
                 176,
                 "GCELLGRID X {} DO {} STEP {} ;",
                 ygp.getStartCoord(),
                 ygp.getCount(),
                 ygp.getSpacing());
    logger->info(DRT,
                 177,
                 "GCELLGRID Y {} DO {} STEP {} ;",
                 xgp.getStartCoord(),
                 xgp.getCount(),
                 xgp.getSpacing());
  }

  design->getTopBlock()->setGCellPatterns({xgp, ygp});

  for (int layerNum = 0; layerNum <= (int) tech->getLayers().size();
       layerNum += 2) {
    for (int i = 0; i < (int) xgp.getCount(); i++) {
      for (int j = 0; j < (int) ygp.getCount(); j++) {
        Rect gcellBox;
        design->getTopBlock()->getGCellBox(Point(i, j), gcellBox);
        bool isH = (tech->getLayers().at(layerNum)->getDir()
                    == dbTechLayerDir::HORIZONTAL);
        frCoord gcLow = isH ? gcellBox.yMin() : gcellBox.xMax();
        frCoord gcHigh = isH ? gcellBox.yMax() : gcellBox.xMin();
        int trackCnt = 0;
        for (auto& tp : design->getTopBlock()->getTrackPatterns(layerNum)) {
          if ((tech->getLayer(layerNum)->getDir() == dbTechLayerDir::HORIZONTAL
               && tp->isHorizontal() == false)
              || (tech->getLayer(layerNum)->getDir() == dbTechLayerDir::VERTICAL
                  && tp->isHorizontal() == true)) {
            int trackNum
                = (gcLow - tp->getStartCoord()) / (int) tp->getTrackSpacing();
            if (trackNum < 0) {
              trackNum = 0;
            }
            if (trackNum * (int) tp->getTrackSpacing() + tp->getStartCoord()
                < gcLow) {
              trackNum++;
            }
            for (;
                 trackNum < (int) tp->getNumTracks()
                 && trackNum * (int) tp->getTrackSpacing() + tp->getStartCoord()
                        < gcHigh;
                 trackNum++) {
              trackCnt++;
            }
          }
        }
      }
    }
  }
}

void io::Parser::writeGuideFile()
{
  ofstream outputGuide(OUTGUIDE_FILE.c_str());
  if (outputGuide.is_open()) {
    for (auto& net : design->topBlock_->getNets()) {
      auto netName = net->getName();
      outputGuide << netName << endl;
      outputGuide << "(\n";
      for (auto& guide : net->getGuides()) {
        Point bp, ep;
        guide->getPoints(bp, ep);
        Point bpIdx, epIdx;
        design->getTopBlock()->getGCellIdx(bp, bpIdx);
        design->getTopBlock()->getGCellIdx(ep, epIdx);
        Rect bbox, ebox;
        design->getTopBlock()->getGCellBox(bpIdx, bbox);
        design->getTopBlock()->getGCellBox(epIdx, ebox);
        frLayerNum bNum = guide->getBeginLayerNum();
        frLayerNum eNum = guide->getEndLayerNum();
        // append unit guide in case of stacked via
        if (bNum != eNum) {
          auto startLayerName = tech->getLayer(bNum)->getName();
          outputGuide << bbox.xMin() << " " << bbox.yMin() << " " << bbox.xMax()
                      << " " << bbox.yMax() << " " << startLayerName << ".5"
                      << endl;
        } else {
          auto layerName = tech->getLayer(bNum)->getName();
          outputGuide << bbox.xMin() << " " << bbox.yMin() << " " << ebox.xMax()
                      << " " << ebox.yMax() << " " << layerName << endl;
        }
      }
      outputGuide << ")\n";
    }
  }
}
