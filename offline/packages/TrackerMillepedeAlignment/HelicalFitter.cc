#include "HelicalFitter.h"

#include "AlignmentDefs.h"
#include "Mille.h"

#include <tpc/TpcClusterZCrossingCorrection.h>

/// Tracking includes
#include <fun4all/SubsysReco.h>
#include <math.h>
#include <phool/PHIODataNode.h>
#include <phparameter/PHParameterInterface.h>
#include <trackbase/ActsSurfaceMaps.h>
#include <trackbase/InttDefs.h>
#include <trackbase/MvtxDefs.h>
#include <trackbase/TpcDefs.h>
#include <trackbase/TrackFitUtils.h>
#include <trackbase/TrkrClusterContainer.h>
#include <trackbase/TrkrClusterContainerv4.h>
#include <trackbase/TrkrClusterv3.h>
#include <trackbase/TrkrDefs.h>  // for cluskey, getTrkrId, tpcId
#include <trackbase/alignmentTransformationContainer.h>

#include <trackbase_historic/TrackSeedContainer.h>
#include <trackbase_historic/SvtxAlignmentState.h>
#include <trackbase_historic/SvtxAlignmentStateMap_v1.h>
#include <trackbase_historic/SvtxAlignmentState_v1.h>
#include <trackbase_historic/SvtxTrackMap_v2.h>
#include <trackbase_historic/SvtxTrackState_v1.h>
#include <trackbase_historic/SvtxTrack_v4.h>
#include <trackbase_historic/TrackSeedHelper.h>
#include <trackbase_historic/TrackSeed_v2.h>

#include <globalvertex/SvtxVertex.h>
#include <globalvertex/SvtxVertexMap.h>

#include <Acts/Definitions/Algebra.hpp>
#include <Acts/Definitions/Units.hpp>

#include <fun4all/Fun4AllReturnCodes.h>

#include <phool/PHCompositeNode.h>
#include <phool/getClass.h>
#include <phool/phool.h>

#include <TFile.h>
#include <TNtuple.h>

#include <climits>  // for UINT_MAX
#include <cmath>    // for fabs, sqrt
#include <fstream>
#include <iostream>  // for operator<<, basic_ostream
#include <memory>
#include <set>  // for _Rb_tree_const_iterator
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>  // for pair
#include <vector>

using namespace std;

// The derivative formulae used in this code follow the derivation in the ATLAS paper
// Global chi^2 approach to the Alignment of the ATLAS Silicon Tracking Detectors
// ATL-INDET-PUB-2005-002, 11 October 2005

namespace
{  // Anonymous
  bool arr_has_nan(float* arr)
  {
    for (int i = 0; i < AlignmentDefs::NLC; ++i)
    {
      if (isnan(arr[i]))
      {
        return true;
      }
    }
    return false;
  }
};  // namespace
//____________________________________________________________________________..
HelicalFitter::HelicalFitter(const std::string& name)
  : SubsysReco(name)
  , PHParameterInterface(name)
  , _mille(nullptr)
{
  InitializeParameters();

  vertexPosition(0) = 0;
  vertexPosition(1) = 0;

  vtx_sigma(0) = 0.005;
  vtx_sigma(1) = 0.005;
}

//____________________________________________________________________________..
int HelicalFitter::InitRun(PHCompositeNode* topNode)
{
  UpdateParametersWithMacro();

  int ret = GetNodes(topNode);
  if (ret != Fun4AllReturnCodes::EVENT_OK)
  {
    return ret;
  }

  ret = CreateNodes(topNode);
  if (ret != Fun4AllReturnCodes::EVENT_OK)
  {
    return ret;
  }

  // Instantiate Mille and open output data file
  if (test_output)
  {
    _mille = new Mille(data_outfilename.c_str(), false);  // write text in data files, rather than binary, for debugging only
  }
  else
  {
    _mille = new Mille(data_outfilename.c_str());
  }

  // Write the steering file here, and add the data file path to it
  std::ofstream steering_file(steering_outfilename);
  steering_file << data_outfilename << std::endl;
  steering_file.close();

  if (make_ntuple)
  {
    // fout = new TFile("HF_ntuple.root","recreate");
    fout = new TFile(ntuple_outfilename.c_str(), "recreate");
    if (straight_line_fit)
    {
      ntp = new TNtuple("ntp", "HF ntuple", "event:trkid:layer:nsilicon:crosshalfmvtx:ntpc:nclus:trkrid:sector:side:subsurf:phi:glbl0:glbl1:glbl2:glbl3:glbl4:glbl5:sensx:sensy:sensz:normx:normy:normz:sensxideal:sensyideal:senszideal:normxideal:normyideal:normzideal:xglobideal:yglobideal:zglobideal:XYs:Y0:Zs:Z0:xglob:yglob:zglob:xfit:yfit:zfit:xfitMvtxHalf:yfitMvtxHalf:zfitMvtxHalf:pcax:pcay:pcaz:tangx:tangy:tangz:X:Y:fitX:fitY:fitXMvtxHalf:fitYMvtxHalf:dXdXYs:dXdY0:dXdZs:dXdZ0:dXdalpha:dXdbeta:dXdgamma:dXdx:dXdy:dXdz:dYdXYs:dYdY0:dYdZs:dYdZ0:dYdalpha:dYdbeta:dYdgamma:dYdx:dYdy:dYdz");
    }
    else
    {
      ntp = new TNtuple("ntp", "HF ntuple", "event:trkid:layer:nsilicon:ntpc:nclus:trkrid:sector:side:subsurf:phi:glbl0:glbl1:glbl2:glbl3:glbl4:glbl5:sensx:sensy:sensz:normx:normy:normz:sensxideal:sensyideal:senszideal:normxideal:normyideal:normzideal:xglobideal:yglobideal:zglobideal:R:X0:Y0:Zs:Z0:xglob:yglob:zglob:xfit:yfit:zfit:pcax:pcay:pcaz:tangx:tangy:tangz:X:Y:fitX:fitY:dXdR:dXdX0:dXdY0:dXdZs:dXdZ0:dXdalpha:dXdbeta:dXdgamma:dXdx:dXdy:dXdz:dYdR:dYdX0:dYdY0:dYdZs:dYdZ0:dYdalpha:dYdbeta:dYdgamma:dYdx:dYdy:dYdz");
    }

    if (straight_line_fit)
    {
      track_ntp = new TNtuple("track_ntp", "HF track ntuple", "track_id:residual_x:residual_y:residualxsigma:residualysigma:dXdXYs:dXdY0:dXdZs:dXdZ0:dXdx:dXdy:dXdz:dYdXYs:dYdY0:dYdZs:dYdZ0:dYdx:dYdy:dYdz:track_xvtx:track_yvtx:track_zvtx:event_xvtx:event_yvtx:event_zvtx:track_phi:perigee_phi:track_eta");
    }
    else
    {
      track_ntp = new TNtuple("track_ntp", "HF track ntuple", "track_id:residual_x:residual_y:residualxsigma:residualysigma:dXdR:dXdX0:dXdY0:dXdZs:dXdZ0:dXdx:dXdy:dXdz:dYdR:dYdX0:dYdY0:dYdZs:dYdZ0:dYdx:dYdy:dYdz:track_xvtx:track_yvtx:track_zvtx:event_xvtx:event_yvtx:event_zvtx:track_phi:perigee_phi");
    }
  }

  // print grouping setup to log file:
  std::cout << "HelicalFitter::InitRun: Surface groupings are mvtx " << mvtx_grp << " intt " << intt_grp << " tpc " << tpc_grp << " mms " << mms_grp << std::endl;
  std::cout << " possible groupings are:" << std::endl
            << " mvtx "
            << AlignmentDefs::mvtxGrp::snsr << "  "
            << AlignmentDefs::mvtxGrp::stv << "  "
            << AlignmentDefs::mvtxGrp::mvtxlyr << "  "
            << AlignmentDefs::mvtxGrp::clamshl << "  " << std::endl
            << " intt "
            << AlignmentDefs::inttGrp::chp << "  "
            << AlignmentDefs::inttGrp::lad << "  "
            << AlignmentDefs::inttGrp::inttlyr << "  "
            << AlignmentDefs::inttGrp::inttbrl << "  " << std::endl
            << " tpc "
            << AlignmentDefs::tpcGrp::htst << "  "
            << AlignmentDefs::tpcGrp::sctr << "  "
            << AlignmentDefs::tpcGrp::tp << "  " << std::endl
            << " mms "
            << AlignmentDefs::mmsGrp::tl << "  "
            << AlignmentDefs::mmsGrp::mm << "  " << std::endl;

  event = -1;

  return ret;
}

//_____________________________________________________________________
void HelicalFitter::SetDefaultParameters()
{
  return;
}

//____________________________________________________________________________..
int HelicalFitter::process_event(PHCompositeNode* /*unused*/)
{
  // _track_map_tpc contains the TPC seed track stubs
  // _track_map_silicon contains the silicon seed track stubs
  // _svtx_seed_map contains the combined silicon and tpc track seeds

  event++;

  if (Verbosity() > 0)
  {
    if (_track_map_tpc)
    {
      std::cout << PHWHERE
                << " TPC seed map size " << _track_map_tpc->size() << std::endl;
    }
    if (_track_map_silicon)
    {
      std::cout << " Silicon seed map size " << _track_map_silicon->size()
                << std::endl;
    }
  }

  if (fitsilicon && _track_map_silicon != nullptr)
  {
    if (_track_map_silicon->size() == 0)
    {
      return Fun4AllReturnCodes::ABORTEVENT;
    }
  }
  if (fittpc && _track_map_tpc != nullptr)
  {
    if (_track_map_tpc->size() == 0)
    {
      return Fun4AllReturnCodes::ABORTEVENT;
    }
  }

  // Decide whether we want to make a helical fit for silicon or TPC
  unsigned int maxtracks = 0;
  unsigned int nsilicon = 0;
  unsigned int ntpc = 0;
  unsigned int nclus = 0;
  bool h2h_flag = false;
  bool mvtx_east_only = false;
  bool mvtx_west_only = false;
  if (do_mvtx_half == 0)
  {
    mvtx_east_only = true;
  }
  if (do_mvtx_half == 1)
  {
    mvtx_west_only = true;
  }
  std::vector<std::vector<Acts::Vector3>> cumulative_global_vec;
  std::vector<std::vector<TrkrDefs::cluskey>> cumulative_cluskey_vec;
  std::vector<std::vector<float>> cumulative_fitpars_vec;
  std::vector<std::vector<float>> cumulative_fitpars_mvtx_half_vec;
  std::vector<Acts::Vector3> cumulative_vertex;
  std::vector<TrackSeed> cumulative_someseed;
  std::vector<SvtxTrack_v4> cumulative_newTrack;

  if (fittpc && _track_map_tpc != nullptr)
  {
    maxtracks = _track_map_tpc->size();
  }
  if (fitsilicon && _track_map_silicon != nullptr)
  {
    maxtracks = _track_map_silicon->size();
  }
  for (unsigned int trackid = 0; trackid < maxtracks; ++trackid)
  {
    TrackSeed* tracklet = nullptr;
    if (fitsilicon && _track_map_silicon != nullptr)
    {
      tracklet = _track_map_silicon->get(trackid);
    }
    else if (fittpc && _track_map_tpc != nullptr)
    {
      tracklet = _track_map_tpc->get(trackid);
    }
    if (!tracklet)
    {
      continue;
    }

    std::vector<Acts::Vector3> global_vec;
    std::vector<TrkrDefs::cluskey> cluskey_vec;

    // Get a vector of cluster keys from the tracklet
    getTrackletClusterList(tracklet, cluskey_vec);
    if (cluskey_vec.size() < 3)
    {
      continue;
    }
    int nintt = 0;
    for (auto& key : cluskey_vec)
    {
      if (TrkrDefs::getTrkrId(key) == TrkrDefs::inttId)
      {
        nintt++;
      }
    }

    // store cluster global positions in a vector global_vec and cluskey_vec

    TrackFitUtils::getTrackletClusters(_tGeometry, _cluster_map, global_vec, cluskey_vec);

    correctTpcGlobalPositions(global_vec, cluskey_vec);


    std::vector<float> fitpars;
    std::vector<float> fitpars_mvtx_half;
    if (straight_line_fit)
    {
      fitpars = TrackFitUtils::fitClustersZeroField(global_vec, cluskey_vec, use_intt_zfit, false, false);
      fitpars_mvtx_half = TrackFitUtils::fitClustersZeroField(global_vec, cluskey_vec, use_intt_zfit, mvtx_east_only, mvtx_west_only);
      if (fitpars_mvtx_half.size() < 3)
      {
        fitpars_mvtx_half = fitpars;
      }
      if (fitpars.size() == 0)
      {
        continue;  // discard this track, not enough clusters to fit
      }

      if (Verbosity() > 1)
      {
        std::cout << " Track " << trackid << " xy slope " << fitpars[0] << " y intercept " << fitpars[1]
                  << " zslope " << fitpars[2] << " Z0 " << fitpars[3] << " eta " << tracklet->get_eta() << " phi " << tracklet->get_phi() << std::endl;
      }
    }
    else
    {
      if (fitsilicon && nintt < 2)
      {
        continue;  // discard incomplete seeds
      }
      if (fabs(tracklet->get_eta()) > m_eta_cut)
      {
        continue;
      }

      fitpars = TrackFitUtils::fitClusters(global_vec, cluskey_vec);  // do helical fit

      if (fitpars.size() == 0)
      {
        continue;  // discard this track, not enough clusters to fit
      }

      if (Verbosity() > 1)
      {
        std::cout << " Track " << trackid << " radius " << fitpars[0] << " X0 " << fitpars[1] << " Y0 " << fitpars[2]
                  << " zslope " << fitpars[3] << " Z0 " << fitpars[4] << std::endl;
      }
    }

    //// Create a track map for diagnostics
    SvtxTrack_v4 newTrack;
    newTrack.set_id(trackid);
    if (fitsilicon)
    {
      newTrack.set_silicon_seed(tracklet);
    }
    else if (fittpc)
    {
      newTrack.set_tpc_seed(tracklet);
    }

    // if a full track is requested, get the silicon clusters too and refit
    if (fittpc && fitfulltrack)
    {
      // this associates silicon clusters and adds them to the vectors
      ntpc = cluskey_vec.size();

      if (straight_line_fit)
      {
        std::tuple<double, double> linefit_xy(fitpars[0], fitpars[1]);
        nsilicon = TrackFitUtils::addClustersOnLine(linefit_xy, true, dca_cut, _tGeometry, _cluster_map, global_vec, cluskey_vec, 0, 6);
      }
      else
      {
        nsilicon = TrackFitUtils::addClusters(fitpars, dca_cut, _tGeometry, _cluster_map, global_vec, cluskey_vec, 0, 6);
      }

      if (nsilicon < 5)
      {
        continue;  // discard this TPC seed, did not get a good match to silicon
      }
      auto trackseed = std::make_unique<TrackSeed_v2>();
      for (auto& ckey : cluskey_vec)
      {
        if (TrkrDefs::getTrkrId(ckey) == TrkrDefs::TrkrId::mvtxId or
            TrkrDefs::getTrkrId(ckey) == TrkrDefs::TrkrId::inttId)
        {
          trackseed->insert_cluster_key(ckey);
        }
      }

      newTrack.set_silicon_seed(trackseed.get());

      // fit the full track now
      fitpars.clear();
      fitpars_mvtx_half.clear();

      if (straight_line_fit)
      {
        fitpars = TrackFitUtils::fitClustersZeroField(global_vec, cluskey_vec, use_intt_zfit);
        fitpars_mvtx_half = TrackFitUtils::fitClustersZeroField(global_vec, cluskey_vec, use_intt_zfit, mvtx_east_only, mvtx_west_only);
        if (fitpars.size() == 0)
        {
          continue;  // discard this track, not enough clusters to fit
        }

        if (Verbosity() > 1)
        {
          std::cout << " Track " << trackid << " dy/dx " << fitpars[0] << " y intercept " << fitpars[1]
                    << " dx/dz " << fitpars[2] << " Z0 " << fitpars[3] << " eta " << tracklet->get_eta() << " phi " << tracklet->get_phi() << std::endl;
        }
        if (fabs(tracklet->get_eta()) > m_eta_cut)
        {
          continue;
        }
      }
      else
      {
        fitpars = TrackFitUtils::fitClusters(global_vec, cluskey_vec, use_intt_zfit);  // do helical fit

        if (fitpars.size() == 0)
        {
          continue;  // discard this track, fit failed
        }

        if (Verbosity() > 1)
        {
          std::cout << " Full track " << trackid << " radius " << fitpars[0] << " X0 " << fitpars[1] << " Y0 " << fitpars[2]
                    << " zslope " << fitpars[3] << " Z0 " << fitpars[4] << std::endl;
        }
      }
    }
    else if (fitsilicon)
    {
      nsilicon = cluskey_vec.size();
      h2h_flag = TrackFitUtils::isTrackCrossMvtxHalf(cluskey_vec);
    }
    else if (fittpc && !fitfulltrack)
    {
      ntpc = cluskey_vec.size();
    }

    Acts::Vector3 const beamline(0, 0, 0);
    Acts::Vector2 pca2d;
    Acts::Vector3 track_vtx;
    if (straight_line_fit)
    {
      pca2d = TrackFitUtils::get_line_point_pca(fitpars[0], fitpars[1], beamline);
      track_vtx(0) = pca2d(0);
      track_vtx(1) = pca2d(1);
      track_vtx(2) = fitpars[3];  // z axis intercept
    }
    else
    {
      pca2d = TrackFitUtils::get_circle_point_pca(fitpars[0], fitpars[1], fitpars[2], beamline);
      track_vtx(0) = pca2d(0);
      track_vtx(1) = pca2d(1);
      track_vtx(2) = fitpars[4];  // z axis intercept
    }

    newTrack.set_crossing(tracklet->get_crossing());
    newTrack.set_id(trackid);

    /// use the track seed functions to help get the track trajectory values
    /// in the usual coordinates

    TrackSeed_v2 someseed;
    for (auto& ckey : cluskey_vec)
    {
      someseed.insert_cluster_key(ckey);
    }

    if (straight_line_fit)
    {
      someseed.set_qOverR(1.0);
      someseed.set_phi(tracklet->get_phi());

      someseed.set_X0(fitpars[0]);
      someseed.set_Y0(fitpars[1]);
      someseed.set_Z0(fitpars[3]);
      someseed.set_slope(fitpars[2]);

      auto tangent = get_line_tangent(fitpars, global_vec[0]);
      newTrack.set_x(track_vtx(0));
      newTrack.set_y(track_vtx(1));
      newTrack.set_z(track_vtx(2));
      newTrack.set_px(tangent.second(0));
      newTrack.set_py(tangent.second(1));
      newTrack.set_pz(tangent.second(2));
      newTrack.set_charge(tracklet->get_charge());
    }
    else
    {
      someseed.set_qOverR(tracklet->get_charge() / fitpars[0]);
      someseed.set_phi(tracklet->get_phi());

      someseed.set_X0(fitpars[1]);
      someseed.set_Y0(fitpars[2]);
      someseed.set_Z0(fitpars[4]);
      someseed.set_slope(fitpars[3]);

      const auto position = TrackSeedHelper::get_xyz(&someseed);

      newTrack.set_x(position.x());
      newTrack.set_y(position.y());
      newTrack.set_z(position.z());
      newTrack.set_px(someseed.get_px());
      newTrack.set_py(someseed.get_py());
      newTrack.set_pz(someseed.get_pz());
      newTrack.set_charge(tracklet->get_charge());
    }

    nclus = ntpc + nsilicon;

    // some basic track quality requirements
    if (fittpc && ntpc < 35)
    {
      if (Verbosity() > 1)
      {
        std::cout << " reject this track, ntpc = " << ntpc << std::endl;
      }
      continue;
    }
    if ((fitsilicon || fitfulltrack) && nsilicon < 3)
    {
      if (Verbosity() > 1)
      {
        std::cout << " reject this track, nsilicon = " << nsilicon << std::endl;
      }
      continue;
    }
    if (fabs(newTrack.get_eta()) > m_eta_cut)
    {
      continue;
    }
    cumulative_global_vec.push_back(global_vec);
    cumulative_cluskey_vec.push_back(cluskey_vec);
    cumulative_vertex.push_back(track_vtx);
    cumulative_fitpars_vec.push_back(fitpars);
    cumulative_fitpars_mvtx_half_vec.push_back(fitpars_mvtx_half);
    cumulative_someseed.push_back(someseed);
    cumulative_newTrack.push_back(newTrack);
  }

  // terminate loop over tracks
  // Collect fitpars for each track by intializing array of size maxtracks and populaating thorughout the loop
  // Then start new loop over tracks and for each track go over clsutaer
  //  make vector of global_vecs
  float xsum = 0;
  float ysum = 0;
  float zsum = 0;
  unsigned int const accepted_tracks = cumulative_fitpars_vec.size();

  for (unsigned int trackid = 0; trackid < accepted_tracks; ++trackid)
  {
    xsum += cumulative_vertex[trackid][0];
    ysum += cumulative_vertex[trackid][1];
    zsum += cumulative_vertex[trackid][2];
  }
  Acts::Vector3 averageVertex(xsum / accepted_tracks, ysum / accepted_tracks, zsum / accepted_tracks);

  for (unsigned int trackid = 0; trackid < accepted_tracks; ++trackid)
  {
    const auto& global_vec = cumulative_global_vec[trackid];
    const auto& cluskey_vec = cumulative_cluskey_vec[trackid];
    auto fitpars = cumulative_fitpars_vec[trackid];
    auto fitpars_mvtx_half = cumulative_fitpars_mvtx_half_vec[trackid];
    const auto& someseed = cumulative_someseed[trackid];
    auto newTrack = cumulative_newTrack[trackid];
    SvtxAlignmentStateMap::StateVec statevec;

    // get the residuals and derivatives for all clusters
    for (unsigned int ivec = 0; ivec < global_vec.size(); ++ivec)
    {
      auto global = global_vec[ivec];
      auto cluskey = cluskey_vec[ivec];
      auto cluster = _cluster_map->findCluster(cluskey);

      if (!cluster)
      {
        continue;
      }

      unsigned int const trkrid = TrkrDefs::getTrkrId(cluskey);

      // What we need now is to find the point on the surface at which the helix would intersect
      // If we have that point, we can transform the fit back to local coords
      // we have fitpars for the helix, and the cluster key - from which we get the surface

      Surface const surf = _tGeometry->maps().getSurface(cluskey, cluster);
      Acts::Vector3 helix_pca(0, 0, 0);
      Acts::Vector3 helix_tangent(0, 0, 0);
      Acts::Vector3 fitpoint;
      Acts::Vector3 fitpoint_mvtx_half;
      if (straight_line_fit)
      {
        fitpoint = get_line_surface_intersection(surf, fitpars);
        fitpoint_mvtx_half = get_line_surface_intersection(surf, fitpars_mvtx_half);
      }
      else
      {
        fitpoint = get_helix_surface_intersection(surf, fitpars, global, helix_pca, helix_tangent);
      }

      // fitpoint is the point where the helical fit intersects the plane of the surface
      // Now transform the helix fitpoint to local coordinates to compare with cluster local coordinates
      Acts::Vector3 fitpoint_local = surf->transform(_tGeometry->geometry().getGeoContext()).inverse() * (fitpoint * Acts::UnitConstants::cm);
      Acts::Vector3 fitpoint_mvtx_half_local = surf->transform(_tGeometry->geometry().getGeoContext()).inverse() * (fitpoint_mvtx_half * Acts::UnitConstants::cm);

      fitpoint_local /= Acts::UnitConstants::cm;
      fitpoint_mvtx_half_local /= Acts::UnitConstants::cm;

      auto xloc = cluster->getLocalX();  // in cm
      auto zloc = cluster->getLocalY();

      if (trkrid == TrkrDefs::tpcId)
      {
        zloc = convertTimeToZ(cluskey, cluster);
      }

      Acts::Vector2 residual(xloc - fitpoint_local(0), zloc - fitpoint_local(1));

      unsigned int const layer = TrkrDefs::getLayer(cluskey_vec[ivec]);
      float const phi = atan2(global(1), global(0));

      SvtxTrackState_v1 svtxstate(fitpoint.norm());
      svtxstate.set_x(fitpoint(0));
      svtxstate.set_y(fitpoint(1));
      svtxstate.set_z(fitpoint(2));
      std::pair<Acts::Vector3, Acts::Vector3> tangent;
      if (straight_line_fit)
      {
        tangent = get_line_tangent(fitpars, global);
      }
      else
      {
        tangent = get_helix_tangent(fitpars, global);
      }

      svtxstate.set_px(someseed.get_p() * tangent.second.x());
      svtxstate.set_py(someseed.get_p() * tangent.second.y());
      svtxstate.set_pz(someseed.get_p() * tangent.second.z());
      newTrack.insert_state(&svtxstate);

      if (Verbosity() > 1)
      {
        Acts::Vector3 loc_check = surf->transform(_tGeometry->geometry().getGeoContext()).inverse() * (global * Acts::UnitConstants::cm);
        loc_check /= Acts::UnitConstants::cm;
        std::cout << "    layer " << layer << std::endl
                  << " cluster global " << global(0) << " " << global(1) << " " << global(2) << std::endl
                  << " fitpoint " << fitpoint(0) << " " << fitpoint(1) << " " << fitpoint(2) << std::endl
                  << " fitpoint_local " << fitpoint_local(0) << " " << fitpoint_local(1) << " " << fitpoint_local(2) << std::endl
                  << " cluster local x " << cluster->getLocalX() << " cluster local y " << cluster->getLocalY() << std::endl
                  << " cluster global to local x " << loc_check(0) << " local y " << loc_check(1) << "  local z " << loc_check(2) << std::endl
                  << " cluster local residual x " << residual(0) << " cluster local residual y " << residual(1) << std::endl;
      }

      if (Verbosity() > 1)
      {
        Acts::Transform3 transform = surf->transform(_tGeometry->geometry().getGeoContext());
        std::cout << "Transform is:" << std::endl;
        std::cout << transform.matrix() << std::endl;
        Acts::Vector3 loc_check = surf->transform(_tGeometry->geometry().getGeoContext()).inverse() * (global * Acts::UnitConstants::cm);
        loc_check /= Acts::UnitConstants::cm;
        unsigned int const sector = TpcDefs::getSectorId(cluskey_vec[ivec]);
        unsigned int const side = TpcDefs::getSide(cluskey_vec[ivec]);
        std::cout << "    layer " << layer << " sector " << sector << " side " << side << " subsurf " << cluster->getSubSurfKey() << std::endl
                  << " cluster global " << global(0) << " " << global(1) << " " << global(2) << std::endl
                  << " fitpoint " << fitpoint(0) << " " << fitpoint(1) << " " << fitpoint(2) << std::endl
                  << " fitpoint_local " << fitpoint_local(0) << " " << fitpoint_local(1) << " " << fitpoint_local(2) << std::endl
                  << " cluster local x " << cluster->getLocalX() << " cluster local y " << cluster->getLocalY() << std::endl
                  << " cluster global to local x " << loc_check(0) << " local y " << loc_check(1) << "  local z " << loc_check(2) << std::endl
                  << " cluster local residual x " << residual(0) << " cluster local residual y " << residual(1) << std::endl;
      }

      // need standard deviation of measurements
      Acts::Vector2 clus_sigma = getClusterError(cluster, cluskey, global);
      if (isnan(clus_sigma(0)) || isnan(clus_sigma(1)))
      {
        continue;
      }

      int glbl_label[AlignmentDefs::NGL];
      if (layer < 3)
      {
        AlignmentDefs::getMvtxGlobalLabels(surf, cluskey, glbl_label, mvtx_grp);
      }
      else if (layer > 2 && layer < 7)
      {
        AlignmentDefs::getInttGlobalLabels(surf, cluskey, glbl_label, intt_grp);
      }
      else if (layer < 55)
      {
        AlignmentDefs::getTpcGlobalLabels(surf, cluskey, glbl_label, tpc_grp);
      }
      else
      {
        continue;
      }

      // These derivatives are for the local parameters
      float lcl_derivativeX[AlignmentDefs::NLC] = {0., 0., 0., 0., 0.};
      float lcl_derivativeY[AlignmentDefs::NLC] = {0., 0., 0., 0., 0.};
      if (straight_line_fit)
      {
        getLocalDerivativesZeroFieldXY(surf, global, fitpars, lcl_derivativeX, lcl_derivativeY, layer);
      }
      else
      {
        getLocalDerivativesXY(surf, global, fitpars, lcl_derivativeX, lcl_derivativeY, layer);
      }

      // The global derivs dimensions are [alpha/beta/gamma](x/y/z)
      float glbl_derivativeX[AlignmentDefs::NGL];
      float glbl_derivativeY[AlignmentDefs::NGL];
      getGlobalDerivativesXY(surf, global, fitpoint, fitpars, glbl_derivativeX, glbl_derivativeY, layer);

      auto alignmentstate = std::make_unique<SvtxAlignmentState_v1>();
      alignmentstate->set_residual(residual);
      alignmentstate->set_cluster_key(cluskey);
      SvtxAlignmentState::GlobalMatrix svtxglob =
          SvtxAlignmentState::GlobalMatrix::Zero();
      SvtxAlignmentState::LocalMatrix svtxloc =
          SvtxAlignmentState::LocalMatrix::Zero();
      for (int i = 0; i < AlignmentDefs::NLC; i++)
      {
        svtxloc(0, i) = lcl_derivativeX[i];
        svtxloc(1, i) = lcl_derivativeY[i];
      }
      for (int i = 0; i < AlignmentDefs::NGL; i++)
      {
        svtxglob(0, i) = glbl_derivativeX[i];
        svtxglob(1, i) = glbl_derivativeY[i];
      }

      alignmentstate->set_local_derivative_matrix(svtxloc);
      alignmentstate->set_global_derivative_matrix(svtxglob);

      statevec.push_back(alignmentstate.release());

      for (unsigned int i = 0; i < AlignmentDefs::NGL; ++i)
      {
        if (trkrid == TrkrDefs::mvtxId)
        {
          // need stave to get clamshell
          auto stave = MvtxDefs::getStaveId(cluskey_vec[ivec]);
          auto clamshell = AlignmentDefs::getMvtxClamshell(layer, stave);
          if (is_layer_param_fixed(layer, i) || is_mvtx_layer_fixed(layer, clamshell))
          {
            glbl_derivativeX[i] = 0;
            glbl_derivativeY[i] = 0;
          }
        }

        if (trkrid == TrkrDefs::inttId)
        {
          if (is_layer_param_fixed(layer, i) || is_intt_layer_fixed(layer))
          {
            glbl_derivativeX[i] = 0;
            glbl_derivativeY[i] = 0;
          }
        }

        if (trkrid == TrkrDefs::tpcId)
        {
          unsigned int const sector = TpcDefs::getSectorId(cluskey_vec[ivec]);
          unsigned int const side = TpcDefs::getSide(cluskey_vec[ivec]);
          if (is_layer_param_fixed(layer, i) || is_tpc_sector_fixed(layer, sector, side))
          {
            glbl_derivativeX[i] = 0;
            glbl_derivativeY[i] = 0;
          }
        }
      }

      // Add the measurement separately for each coordinate direction to Mille
      // set the derivatives non-zero only for parameters we want to be optimized
      // local parameter numbering is arbitrary:
      float errinf = 1.0;

      if (_layerMisalignment.find(layer) != _layerMisalignment.end())
      {
        errinf = _layerMisalignment.find(layer)->second;
      }
      if (make_ntuple)
      {
        // get the local parameters using the ideal transforms
        alignmentTransformationContainer::use_alignment = false;
        Acts::Vector3 ideal_center = surf->center(_tGeometry->geometry().getGeoContext()) * 0.1;
        Acts::Vector3 ideal_norm = -surf->normal(_tGeometry->geometry().getGeoContext(),Acts::Vector3(1,1,1), Acts::Vector3(1,1,1));
        Acts::Vector3 const ideal_local(xloc, zloc, 0.0);  // cm
        Acts::Vector3 ideal_glob = surf->transform(_tGeometry->geometry().getGeoContext()) * (ideal_local * Acts::UnitConstants::cm);
        ideal_glob /= Acts::UnitConstants::cm;
        alignmentTransformationContainer::use_alignment = true;

        Acts::Vector3 sensorCenter = surf->center(_tGeometry->geometry().getGeoContext()) * 0.1;  // cm
        Acts::Vector3 sensorNormal = -surf->normal(_tGeometry->geometry().getGeoContext(), Acts::Vector3(1,1,1), Acts::Vector3(1,1,1));
        unsigned int sector = TpcDefs::getSectorId(cluskey_vec[ivec]);
        unsigned int const side = TpcDefs::getSide(cluskey_vec[ivec]);
        unsigned int subsurf = cluster->getSubSurfKey();
        if (layer < 3)
        {
          sector = MvtxDefs::getStaveId(cluskey_vec[ivec]);
          subsurf = MvtxDefs::getChipId(cluskey_vec[ivec]);
        }
        else if (layer > 2 && layer < 7)
        {
          sector = InttDefs::getLadderPhiId(cluskey_vec[ivec]);
          subsurf = InttDefs::getLadderZId(cluskey_vec[ivec]);
        }
        if (straight_line_fit)
        {
          float ntp_data[78] = {
              (float) event, (float) trackid,
              (float) layer, (float) nsilicon, (float) h2h_flag, (float) ntpc, (float) nclus, (float) trkrid, (float) sector, (float) side,
              (float) subsurf, phi,
              (float) glbl_label[0], (float) glbl_label[1], (float) glbl_label[2], (float) glbl_label[3], (float) glbl_label[4], (float) glbl_label[5],
              (float) sensorCenter(0), (float) sensorCenter(1), (float) sensorCenter(2),
              (float) sensorNormal(0), (float) sensorNormal(1), (float) sensorNormal(2),
              (float) ideal_center(0), (float) ideal_center(1), (float) ideal_center(2),
              (float) ideal_norm(0), (float) ideal_norm(1), (float) ideal_norm(2),
              (float) ideal_glob(0), (float) ideal_glob(1), (float) ideal_glob(2),
              (float) fitpars[0], (float) fitpars[1], (float) fitpars[2], (float) fitpars[3],
              (float) global(0), (float) global(1), (float) global(2),
              (float) fitpoint(0), (float) fitpoint(1), (float) fitpoint(2),
              (float) fitpoint_mvtx_half(0), (float) fitpoint_mvtx_half(1), (float) fitpoint_mvtx_half(2),
              (float) tangent.first.x(), (float) tangent.first.y(), (float) tangent.first.z(),
              (float) tangent.second.x(), (float) tangent.second.y(), (float) tangent.second.z(),
              xloc, zloc, (float) fitpoint_local(0), (float) fitpoint_local(1), (float) fitpoint_mvtx_half_local(0), (float) fitpoint_mvtx_half_local(1),
              lcl_derivativeX[0], lcl_derivativeX[1], lcl_derivativeX[2], lcl_derivativeX[3],
              glbl_derivativeX[0], glbl_derivativeX[1], glbl_derivativeX[2], glbl_derivativeX[3], glbl_derivativeX[4], glbl_derivativeX[5],
              lcl_derivativeY[0], lcl_derivativeY[1], lcl_derivativeY[2], lcl_derivativeY[3],
              glbl_derivativeY[0], glbl_derivativeY[1], glbl_derivativeY[2], glbl_derivativeY[3], glbl_derivativeY[4], glbl_derivativeY[5]};

          ntp->Fill(ntp_data);
        }
        else
        {
          float ntp_data[75] = {
              (float) event, (float) trackid,
              (float) layer, (float) nsilicon, (float) ntpc, (float) nclus, (float) trkrid, (float) sector, (float) side,
              (float) subsurf, phi,
              (float) glbl_label[0], (float) glbl_label[1], (float) glbl_label[2], (float) glbl_label[3], (float) glbl_label[4], (float) glbl_label[5],
              (float) sensorCenter(0), (float) sensorCenter(1), (float) sensorCenter(2),
              (float) sensorNormal(0), (float) sensorNormal(1), (float) sensorNormal(2),
              (float) ideal_center(0), (float) ideal_center(1), (float) ideal_center(2),
              (float) ideal_norm(0), (float) ideal_norm(1), (float) ideal_norm(2),
              (float) ideal_glob(0), (float) ideal_glob(1), (float) ideal_glob(2),
              (float) fitpars[0], (float) fitpars[1], (float) fitpars[2], (float) fitpars[3], (float) fitpars[4],
              (float) global(0), (float) global(1), (float) global(2),
              (float) fitpoint(0), (float) fitpoint(1), (float) fitpoint(2),
              (float) tangent.first.x(), (float) tangent.first.y(), (float) tangent.first.z(),
              (float) tangent.second.x(), (float) tangent.second.y(), (float) tangent.second.z(),
              xloc, zloc, (float) fitpoint_local(0), (float) fitpoint_local(1),
              lcl_derivativeX[0], lcl_derivativeX[1], lcl_derivativeX[2], lcl_derivativeX[3], lcl_derivativeX[4],
              glbl_derivativeX[0], glbl_derivativeX[1], glbl_derivativeX[2], glbl_derivativeX[3], glbl_derivativeX[4], glbl_derivativeX[5],
              lcl_derivativeY[0], lcl_derivativeY[1], lcl_derivativeY[2], lcl_derivativeY[3], lcl_derivativeY[4],
              glbl_derivativeY[0], glbl_derivativeY[1], glbl_derivativeY[2], glbl_derivativeY[3], glbl_derivativeY[4], glbl_derivativeY[5]};

          ntp->Fill(ntp_data);

          if (Verbosity() > 2)
          {
            for (auto& i : ntp_data)
            {
              std::cout << i << "  ";
            }
            std::cout << std::endl;
          }
        }
      }

      if (!isnan(residual(0)) && clus_sigma(0) < 1.0)  // discards crazy clusters
      {
        if (arr_has_nan(lcl_derivativeX))
        {
          std::cerr << "lcl_derivativeX is NaN" << std::endl;
          continue;
        }
        if (arr_has_nan(glbl_derivativeX))
        {
          std::cerr << "glbl_derivativeX is NaN" << std::endl;
          continue;
        }
        _mille->mille(AlignmentDefs::NLC, lcl_derivativeX, AlignmentDefs::NGL, glbl_derivativeX, glbl_label, residual(0), errinf * clus_sigma(0));
      }

      if (!isnan(residual(1)) && clus_sigma(1) < 1.0)
      {
        if (arr_has_nan(lcl_derivativeY))
        {
          std::cerr << "lcl_derivativeY is NaN" << std::endl;
          continue;
        }
        if (arr_has_nan(glbl_derivativeY))
        {
          std::cerr << "glbl_derivativeY is NaN" << std::endl;
          continue;
        }
        _mille->mille(AlignmentDefs::NLC, lcl_derivativeY, AlignmentDefs::NGL, glbl_derivativeY, glbl_label, residual(1), errinf * clus_sigma(1));
      }
    }

    m_alignmentmap->insertWithKey(trackid, statevec);
    m_trackmap->insertWithKey(&newTrack, trackid);

    // if cosmics, end here, if collision track, continue with vtx
    //   skip the common vertex requirement for this track unless there are 3 tracks in the event
    if (accepted_tracks < 3)
    {
      _mille->end();
      continue;
    }
    // calculate vertex residual with perigee surface
    //-------------------------------------------------------

    Acts::Vector3 event_vtx(averageVertex(0), averageVertex(1), averageVertex(2));

    for (const auto& [vtxkey, vertex] : *m_vertexmap)
    {
      for (auto trackiter = vertex->begin_tracks(); trackiter != vertex->end_tracks(); ++trackiter)
      {
        SvtxTrack* vtxtrack = m_trackmap->get(*trackiter);
        if (vtxtrack)
        {
          unsigned int const vtxtrackid = vtxtrack->get_id();
          if (trackid == vtxtrackid)
          {
            event_vtx(0) = vertex->get_x();
            event_vtx(1) = vertex->get_y();
            event_vtx(2) = vertex->get_z();
            if (Verbosity() > 0)
            {
              std::cout << "     setting event_vertex for trackid " << trackid << " to vtxid " << vtxkey
                        << " vtx " << event_vtx(0) << "  " << event_vtx(1) << "  " << event_vtx(2) << std::endl;
            }
          }
        }
      }
    }


    // The residual for the vtx case is (event vtx - track vtx)
    // that is -dca
    float dca3dxy = 0;
    float dca3dz = 0;
    float dca3dxysigma = 0;
    float dca3dzsigma = 0;
    if (!straight_line_fit)
    {
      get_dca(newTrack, dca3dxy, dca3dz, dca3dxysigma, dca3dzsigma, event_vtx);
    }
    else
    {
      get_dca_zero_field(newTrack, dca3dxy, dca3dz, dca3dxysigma, dca3dzsigma, event_vtx);
    }

    // These are local coordinate residuals in the perigee surface
    Acts::Vector2 vtx_residual(-dca3dxy, -dca3dz);

    float lclvtx_derivativeX[AlignmentDefs::NLC];
    float lclvtx_derivativeY[AlignmentDefs::NLC];
    if (straight_line_fit)
    {
      getLocalVtxDerivativesZeroFieldXY(newTrack, event_vtx, fitpars, lclvtx_derivativeX, lclvtx_derivativeY);
    }
    else
    {
      getLocalVtxDerivativesXY(newTrack, event_vtx, fitpars, lclvtx_derivativeX, lclvtx_derivativeY);
    }

    // The global derivs dimensions are [alpha/beta/gamma](x/y/z)
    float glblvtx_derivativeX[3];
    float glblvtx_derivativeY[3];
    getGlobalVtxDerivativesXY(newTrack, event_vtx, glblvtx_derivativeX, glblvtx_derivativeY);

    if (use_event_vertex)
    {
      for (int p = 0; p < 3; p++)
      {
        if (is_vertex_param_fixed(p))
        {
          glblvtx_derivativeX[p] = 0;
          glblvtx_derivativeY[p] = 0;
        }
      }
      if (Verbosity() > 1)
      {
        std::cout << "vertex info for track " << trackid << " with charge " << newTrack.get_charge() << std::endl;

        std::cout << "vertex is " << event_vtx.transpose() << std::endl;
        std::cout << "vertex residuals " << vtx_residual.transpose()
                  << std::endl;
        std::cout << "local derivatives " << std::endl;
        for (float const i : lclvtx_derivativeX)
        {
          std::cout << i << ", ";
        }
        std::cout << std::endl;
        for (float const i : lclvtx_derivativeY)
        {
          std::cout << i << ", ";
        }
        std::cout << "global vtx derivaties " << std::endl;
        for (float const i : glblvtx_derivativeX)
        {
          std::cout << i << ", ";
        }
        std::cout << std::endl;
        for (float const i : glblvtx_derivativeY)
        {
          std::cout << i << ", ";
        }
      }

      if (!isnan(vtx_residual(0)))
      {
        if (arr_has_nan(lclvtx_derivativeX))
        {
          std::cerr << "lclvtx_derivativeX is NaN" << std::endl;
          continue;
        }
        if (arr_has_nan(glblvtx_derivativeX))
        {
          std::cerr << "glblvtx_derivativeX is NaN" << std::endl;
          continue;
        }
        _mille->mille(AlignmentDefs::NLC, lclvtx_derivativeX, AlignmentDefs::NGLVTX, glblvtx_derivativeX, AlignmentDefs::glbl_vtx_label, vtx_residual(0), vtx_sigma(0));
      }
      if (!isnan(vtx_residual(1)))
      {
        if (arr_has_nan(lclvtx_derivativeY))
        {
          std::cerr << "lclvtx_derivativeY is NaN" << std::endl;
          continue;
        }
        if (arr_has_nan(glblvtx_derivativeY))
        {
          std::cerr << "glblvtx_derivativeY is NaN" << std::endl;
          continue;
        }
        _mille->mille(AlignmentDefs::NLC, lclvtx_derivativeY, AlignmentDefs::NGLVTX, glblvtx_derivativeY, AlignmentDefs::glbl_vtx_label, vtx_residual(1), vtx_sigma(1));
      }
    }

    if (make_ntuple)
    {
      Acts::Vector3 const mom(newTrack.get_px(), newTrack.get_py(), newTrack.get_pz());
      Acts::Vector3 r = mom.cross(Acts::Vector3(0., 0., 1.));
      float const perigee_phi = atan2(r(1), r(0));
      float const track_phi = atan2(newTrack.get_py(), newTrack.get_px());
      float const track_eta = atanh(newTrack.get_pz() / newTrack.get_p());
      if (straight_line_fit)
      {
        float ntp_data[28] = {(float) trackid, (float) vtx_residual(0), (float) vtx_residual(1), (float) vtx_sigma(0), (float) vtx_sigma(1),
                              lclvtx_derivativeX[0], lclvtx_derivativeX[1], lclvtx_derivativeX[2], lclvtx_derivativeX[3],
                              glblvtx_derivativeX[0], glblvtx_derivativeX[1], glblvtx_derivativeX[2],
                              lclvtx_derivativeY[0], lclvtx_derivativeY[1], lclvtx_derivativeY[2], lclvtx_derivativeY[3],
                              glblvtx_derivativeY[0], glblvtx_derivativeY[1], glblvtx_derivativeY[2],
                              newTrack.get_x(), newTrack.get_y(), newTrack.get_z(),
                              (float) event_vtx(0), (float) event_vtx(1), (float) event_vtx(2), track_phi, perigee_phi, track_eta};

        track_ntp->Fill(ntp_data);
      }
      else
      {
        float ntp_data[29] = {(float) trackid, (float) vtx_residual(0), (float) vtx_residual(1), (float) vtx_sigma(0), (float) vtx_sigma(1),
                              lclvtx_derivativeX[0], lclvtx_derivativeX[1], lclvtx_derivativeX[2], lclvtx_derivativeX[3], lclvtx_derivativeX[4],
                              glblvtx_derivativeX[0], glblvtx_derivativeX[1], glblvtx_derivativeX[2],
                              lclvtx_derivativeY[0], lclvtx_derivativeY[1], lclvtx_derivativeY[2], lclvtx_derivativeY[3], lclvtx_derivativeY[4],
                              glblvtx_derivativeY[0], glblvtx_derivativeY[1], glblvtx_derivativeY[2],
                              newTrack.get_x(), newTrack.get_y(), newTrack.get_z(),
                              (float) event_vtx(0), (float) event_vtx(1), (float) event_vtx(2), track_phi, perigee_phi};

        track_ntp->Fill(ntp_data);
      }

    }

    if (Verbosity() > 1)
    {
      std::cout << "vtx_residual xy: " << vtx_residual(0) << " vtx_residual z: " << vtx_residual(1) << " vtx_sigma xy: " << vtx_sigma(0) << " vtx_sigma z: " << vtx_sigma(1) << std::endl;
      std::cout << "track_x " << newTrack.get_x() << "track_y " << newTrack.get_y() << "track_z " << newTrack.get_z() << std::endl;
    }
    // close out this track
    _mille->end();

  }  // end loop over tracks

  return Fun4AllReturnCodes::EVENT_OK;
}
/*
std::make_pair<unsigned int, Acts::Vector3> HelicalFitter::getAverageVertex( std::vector<Acts::Vector3> cumulative_vertex)
{




}
*/
Acts::Vector3 HelicalFitter::get_helix_surface_intersection(const Surface& surf, std::vector<float>& fitpars, Acts::Vector3 global)
{
  // we want the point where the helix intersects the plane of the surface
  // get the plane of the surface
  Acts::Vector3 const sensorCenter = surf->center(_tGeometry->geometry().getGeoContext()) * 0.1;  // convert to cm
  Acts::Vector3 sensorNormal = -surf->normal(_tGeometry->geometry().getGeoContext(), Acts::Vector3(1, 1, 1), Acts::Vector3(1, 1, 1));
  sensorNormal /= sensorNormal.norm();

  // there are analytic solutions for a line-plane intersection.
  // to use this, need to get the vector tangent to the helix near the measurement and a point on it.
  std::pair<Acts::Vector3, Acts::Vector3> const line = get_helix_tangent(fitpars, std::move(global));
  Acts::Vector3 const pca = line.first;
  Acts::Vector3 const tangent = line.second;

  Acts::Vector3 intersection = get_line_plane_intersection(pca, tangent, sensorCenter, sensorNormal);

  return intersection;
}

Acts::Vector3 HelicalFitter::get_line_surface_intersection(const Surface& surf, std::vector<float>& fitpars)
{
  // we want the point where the helix intersects the plane of the surface
  // get the plane of the surface
  Acts::Vector3 const sensorCenter = surf->center(_tGeometry->geometry().getGeoContext()) * 0.1;  // convert to cm
  Acts::Vector3 sensorNormal = -surf->normal(_tGeometry->geometry().getGeoContext(), Acts::Vector3(1, 1, 1), Acts::Vector3(1, 1, 1));
  sensorNormal /= sensorNormal.norm();

  /*
  // we need the direction of the line

  // consider a point some distance along the straight line.
  // Consider a value of x, calculate y, calculate radius, calculate z
  double x = 2;
  double y = fitpars[0]*x + fitpars[1];
  double rxy = sqrt(x*x+y*y);
  double z = fitpars[2]*rxy + fitpars[3];
  Acts::Vector3 arb_point(x, y, z);

  double x2 = 4;
  double y2 = fitpars[0]*x2 + fitpars[1];
  double rxy2 = sqrt(x2*x2+y2*y2);
  double z2 = fitpars[2]*rxy2 + fitpars[3];
  Acts::Vector3 arb_point2(x2, y2, z2);

  Acts::Vector3 tangent = arb_point2 - arb_point;   // direction of line
  */
  auto line = get_line_zero_field(fitpars);  // do not need the direction

  auto arb_point = line.first;
  auto tangent = line.second;

  Acts::Vector3 intersection = get_line_plane_intersection(arb_point, tangent, sensorCenter, sensorNormal);

  return intersection;
}

Acts::Vector3 HelicalFitter::get_helix_surface_intersection(const Surface& surf, std::vector<float>& fitpars, Acts::Vector3 global, Acts::Vector3& pca, Acts::Vector3& tangent)
{
  // we want the point where the helix intersects the plane of the surface

  // get the plane of the surface
  Acts::Vector3 const sensorCenter = surf->center(_tGeometry->geometry().getGeoContext()) * 0.1;  // convert to cm
  Acts::Vector3 sensorNormal = -surf->normal(_tGeometry->geometry().getGeoContext(), Acts::Vector3(1, 1, 1), Acts::Vector3(1, 1, 1));
  sensorNormal /= sensorNormal.norm();

  // there are analytic solutions for a line-plane intersection.
  // to use this, need to get the vector tangent to the helix near the measurement and a point on it.
  std::pair<Acts::Vector3, Acts::Vector3> const line = get_helix_tangent(fitpars, std::move(global));
  pca = line.first;
  tangent = line.second;

  Acts::Vector3 intersection = get_line_plane_intersection(pca, tangent, sensorCenter, sensorNormal);

  return intersection;
}

Acts::Vector3 HelicalFitter::get_helix_vtx(Acts::Vector3 event_vtx, const std::vector<float>& fitpars)
{
  Acts::Vector2 pca2d = TrackFitUtils::get_circle_point_pca(fitpars[0], fitpars[1], fitpars[2], std::move(event_vtx));
  Acts::Vector3 helix_vtx(pca2d(0), pca2d(1), fitpars[4]);

  return helix_vtx;
}

Acts::Vector3 HelicalFitter::get_line_vtx(Acts::Vector3 event_vtx, const std::vector<float>& fitpars)
{
  Acts::Vector2 pca2d = TrackFitUtils::get_line_point_pca(fitpars[0], fitpars[1], std::move(event_vtx));
  Acts::Vector3 line_vtx(pca2d(0), pca2d(1), fitpars[3]);

  return line_vtx;
}

std::pair<Acts::Vector3, Acts::Vector3> HelicalFitter::get_line_tangent(const std::vector<float>& fitpars, Acts::Vector3 global)
{
  // returns the equation of the line, with the direction obtained from the global cluster point

  // Get the rough direction of the line from the global vector, which is a point on the line
  float const phi = atan2(global(1), global(0));

  // we need the direction of the line
  // consider a point some distance along the straight line.
  // Consider a value of x, calculate y, calculate radius, calculate z
  double const x = 0;
  double const y = fitpars[0] * x + fitpars[1];
  //  double rxy = sqrt(x*x+y*y);
  double const z = fitpars[2] * x + fitpars[3];
  Acts::Vector3 arb_point(x, y, z);

  double const x2 = 1;
  double const y2 = fitpars[0] * x2 + fitpars[1];
  double const z2 = fitpars[2] * x2 + fitpars[3];
  Acts::Vector3 const arb_point2(x2, y2, z2);

  float const arb_phi = atan2(arb_point(1), arb_point(0));
  Acts::Vector3 tangent = arb_point2 - arb_point;  // direction of line
  if (fabs(arb_phi - phi) > M_PI / 2)
  {
    tangent = arb_point - arb_point2;  // direction of line
  }

  tangent /= tangent.norm();

  std::pair<Acts::Vector3, Acts::Vector3> line = std::make_pair(arb_point, tangent);

  return line;
}

std::pair<Acts::Vector3, Acts::Vector3> HelicalFitter::get_line_zero_field(const std::vector<float>& fitpars)
{
  // Returns the line equation, but without the direction of the track
  // consider a point some distance along the straight line.
  // Consider a value of x, calculate y, calculate z
  double const x = 0;
  double const y = fitpars[0] * x + fitpars[1];
  double const z = fitpars[2] * x + fitpars[3];
  Acts::Vector3 const arb_point(x, y, z);

  double const x2 = 1;
  double const y2 = fitpars[0] * x2 + fitpars[1];
  double const z2 = fitpars[2] * x2 + fitpars[3];
  Acts::Vector3 const arb_point2(x2, y2, z2);

  Acts::Vector3 tangent = arb_point2 - arb_point;  // +/- times direction of line
  tangent /= tangent.norm();

  std::pair<Acts::Vector3, Acts::Vector3> line = std::make_pair(arb_point, tangent);

  return line;
}

std::pair<Acts::Vector3, Acts::Vector3> HelicalFitter::get_line(const std::vector<float>& fitpars)
{
  // we need the direction of the line
  // consider a point some distance along the straight line.
  // Consider a value of x, calculate y, calculate radius, calculate z
  double const x = 0.0;
  double const y = fitpars[0] * x + fitpars[1];
  double const rxy = sqrt(x * x + y * y);
  double const z = fitpars[2] * rxy + fitpars[3];
  Acts::Vector3 const arb_point(x, y, z);

  double const x2 = 1;
  double const y2 = fitpars[0] * x2 + fitpars[1];
  double const rxy2 = sqrt(x2 * x2 + y2 * y2);
  double const z2 = fitpars[2] * rxy2 + fitpars[3];
  Acts::Vector3 const arb_point2(x2, y2, z2);

  Acts::Vector3 tangent = arb_point2 - arb_point;  // direction of line
  tangent /= tangent.norm();

  std::pair<Acts::Vector3, Acts::Vector3> line = std::make_pair(arb_point, tangent);

  return line;
}

Acts::Vector3 HelicalFitter::get_line_plane_intersection(const Acts::Vector3& PCA, const Acts::Vector3& tangent, const Acts::Vector3& sensor_center, const Acts::Vector3& sensor_normal)
{
  // get the intersection of the line made by PCA and tangent with the plane of the sensor

  // For a point on the line:      p = PCA + d * tangent;
  // for a point on the plane:  (p - sensor_center).sensor_normal = 0

  // The solution is:
  float const d = (sensor_center - PCA).dot(sensor_normal) / tangent.dot(sensor_normal);
  Acts::Vector3 intersection = PCA + d * tangent;

  /*
  std::cout << "    sensor center " << sensor_center(0) << "  " << sensor_center(1) << "  " << sensor_center(2) << std::endl;
  std::cout << "      intersection " << intersection(0) << "  " << intersection(1) << "  " << intersection(2) << std::endl;
  std::cout << "      PCA " << PCA(0) << "  " << PCA(1) << "  " << PCA(2) << std::endl;
  std::cout << "      tangent " << tangent(0) << "  " << tangent(1) << "  " << tangent(2) << std::endl;
  std::cout << "            d " << d << std::endl;
  */

  return intersection;
}

std::pair<Acts::Vector3, Acts::Vector3> HelicalFitter::get_helix_tangent(const std::vector<float>& fitpars, Acts::Vector3 global)
{
  auto pair = TrackFitUtils::get_helix_tangent(fitpars, global);
  /*
    save for posterity purposes
  if(Verbosity() > 2)
    {
      // different method for checking:
      // project the circle PCA vector an additional small amount and find the helix PCA to that point

      float projection = 0.25;  // cm
      Acts::Vector3 second_point = pca + projection * pca/pca.norm();
      Acts::Vector2 second_point_pca_circle = TrackFitUtils::get_circle_point_pca(radius, x0, y0, second_point);
      float second_point_pca_z = second_point_pca_circle.norm() * zslope + z0;
      Acts::Vector3 second_point_pca2(second_point_pca_circle(0), second_point_pca_circle(1), second_point_pca_z);
      Acts::Vector3 tangent2 = (second_point_pca2 - pca) /  (second_point_pca2 - pca).norm();
      Acts::Vector3 final_pca2 = getPCALinePoint(global, tangent2, pca);

      std::cout << " get_helix_tangent: getting tangent at angle_pca: " << angle_pca * 180.0 / M_PI << std::endl
                << " original first point pca                      " << pca(0) << "  " << pca(1) << "  " << pca(2) << std::endl
                << " original second point pca  " << second_point_pca(0) << "  " << second_point_pca(1) << "  " << second_point_pca(2) << std::endl
                << " original tangent " << tangent(0) << "  " << tangent(1) << "  " << tangent(2) << std::endl
                << " original final pca from line " << final_pca(0) << "  " << final_pca(1) << "  " << final_pca(2) << std::endl;

      if(Verbosity() > 3)
        {
          std::cout  << "    Check: 2nd point pca meth 2 "<< second_point_pca2(0)<< "  "<< second_point_pca2(1) << "  "<< second_point_pca2(2) << std::endl
                        << "    check tangent " << tangent2(0) << "  " << tangent2(1) << "  " << tangent2(2) << std::endl
                        << "    check final pca from line " << final_pca2(0) << "  " << final_pca2(1) << "  " << final_pca2(2)
                        << std::endl;
        }

    }
  */

  return pair;
}

int HelicalFitter::End(PHCompositeNode* /*unused*/)
{
  // closes output file in destructor
  delete _mille;

  if (make_ntuple)
  {
    fout->Write();
    fout->Close();
  }

  return Fun4AllReturnCodes::EVENT_OK;
}
int HelicalFitter::CreateNodes(PHCompositeNode* topNode)
{
  PHNodeIterator iter(topNode);

  PHCompositeNode* dstNode = dynamic_cast<PHCompositeNode*>(iter.findFirst("PHCompositeNode", "DST"));

  if (!dstNode)
  {
    std::cerr << "DST node is missing, quitting" << std::endl;
    throw std::runtime_error("Failed to find DST node in PHActsTrkFitter::createNodes");
  }

  PHNodeIterator dstIter(topNode);
  PHCompositeNode* svtxNode = dynamic_cast<PHCompositeNode*>(dstIter.findFirst("PHCompositeNode", "SVTX"));
  if (!svtxNode)
  {
    svtxNode = new PHCompositeNode("SVTX");
    dstNode->addNode(svtxNode);
  }

  m_trackmap = findNode::getClass<SvtxTrackMap>(topNode, "HelicalFitterTrackMap");
  if (!m_trackmap)
  {
    m_trackmap = new SvtxTrackMap_v2;
    PHIODataNode<PHObject>* node = new PHIODataNode<PHObject>(m_trackmap, "HelicalFitterTrackMap", "PHObject");
    svtxNode->addNode(node);
  }

  m_alignmentmap = findNode::getClass<SvtxAlignmentStateMap>(topNode, "HelicalFitterAlignmentStateMap");
  if (!m_alignmentmap)
  {
    m_alignmentmap = new SvtxAlignmentStateMap_v1;
    PHIODataNode<PHObject>* node = new PHIODataNode<PHObject>(m_alignmentmap, "HelicalFitterAlignmentStateMap", "PHObject");
    svtxNode->addNode(node);
  }

  return Fun4AllReturnCodes::EVENT_OK;
}
int HelicalFitter::GetNodes(PHCompositeNode* topNode)
{
  //---------------------------------
  // Get additional objects off the Node Tree
  //---------------------------------

  _track_map_silicon = findNode::getClass<TrackSeedContainer>(topNode, _silicon_track_map_name);
  if (!_track_map_silicon && (fitsilicon || fitfulltrack))
  {
    cerr << PHWHERE << " ERROR: Can't find SiliconTrackSeedContainer " << endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  _track_map_tpc = findNode::getClass<TrackSeedContainer>(topNode, _track_map_name);
  if (!_track_map_tpc && (fittpc || fitfulltrack))
  {
    cerr << PHWHERE << " ERROR: Can't find " << _track_map_name.c_str() << endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  _cluster_map = findNode::getClass<TrkrClusterContainer>(topNode, "TRKR_CLUSTER");
  if (!_cluster_map)
  {
    std::cout << PHWHERE << " ERROR: Can't find node TRKR_CLUSTER" << std::endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  _tGeometry = findNode::getClass<ActsGeometry>(topNode, "ActsGeometry");
  if (!_tGeometry)
  {
    std::cout << PHWHERE << "Error, can't find acts tracking geometry" << std::endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  m_vertexmap = findNode::getClass<SvtxVertexMap>(topNode, "SvtxVertexMap");
  if (!m_vertexmap)
  {
    std::cout << PHWHERE << " ERROR: Can't find node SvtxVertexMap" << std::endl;
    // return Fun4AllReturnCodes::ABORTEVENT;
  }

  // global position wrapper
  m_globalPositionWrapper.loadNodes(topNode);

  return Fun4AllReturnCodes::EVENT_OK;
}

Acts::Vector3 HelicalFitter::get_helix_pca(std::vector<float>& fitpars, const Acts::Vector3& global)
{
  return TrackFitUtils::get_helix_pca(fitpars, global);
}

Acts::Vector3 HelicalFitter::getPCALinePoint(const Acts::Vector3& global, const Acts::Vector3& tangent, const Acts::Vector3& posref)
{
  // Approximate track with a straight line consisting of the state position posref and the vector (px,py,pz)

  // The position of the closest point on the line to global is:
  // posref + projection of difference between the point and posref on the tangent vector
  Acts::Vector3 pca = posref + ((global - posref).dot(tangent)) * tangent;

  return pca;
}

float HelicalFitter::convertTimeToZ(TrkrDefs::cluskey cluster_key, TrkrCluster* cluster)
{
  // must convert local Y from cluster average time of arival to local cluster z position
  double const drift_velocity = _tGeometry->get_drift_velocity();
  double const zdriftlength = cluster->getLocalY() * drift_velocity;
  double const surfCenterZ = 52.89;          // 52.89 is where G4 thinks the surface center is
  double zloc = surfCenterZ - zdriftlength;  // converts z drift length to local z position in the TPC in north
  unsigned int const side = TpcDefs::getSide(cluster_key);
  if (side == 0)
  {
    zloc = -zloc;
  }
  float const z = zloc;  // in cm

  return z;
}

void HelicalFitter::makeTpcGlobalCorrections(TrkrDefs::cluskey cluster_key, short int crossing, Acts::Vector3& global)
{
  // make all corrections to global position of TPC cluster
  unsigned int const side = TpcDefs::getSide(cluster_key);
  global.z() = m_clusterCrossingCorrection.correctZ(global.z(), side, crossing);

  // apply distortion corrections
  global = m_globalPositionWrapper.applyDistortionCorrections(global);
}

void HelicalFitter::getTrackletClusters(TrackSeed* tracklet, std::vector<Acts::Vector3>& global_vec, std::vector<TrkrDefs::cluskey>& cluskey_vec)
{
  getTrackletClusterList(tracklet, cluskey_vec);
  // store cluster global positions in a vector
  TrackFitUtils::getTrackletClusters(_tGeometry, _cluster_map, global_vec, cluskey_vec);
}

void HelicalFitter::getTrackletClusterList(TrackSeed* tracklet, std::vector<TrkrDefs::cluskey>& cluskey_vec)
{
  for (auto clusIter = tracklet->begin_cluster_keys();
       clusIter != tracklet->end_cluster_keys();
       ++clusIter)
  {
    auto key = *clusIter;
    auto cluster = _cluster_map->findCluster(key);
    if (!cluster)
    {
      std::cout << PHWHERE << "Failed to get cluster with key " << key << std::endl;
      continue;
    }

    /// Make a safety check for clusters that couldn't be attached to a surface
    auto surf = _tGeometry->maps().getSurface(key, cluster);
    if (!surf)
    {
      continue;
    }

    // drop some bad layers in the TPC completely
    unsigned int const layer = TrkrDefs::getLayer(key);
    if (layer == 7 || layer == 22 || layer == 23 || layer == 38 || layer == 39)
    {
      continue;
    }

    // drop INTT clusters for now  -- TEMPORARY!
    // if (layer > 2 && layer < 7)
    //{
    //  continue;
    //}

    cluskey_vec.push_back(key);

  }  // end loop over clusters for this track
}

std::vector<float> HelicalFitter::fitClusters(std::vector<Acts::Vector3>& global_vec, std::vector<TrkrDefs::cluskey> cluskey_vec)
{
  return TrackFitUtils::fitClusters(global_vec, std::move(cluskey_vec), use_intt_zfit);  // do helical fit
}

Acts::Vector2 HelicalFitter::getClusterError(TrkrCluster* cluster, TrkrDefs::cluskey cluskey, Acts::Vector3& global)
{
  Acts::Vector2 clus_sigma(0, 0);

  double const clusRadius = sqrt(global[0] * global[0] + global[1] * global[1]);
  auto para_errors = _ClusErrPara.get_clusterv5_modified_error(cluster, clusRadius, cluskey);
  double const phierror = sqrt(para_errors.first);
  double const zerror = sqrt(para_errors.second);
  clus_sigma(1) = zerror;
  clus_sigma(0) = phierror;

  return clus_sigma;
}

// new one
void HelicalFitter::getLocalDerivativesXY(const Surface& surf, const Acts::Vector3& global, const std::vector<float>& fitpars, float lcl_derivativeX[5], float lcl_derivativeY[5], unsigned int layer)
{
  // Calculate the derivatives of the residual wrt the track parameters numerically
  std::vector<float> temp_fitpars;

  std::vector<float> fitpars_delta;
  fitpars_delta.push_back(0.1);  // radius, cm
  fitpars_delta.push_back(0.1);  // X0, cm
  fitpars_delta.push_back(0.1);  // Y0, cm
  fitpars_delta.push_back(0.1);  // zslope, cm
  fitpars_delta.push_back(0.1);  // Z0, cm

  temp_fitpars.reserve(fitpars.size());
  for (float const fitpar : fitpars)
  {
    temp_fitpars.push_back(fitpar);
  }

  // calculate projX and projY vectors once for the optimum fit parameters
  if (Verbosity() > 1)
  {
    std::cout << "Call get_helix_tangent for best fit fitpars" << std::endl;
  }
  std::pair<Acts::Vector3, Acts::Vector3> const tangent = get_helix_tangent(fitpars, global);

  Acts::Vector3 projX(0, 0, 0), projY(0, 0, 0);
  get_projectionXY(surf, tangent, projX, projY);

  Acts::Vector3 const intersection = get_helix_surface_intersection(surf, temp_fitpars, global);

  // loop over the track fit parameters
  for (unsigned int ip = 0; ip < fitpars.size(); ++ip)
  {
    Acts::Vector3 intersection_delta[2];
    for (int ipm = 0; ipm < 2; ++ipm)
    {
      temp_fitpars[ip] = fitpars[ip];  // reset to best fit value
      float const deltapm = pow(-1.0, ipm);
      temp_fitpars[ip] += deltapm * fitpars_delta[ip];

      Acts::Vector3 const temp_intersection = get_helix_surface_intersection(surf, temp_fitpars, global);
      intersection_delta[ipm] = temp_intersection - intersection;
    }
    Acts::Vector3 average_intersection_delta = (intersection_delta[0] - intersection_delta[1]) / (2 * fitpars_delta[ip]);

    if (Verbosity() > 1)
    {
      std::cout << " average_intersection_delta / delta " << average_intersection_delta(0) << "  " << average_intersection_delta(1) << "  " << average_intersection_delta(2) << std::endl;
    }

    // calculate the change in fit for X and Y
    // - note negative sign from ATLAS paper is dropped here because mille wants the derivative of the fit, not the derivative of the residual
    lcl_derivativeX[ip] = average_intersection_delta.dot(projX);
    lcl_derivativeY[ip] = average_intersection_delta.dot(projY);
    if (Verbosity() > 1)
    {
      std::cout << " layer " << layer << " ip " << ip << "  derivativeX " << lcl_derivativeX[ip] << "  "
                << " derivativeY " << lcl_derivativeY[ip] << std::endl;
    }

    temp_fitpars[ip] = fitpars[ip];
  }
}

void HelicalFitter::getLocalDerivativesZeroFieldXY(const Surface& surf, const Acts::Vector3& global, const std::vector<float>& fitpars, float lcl_derivativeX[5], float lcl_derivativeY[5], unsigned int layer)
{
  // Calculate the derivatives of the residual wrt the track parameters numerically
  // This version differs from the field on one in that:
  // Fitpars has parameters of a straight line (4) instead of a helix (5)
  // The track tangent is just the line direction

  std::vector<float> temp_fitpars;

  std::vector<float> fitpars_delta;
  fitpars_delta.push_back(0.1);  // xyslope, cm
  fitpars_delta.push_back(0.1);  // y0, cm
  fitpars_delta.push_back(0.1);  // zslope, cm
  fitpars_delta.push_back(0.1);  // Z0, cm

  temp_fitpars.reserve(fitpars.size());
  for (float const fitpar : fitpars)
  {
    temp_fitpars.push_back(fitpar);
  }

  // calculate projX and projY vectors once for the optimum fit parameters
  if (Verbosity() > 1)
  {
    std::cout << "Call get_line to get tangent for ZF fitpars" << std::endl;
  }

  std::pair<Acts::Vector3, Acts::Vector3> const tangent = get_line_tangent(fitpars, global);

  Acts::Vector3 projX(0, 0, 0), projY(0, 0, 0);
  get_projectionXY(surf, tangent, projX, projY);

  Acts::Vector3 const intersection = get_line_surface_intersection(surf, temp_fitpars);

  // loop over the track fit parameters
  for (unsigned int ip = 0; ip < fitpars.size(); ++ip)
  {
    Acts::Vector3 intersection_delta[2];
    for (int ipm = 0; ipm < 2; ++ipm)
    {
      temp_fitpars[ip] = fitpars[ip];  // reset to best fit value
      float const deltapm = pow(-1.0, ipm);
      temp_fitpars[ip] += deltapm * fitpars_delta[ip];

      Acts::Vector3 const temp_intersection = get_line_surface_intersection(surf, temp_fitpars);
      intersection_delta[ipm] = temp_intersection - intersection;
    }
    Acts::Vector3 average_intersection_delta = (intersection_delta[0] - intersection_delta[1]) / (2 * fitpars_delta[ip]);

    if (Verbosity() > 1)
    {
      std::cout << " average_intersection_delta / delta " << average_intersection_delta(0) << "  " << average_intersection_delta(1) << "  " << average_intersection_delta(2) << std::endl;
    }

    // calculate the change in fit for X and Y
    // - note negative sign from ATLAS paper is dropped here because mille wants the derivative of the fit, not the derivative of the residual
    lcl_derivativeX[ip] = average_intersection_delta.dot(projX);
    lcl_derivativeY[ip] = average_intersection_delta.dot(projY);
    if (Verbosity() > 1)
    {
      std::cout << " layer " << layer << " ip " << ip << "  derivativeX " << lcl_derivativeX[ip] << "  "
                << " derivativeY " << lcl_derivativeY[ip] << std::endl;
    }

    temp_fitpars[ip] = fitpars[ip];
  }
}

void HelicalFitter::getLocalVtxDerivativesXY(SvtxTrack& track, const Acts::Vector3& event_vtx, const std::vector<float>& fitpars, float lcl_derivativeX[5], float lcl_derivativeY[5])
{
  // Calculate the derivatives of the residual wrt the track parameters numerically
  std::vector<float> temp_fitpars;
  Acts::Vector3 const track_vtx(track.get_x(), track.get_y(), track.get_z());

  std::vector<float> fitpars_delta;
  fitpars_delta.push_back(0.1);  // radius, cm
  fitpars_delta.push_back(0.1);  // X0, cm
  fitpars_delta.push_back(0.1);  // Y0, cm
  fitpars_delta.push_back(0.1);  // zslope, cm
  fitpars_delta.push_back(0.1);  // Z0, cm

  temp_fitpars.reserve(fitpars.size());
  for (float const fitpar : fitpars)
  {
    temp_fitpars.push_back(fitpar);
  }

  // calculate projX and projY vectors once for the optimum fit parameters
  if (Verbosity() > 1)
  {
    std::cout << "Get tangent from track momentum vector" << std::endl;
  }

  Acts::Vector3 const perigeeNormal(track.get_px(), track.get_py(), track.get_pz());

  // loop over the track fit parameters
  for (unsigned int ip = 0; ip < fitpars.size(); ++ip)
  {
    Acts::Vector3 localPerturb[2];
    Acts::Vector3 paperPerturb[2];  // for local derivative calculation like from the paper

    for (int ipm = 0; ipm < 2; ++ipm)
    {
      temp_fitpars[ip] = fitpars[ip];  // reset to best fit value
      float const deltapm = pow(-1.0, ipm);
      temp_fitpars[ip] += deltapm * fitpars_delta[ip];

      Acts::Vector3 const temp_track_vtx = get_helix_vtx(event_vtx, temp_fitpars);  // temporary pca
      paperPerturb[ipm] = temp_track_vtx;                                           // for og local derivative calculation

      // old method is next two lines
      Acts::Vector3 const localtemp_track_vtx = globalvtxToLocalvtx(track, event_vtx, temp_track_vtx);
      localPerturb[ipm] = localtemp_track_vtx;

      if (Verbosity() > 1)
      {
        std::cout << "vtx local parameter " << ip << " with ipm " << ipm << " deltapm " << deltapm << " :" << std::endl;
        std::cout << " fitpars " << fitpars[ip] << " temp_fitpars " << temp_fitpars[ip] << std::endl;
        std::cout << " localtmp_track_vtx: " << localtemp_track_vtx << std::endl;
      }
    }

    Acts::Vector3 projX(0, 0, 0), projY(0, 0, 0);
    get_projectionVtxXY(track, event_vtx, projX, projY);

    Acts::Vector3 const average_vtxX = (paperPerturb[0] - paperPerturb[1]) / (2 * fitpars_delta[ip]);
    Acts::Vector3 const average_vtxY = (paperPerturb[0] - paperPerturb[1]) / (2 * fitpars_delta[ip]);

    // average_vtxX and average_vtxY are the numerical results in global coords for d(fit)/d(par)
    // The ATLAS paper formula is for the derivative of the residual, which is (measurement - fit = event vertex - track vertex)
    // Millepede wants the derivative of the fit, so we drop the minus sign from the paper
    lcl_derivativeX[ip] = average_vtxX.dot(projX);  //
    lcl_derivativeY[ip] = average_vtxY.dot(projY);

    temp_fitpars[ip] = fitpars[ip];
  }
}

void HelicalFitter::getLocalVtxDerivativesZeroFieldXY(SvtxTrack& track, const Acts::Vector3& event_vtx, const std::vector<float>& fitpars, float lcl_derivativeX[5], float lcl_derivativeY[5])
{
  // Calculate the derivatives of the residual wrt the track parameters numerically
  std::vector<float> temp_fitpars;
  Acts::Vector3 const track_vtx(track.get_x(), track.get_y(), track.get_z());

  std::vector<float> fitpars_delta;
  fitpars_delta.push_back(0.1);  // xyslope, cm
  fitpars_delta.push_back(0.1);  // y0, cm
  fitpars_delta.push_back(0.1);  // zslope, cm
  fitpars_delta.push_back(0.1);  // Z0, cm

  temp_fitpars.reserve(fitpars.size());
  for (float const fitpar : fitpars)
  {
    temp_fitpars.push_back(fitpar);
  }

  Acts::Vector3 const perigeeNormal(track.get_px(), track.get_py(), track.get_pz());

  // loop over the track fit parameters
  for (unsigned int ip = 0; ip < fitpars.size(); ++ip)
  {
    Acts::Vector3 localPerturb[2];
    Acts::Vector3 paperPerturb[2];  // for local derivative calculation like from the paper

    for (int ipm = 0; ipm < 2; ++ipm)
    {
      temp_fitpars[ip] = fitpars[ip];  // reset to best fit value
      float const deltapm = pow(-1.0, ipm);
      temp_fitpars[ip] += deltapm * fitpars_delta[ip];

      Acts::Vector3 const temp_track_vtx = get_line_vtx(event_vtx, temp_fitpars);  // temporary pca
      paperPerturb[ipm] = temp_track_vtx;                                          // for og local derivative calculation

      // old method is next two lines
      Acts::Vector3 const localtemp_track_vtx = globalvtxToLocalvtx(track, event_vtx, temp_track_vtx);
      localPerturb[ipm] = localtemp_track_vtx;

      if (Verbosity() > 1)
      {
        std::cout << "vtx local parameter " << ip << " with ipm " << ipm << " deltapm " << deltapm << " :" << std::endl;
        std::cout << " fitpars " << fitpars[ip] << " temp_fitpars " << temp_fitpars[ip] << std::endl;
        std::cout << " localtmp_track_vtx: " << localtemp_track_vtx << std::endl;
      }
    }

    // calculate projX and projY vectors once for the optimum fit parameters
    Acts::Vector3 projX(0, 0, 0), projY(0, 0, 0);
    get_projectionVtxXY(track, event_vtx, projX, projY);

    Acts::Vector3 const average_vtxX = (paperPerturb[0] - paperPerturb[1]) / (2 * fitpars_delta[ip]);
    Acts::Vector3 const average_vtxY = (paperPerturb[0] - paperPerturb[1]) / (2 * fitpars_delta[ip]);

    // average_vtxX and average_vtxY are the numerical results in global coords for d(fit)/d(par)
    // The ATLAS paper formula is for the derivative of the residual, which is (measurement - fit = event vertex - track vertex)
    // Millepede wants the derivative of the fit, so we drop the minus sign from the paper
    lcl_derivativeX[ip] = average_vtxX.dot(projX);  //
    lcl_derivativeY[ip] = average_vtxY.dot(projY);

    temp_fitpars[ip] = fitpars[ip];
  }
}

void HelicalFitter::getGlobalDerivativesXY(const Surface& surf, const Acts::Vector3& global, const Acts::Vector3& fitpoint, const std::vector<float>& fitpars, float glbl_derivativeX[6], float glbl_derivativeY[6], unsigned int layer)
{
  // calculate projX and projY vectors once for the optimum fit parameters
  std::pair<Acts::Vector3, Acts::Vector3> tangent;
  if (straight_line_fit)
  {
    tangent = get_line_tangent(fitpars, global);
  }
  else
  {
    tangent = get_helix_tangent(fitpars, global);
  }

  Acts::Vector3 projX(0, 0, 0), projY(0, 0, 0);
  get_projectionXY(surf, tangent, projX, projY);

  /*
  // translations in polar coordinates, unit vectors must be in polar coordinates
  //   -- There is an implicit assumption here that unitrphi is a length
  // unit vector in r is the normalized radial vector pointing to the surface center

  Acts::Vector3 center = surf->center(_tGeometry->geometry().getGeoContext()) / Acts::UnitConstants::cm;
  Acts::Vector3 unitr( center(0), center(1), 0.0);
  unitr /= unitr.norm();
  float phi = atan2(center(1), center(0));
  Acts::Vector3 unitrphi(-sin(phi), cos(phi), 0.0);  // unit vector phi in polar coordinates
  Acts::Vector3 unitz(0, 0, 1);

  glbl_derivativeX[3] = unitr.dot(projX);
  glbl_derivativeX[4] = unitrphi.dot(projX);
  glbl_derivativeX[5] = unitz.dot(projX);

  glbl_derivativeY[3] = unitr.dot(projY);
  glbl_derivativeY[4] = unitrphi.dot(projY);
  glbl_derivativeY[5] = unitz.dot(projY);
  */

  // translations in cartesian coordinates
  // Unit vectors in the global cartesian frame
  Acts::Vector3 const unitx(1, 0, 0);
  Acts::Vector3 const unity(0, 1, 0);
  Acts::Vector3 const unitz(0, 0, 1);

  glbl_derivativeX[3] = unitx.dot(projX);
  glbl_derivativeX[4] = unity.dot(projX);
  glbl_derivativeX[5] = unitz.dot(projX);

  glbl_derivativeY[3] = unitx.dot(projY);
  glbl_derivativeY[4] = unity.dot(projY);
  glbl_derivativeY[5] = unitz.dot(projY);

  /*
  // note: the global derivative sign should be reversed from the ATLAS paper
  // because mille wants the derivative of the fit, while the ATLAS paper gives the derivative of the residual.
  // But this sign reversal does NOT work.
  // Verified that not reversing the sign here produces the correct sign of the prediction of the residual..
  for(unsigned int i = 3; i < 6; ++i)
  {
  glbl_derivativeX[i] *= -1.0;
  glbl_derivativeY[i] *= -1.0;
  }
  */
  // rotations
  // need center of sensor to intersection point
  Acts::Vector3 const sensorCenter = surf->center(_tGeometry->geometry().getGeoContext()) / Acts::UnitConstants::cm;  // convert to cm
  Acts::Vector3 const OM = fitpoint - sensorCenter;                                                                   // this effectively reverses the sign from the ATLAS paper

  /*
  glbl_derivativeX[0] = (unitr.cross(OM)).dot(projX);
  glbl_derivativeX[1] = (unitrphi.cross(OM)).dot(projX);
  glbl_derivativeX[2] = (unitz.cross(OM)).dot(projX);

  glbl_derivativeY[0] = (unitr.cross(OM)).dot(projY);
  glbl_derivativeY[1] = (unitrphi.cross(OM)).dot(projY);
  glbl_derivativeY[2] = (unitz.cross(OM)).dot(projY);
  */

  glbl_derivativeX[0] = (unitx.cross(OM)).dot(projX);
  glbl_derivativeX[1] = (unity.cross(OM)).dot(projX);
  glbl_derivativeX[2] = (unitz.cross(OM)).dot(projX);

  glbl_derivativeY[0] = (unitx.cross(OM)).dot(projY);
  glbl_derivativeY[1] = (unity.cross(OM)).dot(projY);
  glbl_derivativeY[2] = (unitz.cross(OM)).dot(projY);

  if (Verbosity() > 1)
  {
    for (int ip = 0; ip < 6; ++ip)
    {
      std::cout << " layer " << layer << " ip " << ip
                << "  glbl_derivativeX " << glbl_derivativeX[ip] << "  "
                << " glbl_derivativeY " << glbl_derivativeY[ip] << std::endl;
    }
  }

  /*
  if(Verbosity() > 2)
    {
      std::cout << "    unitr mag: " << unitr.norm() << " unitr: " << std::endl << unitr << std::endl;
      std::cout << "    unitrphi mag: " << unitrphi.norm() << " unitrphi: " << std::endl << unitrphi << std::endl;
      std::cout << "    unitz mag: " << unitz.norm() << " unitz: " << std::endl << unitz << std::endl;
      std::cout << "    projX: " << std::endl << projX << std::endl;
      std::cout << "    projY: " << std::endl << projY << std::endl;
    }
  */
}

void HelicalFitter::getGlobalVtxDerivativesXY(SvtxTrack& track, const Acts::Vector3& event_vtx, float glbl_derivativeX[3], float glbl_derivativeY[3])
{
  Acts::Vector3 const unitx(1, 0, 0);
  Acts::Vector3 const unity(0, 1, 0);
  Acts::Vector3 const unitz(0, 0, 1);

  Acts::Vector3 const track_vtx(track.get_x(), track.get_y(), track.get_z());
  Acts::Vector3 const mom(track.get_px(), track.get_py(), track.get_pz());

  // calculate projX and projY vectors once for the optimum fit parameters
  Acts::Vector3 projX(0, 0, 0), projY(0, 0, 0);
  get_projectionVtxXY(track, event_vtx, projX, projY);

  // translations
  glbl_derivativeX[0] = unitx.dot(projX);
  glbl_derivativeX[1] = unity.dot(projX);
  glbl_derivativeX[2] = unitz.dot(projX);
  glbl_derivativeY[0] = unitx.dot(projY);
  glbl_derivativeY[1] = unity.dot(projY);
  glbl_derivativeY[2] = unitz.dot(projY);

  // The derivation in the ATLAS paper used above gives the derivative of the residual (= measurement - fit)
  // pede wants the derivative of the fit, so we reverse that - valid if our residual is (event vertex - track vertex)

  // Verified that reversing these signs produces the correct sign and magnitude for the prediction of the residual.
  // tested this by offsetting the simulated event vertex with zero misalignments. Pede fit reproduced simulated (xvtx, yvtx) within 7%.
  //   -- test gave zero for zvtx param, since this is determined relative to the measured event z vertex.
  for (int i = 0; i < 3; ++i)
  {
    glbl_derivativeX[i] *= -1.0;
    glbl_derivativeY[i] *= -1.0;
  }
}

void HelicalFitter::get_projectionXY(const Surface& surf, const std::pair<Acts::Vector3, Acts::Vector3>& tangent, Acts::Vector3& projX, Acts::Vector3& projY)
{
  // we only need the direction part of the tangent
  Acts::Vector3 const tanvec = tangent.second;
  // get the plane of the surface
  Acts::Vector3 const sensorCenter = surf->center(_tGeometry->geometry().getGeoContext()) / Acts::UnitConstants::cm;  // convert to cm

  // We need the three unit vectors in the sensor local frame, transformed to the global frame
  //====================================================================
  // sensorNormal is the Z vector in the global frame
  Acts::Vector3 const Z = -surf->normal(_tGeometry->geometry().getGeoContext(), Acts::Vector3(1, 1, 1), Acts::Vector3(1, 1, 1));
  // get surface X and Y unit vectors in global frame
  // transform Xlocal = 1.0 to global, subtract the surface center, normalize to 1
  Acts::Vector3 const xloc(1.0, 0.0, 0.0);  // local coord unit vector in x
  Acts::Vector3 xglob = surf->transform(_tGeometry->geometry().getGeoContext()) * (xloc * Acts::UnitConstants::cm);
  xglob /= Acts::UnitConstants::cm;
  Acts::Vector3 const yloc(0.0, 1.0, 0.0);
  Acts::Vector3 yglob = surf->transform(_tGeometry->geometry().getGeoContext()) * (yloc * Acts::UnitConstants::cm);
  yglob /= Acts::UnitConstants::cm;
  // These are the local frame unit vectors transformed to the global frame
  Acts::Vector3 const X = (xglob - sensorCenter) / (xglob - sensorCenter).norm();
  Acts::Vector3 const Y = (yglob - sensorCenter) / (yglob - sensorCenter).norm();

  // see equation 31 of the ATLAS paper (and discussion) for this
  // The unit vector in the local frame transformed to the global frame (X),
  // minus Z scaled by the track vector overlap with X, divided by the track vector overlap with Z.
  projX = X - (tanvec.dot(X) / tanvec.dot(Z)) * Z;
  projY = Y - (tanvec.dot(Y) / tanvec.dot(Z)) * Z;

  if(Verbosity() > 1)
    {
      std::cout << "    tanvec: " << std::endl << tanvec << std::endl;
      std::cout << "    X: " << std::endl << X << std::endl;
      std::cout << "    Y: " << std::endl << Y << std::endl;
      std::cout << "    Z: " << std::endl << Z << std::endl;

      std::cout << "    projX: " << std::endl << projX << std::endl;
      std::cout << "    projY: " << std::endl << projY << std::endl;
    }

  return;
}

void HelicalFitter::get_projectionVtxXY(SvtxTrack& track, const Acts::Vector3& event_vtx, Acts::Vector3& projX, Acts::Vector3& projY)
{
  Acts::Vector3 tanvec(track.get_px(), track.get_py(), track.get_pz());
  Acts::Vector3 normal(track.get_px(), track.get_py(), 0);

  tanvec /= tanvec.norm();
  normal /= normal.norm();

  // get surface X and Y unit vectors in global frame
  Acts::Vector3 const xloc(1.0, 0.0, 0.0);
  Acts::Vector3 const yloc(0.0, 0.0, 1.0);  // local y
  Acts::Vector3 const xglob = localvtxToGlobalvtx(track, event_vtx, xloc);
  Acts::Vector3 const yglob = yloc + event_vtx;
  Acts::Vector3 const X = (xglob - event_vtx) / (xglob - event_vtx).norm();  // local unit vector transformed to global coordinates
  Acts::Vector3 const Y = (yglob - event_vtx) / (yglob - event_vtx).norm();

  // see equation 31 of the ATLAS paper (and discussion) for this
  projX = X - (tanvec.dot(X) / tanvec.dot(normal)) * normal;
  projY = Y - (tanvec.dot(Y) / tanvec.dot(normal)) * normal;
  return;
}

unsigned int HelicalFitter::addSiliconClusters(std::vector<float>& fitpars, std::vector<Acts::Vector3>& global_vec, std::vector<TrkrDefs::cluskey>& cluskey_vec)
{
  return TrackFitUtils::addClusters(fitpars, dca_cut, _tGeometry, _cluster_map, global_vec, cluskey_vec, 0, 6);
}
bool HelicalFitter::is_vertex_param_fixed(unsigned int param)
{
  bool ret = false;
  auto it = fixed_vertex_params.find(param);
  if (it != fixed_vertex_params.end())
  {
    ret = true;
  }
  return ret;
}
bool HelicalFitter::is_intt_layer_fixed(unsigned int layer)
{
  bool ret = false;
  auto it = fixed_intt_layers.find(layer);
  if (it != fixed_intt_layers.end())
  {
    ret = true;
  }

  return ret;
}

bool HelicalFitter::is_mvtx_layer_fixed(unsigned int layer, unsigned int clamshell)
{
  bool ret = false;

  std::pair const pair = std::make_pair(layer, clamshell);
  auto it = fixed_mvtx_layers.find(pair);
  if (it != fixed_mvtx_layers.end())
  {
    ret = true;
  }
  return ret;
}

void HelicalFitter::set_intt_layer_fixed(unsigned int layer)
{
  fixed_intt_layers.insert(layer);
}

void HelicalFitter::set_mvtx_layer_fixed(unsigned int layer, unsigned int clamshell)
{
  fixed_mvtx_layers.insert(std::make_pair(layer, clamshell));
}

bool HelicalFitter::is_layer_param_fixed(unsigned int layer, unsigned int param)
{
  bool ret = false;
  std::pair<unsigned int, unsigned int> const pair = std::make_pair(layer, param);
  auto it = fixed_layer_params.find(pair);
  if (it != fixed_layer_params.end())
  {
    ret = true;
  }

  return ret;
}

void HelicalFitter::set_layer_param_fixed(unsigned int layer, unsigned int param)
{
  std::pair<unsigned int, unsigned int> const pair = std::make_pair(layer, param);
  fixed_layer_params.insert(pair);
}

void HelicalFitter::set_tpc_sector_fixed(unsigned int region, unsigned int sector, unsigned int side)
{
  // make a combined subsector index
  unsigned int const subsector = region * 24 + side * 12 + sector;
  fixed_sectors.insert(subsector);
}

bool HelicalFitter::is_tpc_sector_fixed(unsigned int layer, unsigned int sector, unsigned int side)
{
  bool ret = false;
  unsigned int const region = AlignmentDefs::getTpcRegion(layer);
  unsigned int const subsector = region * 24 + side * 12 + sector;
  auto it = fixed_sectors.find(subsector);
  if (it != fixed_sectors.end())
  {
    ret = true;
  }

  return ret;
}

void HelicalFitter::correctTpcGlobalPositions(std::vector<Acts::Vector3> global_vec, const std::vector<TrkrDefs::cluskey> &cluskey_vec)
{
  for (unsigned int iclus = 0; iclus < cluskey_vec.size(); ++iclus)
  {
    auto cluskey = cluskey_vec[iclus];
    auto global = global_vec[iclus];
    const unsigned int trkrId = TrkrDefs::getTrkrId(cluskey);
    if (trkrId == TrkrDefs::tpcId)
    {
      // have to add corrections for TPC clusters after transformation to global
      int const crossing = 0;  // for now
      makeTpcGlobalCorrections(cluskey, crossing, global);
      global_vec[iclus] = global;
    }
  }
}

float HelicalFitter::getVertexResidual(Acts::Vector3 vtx)
{
  float const phi = atan2(vtx(1), vtx(0));
  float const r = vtx(0) / cos(phi);
  float const test_r = sqrt(vtx(0) * vtx(0) + vtx(1) * vtx(1));

  if (Verbosity() > 1)
  {
    std::cout << "my method position: " << vtx << std::endl;
    std::cout << "r " << r << " phi: " << phi * 180 / M_PI << " test_r" << test_r << std::endl;
  }
  return r;
}

void HelicalFitter::get_dca(SvtxTrack& track, float& dca3dxy, float& dca3dz, float& dca3dxysigma, float& dca3dzsigma, const Acts::Vector3& event_vertex)
{
  // give trackseed
  dca3dxy = NAN;
  Acts::Vector3 track_vtx(track.get_x(), track.get_y(), track.get_z());
  Acts::Vector3 const mom(track.get_px(), track.get_py(), track.get_pz());

  track_vtx -= event_vertex;  // difference between track_vertex and event_vtx

  Acts::ActsSquareMatrix<3> posCov;
  for (int i = 0; i < 3; ++i)
  {
    for (int j = 0; j < 3; ++j)
    {
      posCov(i, j) = track.get_error(i, j);
    }
  }

  Acts::Vector3 r = mom.cross(Acts::Vector3(0., 0., 1.));

  float phi = atan2(r(1), r(0));
  Acts::RotationMatrix3 rot;
  Acts::RotationMatrix3 rot_T;
  phi *= -1;
  rot(0, 0) = cos(phi);
  rot(0, 1) = -sin(phi);
  rot(0, 2) = 0;
  rot(1, 0) = sin(phi);
  rot(1, 1) = cos(phi);
  rot(1, 2) = 0;
  rot(2, 0) = 0;
  rot(2, 1) = 0;
  rot(2, 2) = 1;
  rot_T = rot.transpose();

  Acts::Vector3 pos_R = rot * track_vtx;
  Acts::ActsSquareMatrix<3> rotCov = rot * posCov * rot_T;
  dca3dxy = pos_R(0);
  dca3dz = pos_R(2);
  dca3dxysigma = sqrt(rotCov(0, 0));
  dca3dzsigma = sqrt(rotCov(2, 2));

  if (Verbosity() > 1)
  {
    std::cout << " Helix: momentum.cross(z): " << r << " phi: " << phi * 180 / M_PI << std::endl;
    std::cout << "dca3dxy " << dca3dxy << " dca3dz: " << dca3dz << " pos_R(1): " << pos_R(1) << " dca3dxysigma " << dca3dxysigma << " dca3dzsigma " << dca3dzsigma << std::endl;
  }
}

void HelicalFitter::get_dca_zero_field(SvtxTrack& track, float& dca3dxy, float& dca3dz, float& dca3dxysigma, float& dca3dzsigma, const Acts::Vector3& event_vertex)
{
  dca3dxy = NAN;
  // This is (pca2d(0), pca2d(1), Z0)
  Acts::Vector3 track_vtx(track.get_x(), track.get_y(), track.get_z());
  track_vtx -= event_vertex;  // difference between track_vertex and event_vtx

  // get unit direction vector for zero field track
  // for zero field case this is the tangent unit vector
  Acts::Vector3 const mom(track.get_px(), track.get_py(), track.get_pz());

  Acts::ActsSquareMatrix<3> posCov;
  for (int i = 0; i < 3; ++i)
  {
    for (int j = 0; j < 3; ++j)
    {
      posCov(i, j) = track.get_error(i, j);
    }
  }

  Acts::Vector3 r = mom.cross(Acts::Vector3(0., 0., 1.));

  float phi = atan2(r(1), r(0));
  Acts::RotationMatrix3 rot;
  Acts::RotationMatrix3 rot_T;
  phi *= -1;
  rot(0, 0) = cos(phi);
  rot(0, 1) = -sin(phi);
  rot(0, 2) = 0;
  rot(1, 0) = sin(phi);
  rot(1, 1) = cos(phi);
  rot(1, 2) = 0;
  rot(2, 0) = 0;
  rot(2, 1) = 0;
  rot(2, 2) = 1;
  rot_T = rot.transpose();

  Acts::Vector3 pos_R = rot * track_vtx;
  Acts::ActsSquareMatrix<3> rotCov = rot * posCov * rot_T;
  dca3dxy = pos_R(0);
  dca3dz = pos_R(2);
  dca3dxysigma = sqrt(rotCov(0, 0));
  dca3dzsigma = sqrt(rotCov(2, 2));

  if (Verbosity() > 1)
  {
    std::cout << " Zero Field: momentum.cross(z): " << r << " phi (deg): " << phi * 180 / M_PI << std::endl;
    std::cout << "dca3dxy " << dca3dxy << " dca3dz: " << dca3dz << " pos_R(1): " << pos_R(1) << " dca3dxysigma " << dca3dxysigma << " dca3dzsigma " << dca3dzsigma << std::endl;
  }
}

Acts::Vector3 HelicalFitter::globalvtxToLocalvtx(SvtxTrack& track, const Acts::Vector3& event_vertex)
{
  Acts::Vector3 track_vtx(track.get_x(), track.get_y(), track.get_z());
  Acts::Vector3 const mom(track.get_px(), track.get_py(), track.get_pz());
  track_vtx -= event_vertex;  // difference between track_vertex and event_vtx

  Acts::Vector3 r = mom.cross(Acts::Vector3(0., 0., 1.));
  float phi = atan2(r(1), r(0));
  Acts::RotationMatrix3 rot;
  Acts::RotationMatrix3 rot_T;
  phi *= -1;
  rot(0, 0) = cos(phi);
  rot(0, 1) = -sin(phi);
  rot(0, 2) = 0;
  rot(1, 0) = sin(phi);
  rot(1, 1) = cos(phi);
  rot(1, 2) = 0;
  rot(2, 0) = 0;
  rot(2, 1) = 0;
  rot(2, 2) = 1;
  rot_T = rot.transpose();

  Acts::Vector3 pos_R = rot * track_vtx;

  if (Verbosity() > 1)
  {
    std::cout << " momentum X z: " << r << " phi: " << phi * 180 / M_PI << std::endl;
    std::cout << " pos_R(0): " << pos_R(0) << " pos_R(1): " << pos_R(1) << std::endl;
  }
  return pos_R;
}

Acts::Vector3 HelicalFitter::globalvtxToLocalvtx(SvtxTrack& track, const Acts::Vector3& event_vertex, Acts::Vector3 PCA)
{
  Acts::Vector3 const mom(track.get_px(), track.get_py(), track.get_pz());
  PCA -= event_vertex;  // difference between track_vertex and event_vtx

  Acts::Vector3 r = mom.cross(Acts::Vector3(0., 0., 1.));
  float phi = atan2(r(1), r(0));
  Acts::RotationMatrix3 rot;
  Acts::RotationMatrix3 rot_T;
  phi *= -1;
  rot(0, 0) = cos(phi);
  rot(0, 1) = -sin(phi);
  rot(0, 2) = 0;
  rot(1, 0) = sin(phi);
  rot(1, 1) = cos(phi);
  rot(1, 2) = 0;
  rot(2, 0) = 0;
  rot(2, 1) = 0;
  rot(2, 2) = 1;
  rot_T = rot.transpose();

  Acts::Vector3 pos_R = rot * PCA;

  if (Verbosity() > 1)
  {
    std::cout << " momentum X z: " << r << " phi: " << phi * 180 / M_PI << std::endl;
    std::cout << " pos_R(0): " << pos_R(0) << " pos_R(1): " << pos_R(1) << std::endl;
  }
  return pos_R;
}

Acts::Vector3 HelicalFitter::localvtxToGlobalvtx(SvtxTrack& track, const Acts::Vector3& event_vtx, const Acts::Vector3& local)
{
  // Acts::Vector3 track_vtx = local;
  Acts::Vector3 const mom(track.get_px(), track.get_py(), track.get_pz());
  // std::cout << "first pos: " << pos << " mom: " << mom << std::endl;
  // local -= event_vertex; // difference between track_vertex and event_vtx

  Acts::Vector3 r = mom.cross(Acts::Vector3(0., 0., 1.));
  float const phi = atan2(r(1), r(0));
  Acts::RotationMatrix3 rot;
  Acts::RotationMatrix3 rot_T;
  // phi *= -1;
  rot(0, 0) = cos(phi);
  rot(0, 1) = -sin(phi);
  rot(0, 2) = 0;
  rot(1, 0) = sin(phi);
  rot(1, 1) = cos(phi);
  rot(1, 2) = 0;
  rot(2, 0) = 0;
  rot(2, 1) = 0;
  rot(2, 2) = 1;

  rot_T = rot.transpose();

  Acts::Vector3 pos_R = rot * local;
  pos_R += event_vtx;
  if (Verbosity() > 1)
  {
    std::cout << " momentum X z: " << r << " phi: " << phi * 180 / M_PI << std::endl;
    std::cout << " pos_R(0): " << pos_R(0) << " pos_R(1): " << pos_R(1) << "pos_R(2): " << pos_R(2) << std::endl;
  }
  return pos_R;
}
