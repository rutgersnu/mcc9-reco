#include "lardataobj/RecoBase/Track.h"
#include "lardataobj/RecoBase/MCSFitResult.h"
#include "lardataobj/RecoBase/TrajectoryPointFlags.h"
#include "lardataobj/RecoBase/TrackingPlane.h"
#include "lardata/RecoObjects/TrackingPlaneHelper.h"
#include "lardata/RecoObjects/TrackState.h"
#include "lardata/RecoObjects/TrackStatePropagator.h"
#include "NuCCanalyzer.h"

void NuCCanalyzer::endSubRun(const art::SubRun &subrun)
{
  if (!m_isData)
  {
    art::Handle<sumdata::POTSummary> potSummaryHandle;
    m_pot = subrun.getByLabel("generator", potSummaryHandle) ? static_cast<float>(potSummaryHandle->totpot) : 0.f;
    std::cout << "[NuCCanalyzer::endSubRun] Storing POT info!" << std::endl;
  }

  m_run = subrun.run();
  m_subrun = subrun.subRun();
  fSubrunTree->Fill();
}

void NuCCanalyzer::analyze(art::Event const &evt)
{   

  clearEvent();
  fRun = evt.run();
  fSubrun = evt.subRun();
  fEvent = evt.id().event();
  art::Timestamp evtTime = evt.time();
  fTimeHigh = evtTime.timeHigh();
  fTimeLow = evtTime.timeLow();
  std::cout << "[NuCCanalyzer::analyze]: Run " << fRun << ", Subrun " << fSubrun << ", Event " << fEvent << std::endl;

  // Event weight:
  if (!m_isData)
  {
    art::InputTag eventweight_tag(m_weight_producer);
    art::Handle<std::vector<evwgh::MCEventWeight>> eventweights_handle;
    if (evt.getByLabel(eventweight_tag, eventweights_handle))
    {
      std::vector<art::Ptr<evwgh::MCEventWeight>> eventweights;
      art::fill_ptr_vector(eventweights, eventweights_handle);
      std::map<std::string, std::vector<double>> evtwgt_map = eventweights.at(0)->fWeight;

      double splineWeight = evtwgt_map.at("splines_general_Spline").front();
      double rootinoWeight = evtwgt_map.at("RootinoFix_UBGenie").front();
      double cvWeight = evtwgt_map.at("TunedCentralValue_UBGenie").front();
      fEventWeight = splineWeight * rootinoWeight * cvWeight;

      std::cout << "[NuCCanalyzer::analyze]: Event Weight:  " << fEventWeight << std::endl;
    }
    else
    {
      std::cout << "[NuCCanalyzer::analyze]: Failed obtaining eventweight" << std::endl;
    }
  }

  larpandora.CollectPFParticleMetadata(evt, m_pfp_producer, pfparticles, particlesToMetadata);
  larpandora.BuildPFParticleMap(pfparticles, particleMap);

  if (!m_isData)
  {
    FillTrueNu(evt);
  }

  if (pfparticles.size() == 0)
    std::cout << "[NuCCanalyzer::FillReconstructed] No reconstructed PFParticles in event." << std::endl;
  else
  {
    larpandora.SelectNeutrinoPFParticles(pfparticles, pfneutrinos);
    if (pfneutrinos.size() != 1)
      std::cout << "[NuCCanalyzer::FillReconstructed] Number of reconstructed neutrinos in event is " << pfneutrinos.size() << std::endl;
    else // We have a reconstructed neutrino
    {
      if (!m_isData)
      {
        FillReconTruthMatching(evt);
        FillTrueNuDaughters(evt);
      }
      FillReconstructed(evt);
      // After all the fields are filled, do the selection.
      fIsNuMuCC = IsNuMuCC(evt);
    }
  }
  fEventTree->Fill();
  std::cout << "\n\n";
}

void NuCCanalyzer::FillReconstructed(art::Event const &evt)
{
  fNumPfp = pfparticles.size();
  // Load associations and collections
  lar_pandora::VertexVector vertexVector_dummy;
  lar_pandora::PFParticleVector particleVector_dummy;
  lar_pandora::SpacePointVector spacePointVector_dummy;
  larpandora.CollectVertices(evt, m_pfp_producer, vertexVector_dummy, particlesToVertices);
  larpandora.CollectPFParticles(evt, m_pfp_producer, particleVector_dummy, particlesToClusters);
  larpandora.CollectPFParticles(evt, m_pfp_producer, particleVector_dummy, particlesToSpacePoints);
  larpandora.CollectShowers(evt, m_pfp_producer, pfshowers, particlesToShowers);
  larpandora.CollectTracks(evt, m_pfp_producer, pftracks, particlesToTracks);
  lar_pandora::ClusterVector clusterVector_dummy;
  larpandora.CollectClusters(evt, m_pfp_producer, clusterVector_dummy, clustersToHits);
  //larpandora.CollectSpacePoints(evt, m_pfp_producer, spacePointVector_dummy, spacePointsToHits, hitsToSpacePoints);
  art::ValidHandle<std::vector<recob::Track>> trackHandle = evt.getValidHandle<std::vector<recob::Track>>(m_pfp_producer);
  const art::ValidHandle<std::vector<recob::MCSFitResult>> &MCSMu_handle = evt.getValidHandle<std::vector<recob::MCSFitResult>>("pandoraMCSMu");
  const art::FindManyP<anab::ParticleID> trackPIDAssn(trackHandle, evt, "pandoracalipidSCE");
  art::Handle<std::vector<recob::PFParticle>> pfparticles_handle;
  evt.getByLabel(m_pfp_producer, pfparticles_handle);
  art::FindManyP<anab::T0> nuFlashScoreAsso(pfparticles_handle, evt, "flashmatch");

  art::ValidHandle<std::vector<recob::Track> > track1handle = evt.getValidHandle<std::vector<recob::Track> >(m_splittrack1_producer);
  art::ValidHandle<std::vector<recob::Track> > track2handle = evt.getValidHandle<std::vector<recob::Track> >(m_splittrack2_producer);
  
  if (!trackPIDAssn.isValid())
  {
    std::cout << "[NuCCanalyzer::FillReconstructed] trackPIDAssn.isValid() == false" << std::endl;
  }

  // Start filling information
  art::Ptr<recob::PFParticle> pfnu = pfneutrinos.front();

  // added

art::ValidHandle<std::vector<recob::PFParticle> > inputPFParticle = evt.getValidHandle<std::vector<recob::PFParticle> >(inputPFLabel);

    auto assocVertex1 = std::unique_ptr<art::FindManyP<recob::Vertex> >(new art::FindManyP<recob::Vertex>(inputPFParticle, evt, inputVertexLabel1));
    auto assocVertex2 = std::unique_ptr<art::FindManyP<recob::Vertex> >(new art::FindManyP<recob::Vertex>(inputPFParticle, evt, inputVertexLabel2));

        const std::vector<art::Ptr<recob::Vertex> >& VertexVec1 = assocVertex1->at(pfnu.key());
        const std::vector<art::Ptr<recob::Vertex> >& VertexVec2 = assocVertex2->at(pfnu.key());


        if (VertexVec1.size()==1 || VertexVec2.size()==1){ 
        auto vtx1 = VertexVec1[0];
        auto vtx2 = VertexVec2[0];

       fv1pos = { float(vtx1->position().X()), float(vtx1->position().Y()), float(vtx1->position().Z()) };
       fv2pos = { float(vtx2->position().X()), float(vtx2->position().Y()), float(vtx2->position().Z()) };
       fv1xpos = float(vtx1->position().X());
       fv2xpos = float(vtx2->position().X());
       fv1ypos = float(vtx1->position().Y());
       fv2ypos = float(vtx2->position().Y());
       fv1zpos = float(vtx1->position().Z());
       fv2zpos = float(vtx2->position().Z());
}


//NEED TO REVIEW HERE
    art::InputTag TrackInputTag(inputTracksLabel);
    art::InputTag TrackInputTag1st(inputTracksLabel1st);
    art::ValidHandle<std::vector<recob::Track> > Tracks1st = evt.getValidHandle<std::vector<recob::Track> >(TrackInputTag1st);
    art::InputTag TrackInputTag2nd(inputTracksLabel2nd);
    art::ValidHandle<std::vector<recob::Track> > Tracks2nd = evt.getValidHandle<std::vector<recob::Track> >(TrackInputTag2nd);
    auto assocTracks = std::unique_ptr<art::FindManyP<recob::Track> >(new art::FindManyP<recob::Track>(inputPFParticle, evt, TrackInputTag));
  
    	if (isFromNeutrino(inputPFParticle,pfnu.key())==true){
   
    	const std::vector<art::Ptr<recob::Track> >& Tracks = assocTracks->at(pfnu.key());
    		for (unsigned int iTrack = 0; iTrack < Tracks.size(); ++iTrack) {
    		       	art::Ptr<recob::Track> ptrack = Tracks[iTrack];
                        const recob::Track* ptrack1 = 0;
                        const recob::Track* ptrack2 = 0;
              		auto id = ptrack->ID();
                        for (unsigned int iTrack1 = 0; iTrack1 < Tracks1st->size(); ++iTrack1) {
				art::Ptr<recob::Track> ptrack1tmp(Tracks1st, iTrack1);
				// std::cout << "ptrack1tmp->ID()=" << ptrack1tmp->ID() << " id=" << id << std::endl;
				if (ptrack1tmp->ID()!=id) continue;
				if (ptrack1tmp->CountValidPoints()<3) continue;
				ptrack1 = ptrack1tmp.get();
				break; 
                        } 
			for (unsigned int iTrack2 = 0; iTrack2 < Tracks2nd->size(); ++iTrack2) {
				art::Ptr<recob::Track> ptrack2tmp(Tracks2nd, iTrack2);
				// std::cout << "ptrack2tmp->ID()=" << ptrack2tmp->ID() << " id=" << id << std::endl;
				if (ptrack2tmp->ID()!=id) continue;
				if (ptrack2tmp->CountValidPoints()<3) continue;
				ptrack2 = ptrack2tmp.get();
                                break;
                        }
    			if (ptrack1!=0 || ptrack2!=0){
    			ftk_nhits   = ptrack->NumberTrajectoryPoints();
    			ftk1_nhits   = ptrack1->NumberTrajectoryPoints();
    			ftk2_nhits   = ptrack2->NumberTrajectoryPoints();
                        }
		}
	}

 // added 

  fNu_PDG = pfnu->PdgCode();
  fNumPrimaryDaughters = pfnu->NumDaughters();

  const std::vector<art::Ptr<anab::T0>> T0_flashchi_v = nuFlashScoreAsso.at(pfnu.key());
  if (T0_flashchi_v.size() == 1)
  {
    fNu_FlashChi2 = T0_flashchi_v.at(0)->TriggerConfidence();
    std::cout << "[NuCCanalyzer::FillReconstructed] fNu_FlashChi2: " << fNu_FlashChi2 << std::endl;
  }

  lar_pandora::MetadataVector neutrino_metadata_vec = particlesToMetadata.at(pfnu);
  lar_pandora::VertexVector neutrino_vertex_vec = particlesToVertices.at(pfnu);
  if (neutrino_metadata_vec.size() != 1 || neutrino_vertex_vec.size() != 1)
  {
    std::cout << "[NuCCanalyzer::FillReconstructed] Neutrino association problem." << std::endl;
  }
  else
  {
    const larpandoraobj::PFParticleMetadata::PropertiesMap &neutrino_properties = neutrino_metadata_vec.front()->GetPropertiesMap();
    fNu_Score = neutrino_properties.at("NuScore");
    fNu_SliceIndex = neutrino_properties.at("SliceIndex");
    const recob::Vertex::Point_t &neutrino_vtx = neutrino_vertex_vec.front()->position();
    fNu_Vx = neutrino_vtx.X();
    fNu_Vy = neutrino_vtx.Y();
    fNu_Vz = neutrino_vtx.Z();
    std::vector<float> fid_vtx_v = {m_vtx_fid_x_start, m_vtx_fid_y_start, m_vtx_fid_z_start,
                                    m_vtx_fid_x_end, m_vtx_fid_y_end, m_vtx_fid_z_end};
    fNu_Contained = IsContained(fNu_Vx, fNu_Vy, fNu_Vz, fid_vtx_v);

    if (!m_isData)
    {
      fTrueNu_VtxDistance = pandoraInterfaceHelper.Distance3D(fTrueNu_VxSce, fTrueNu_VySce, fTrueNu_VzSce, fNu_Vx, fNu_Vy, fNu_Vz);
    }
  }
  pandoraInterfaceHelper.CollectDownstreamPFParticles(particleMap, pfnu, pfdaughters);
  fNumDaughters = pfdaughters.size() - 1; // The neutrino itself is included here.
  std::cout << "[NuCCanalyzer::FillReconstructed] neutrino PDG: " << fNu_PDG << ", Primary Daughters: " << fNumPrimaryDaughters;
  std::cout << ", Daughters: " << fNumDaughters << ", TopoScore: " << fNu_Score;
  std::cout << ", TrueNu_VtxDistance: " << fTrueNu_VtxDistance << std::endl;

  for (auto const pfp : pfdaughters)
  {
    if (!pfp->IsPrimary())
    {
      if (!FillDaughters(pfp, MCSMu_handle, trackPIDAssn, track1handle, track2handle))
      {
        fDaughtersStored = false;
      }
      else
      {
        if (MatchDaughter(evt, pfp))
          fNumMatchedDaughters++;
        fNueDaughtersTree->Fill();
      }
    }
  }
  // Purity Completeness approximations:
  std::cout << "[NuCCanalyzer::FillReconstructed] Total MC hits in event: " << m_total_mc_hits << std::endl;
  std::cout << "[NuCCanalyzer::FillReconstructed] Total MC hits in nu pfps: " << fMatchedHits << std::endl;
  std::cout << "[NuCCanalyzer::FillReconstructed] Total hits in nu pfps: " << fNu_totalHits << std::endl;
  fMCHitsFraction = (float)fMatchedHits / fNu_totalHits;
  fClusteredHitCompleteness = (float)fMatchedHits / m_total_mc_hits;
  std::cout << "[NuCCanalyzer::FillReconstructed] Completeness: " << fClusteredHitCompleteness << " Purity: " << fMCHitsFraction << std::endl;

  // Store the obvious cosmic with the lowest score:
  fBestObviousCosmic_FlashChi2 = std::numeric_limits<float>::max();
  for (auto const pfp : pfparticles)
  {
    // Only look at obvious cosmics:
    lar_pandora::MetadataVector pfp_metadata_vec = particlesToMetadata.at(pfp);
    const larpandoraobj::PFParticleMetadata::PropertiesMap &pfp_properties = pfp_metadata_vec.front()->GetPropertiesMap();

    if (pfp_properties.count("IsClearCosmic"))
    {
      if (pfp_properties.at("IsClearCosmic") && pfp->IsPrimary())
      {
        const std::vector<art::Ptr<anab::T0>> T0_v = nuFlashScoreAsso.at(pfp.key());
        if (T0_v.size() == 1)
        {
          if (fBestObviousCosmic_FlashChi2 > T0_v.at(0)->TriggerConfidence())
          {
            fBestObviousCosmic_FlashChi2 = T0_v.at(0)->TriggerConfidence();
          }
        }
      }
    }
  }
  //std::cout << "[NuCCanalyzer::FillReconstructed] fNu_FlashChi2 / fBestObviousCosmic_FlashChi2: " << fNu_FlashChi2 / fBestObviousCosmic_FlashChi2 << std::endl;
}

bool NuCCanalyzer::FillDaughters(const art::Ptr<recob::PFParticle> &pfp,
                                 const art::ValidHandle<std::vector<recob::MCSFitResult>> &MCSMu_handle,
                                 const art::FindManyP<anab::ParticleID> &trackPIDAssn,
                                 const art::ValidHandle<std::vector<recob::Track>> &track1handle,
                                 const art::ValidHandle<std::vector<recob::Track>> &track2handle)
{
  clearDaughter();
  const lar_pandora::ClusterVector cluster_vec = particlesToClusters.at(pfp);
  std::vector<uint> nHits;
  std::vector<float> pfenergy;
  energyHelper.energy_from_hits(cluster_vec, nHits, pfenergy);
  fNhitsU = nHits[0];
  fNhitsV = nHits[1];
  fNhitsY = nHits[2];
  fCaloU = pfenergy[0];
  fCaloV = pfenergy[1];
  fCaloY = pfenergy[2];
  fNu_NhitsU += fNhitsU;
  fNu_NhitsV += fNhitsV;
  fNu_NhitsY += fNhitsY;
  fNu_totalHits += (fNhitsU + fNhitsV + fNhitsY);
  fNu_CaloU += fCaloU;
  fNu_CaloV += fCaloV;
  fNu_CaloY += fCaloY;

  if (particlesToSpacePoints.find(pfp) == particlesToSpacePoints.end())
  {
    // If a daughter has no associated spacepoints, count the hits to contribute to the total, but dont save the daughter
    std::cout << "[NuCCanalyzer::FillDaughters] Daughter had no associated spacepoints." << std::endl;
    return false;
  }
  fNSpacepoints = particlesToSpacePoints.at(pfp).size();
  fNu_NSpacepoints += fNSpacepoints;

  if (particlesToVertices.find(pfp) == particlesToVertices.end())
  {
    // If a daughter has no associated vertex, count the hits to contribute to the total, but dont save the daughter
    std::cout << "[NuCCanalyzer::FillDaughters] Daughter had no associated vertex." << std::endl;
    return false;
  }

  if (particlesToMetadata.at(pfp).size() != 1 || particlesToVertices.at(pfp).size() != 1)
  {
    std::cout << "[NuCCanalyzer::FillDaughters] Daughter association problem." << std::endl;
    return false;
  }
  const recob::Vertex::Point_t &pfp_vtx = particlesToVertices.at(pfp).front()->position();
  fVx = pfp_vtx.X();
  fVy = pfp_vtx.Y();
  fVz = pfp_vtx.Z();
  std::vector<float> pfp_start_fid_v(6, m_pfp_start_border);

  // There is a bug here!S
  fStartContained = IsContained(fVx, fVy, fVz, pfp_start_fid_v);
  if (!fStartContained)
  {
    fDaughtersStartContained = false;
  }

  const larpandoraobj::PFParticleMetadata::PropertiesMap &pfp_properties = particlesToMetadata.at(pfp).front()->GetPropertiesMap();
  fTrackScore = pfp_properties.at("TrackScore");
  fVtxDistance = pandoraInterfaceHelper.Distance3D(fVx, fVy, fVz, fNu_Vx, fNu_Vy, fNu_Vz);

  // Hierarchy info
  fGeneration = larpandora.GetGeneration(particleMap, pfp);
  if (fNumPrimaryDaughters < fNumDaughters)
  {
    if (particleMap.at(pfp->Parent())->PdgCode() == 13)
    {
      fIsTrackDaughter = true;
    }
    if (pfp->NumDaughters())
    {
      for (const int daughter_id : pfp->Daughters())
      {
        if (particleMap.at(daughter_id)->PdgCode() == 11)
        {
          fHasShowerDaughter = true;
        }
      }
    }
  }

  // Track-like fields
  if (particlesToTracks.find(pfp) != particlesToTracks.end())
  {
    fIsTrack = true;
    fNumTracks++;
    const art::Ptr<recob::Track> this_track = particlesToTracks.at(pfp).front();
    const recob::Track::Vector_t &track_dir = this_track->StartDirection();
    fTrackLength = this_track->Length();
    fTrackDirX = track_dir.X();
    fTrackDirY = track_dir.Y();
    fTrackDirZ = track_dir.Z();
    fTrackEndX = this_track->End().X();
    fTrackEndY = this_track->End().Y();
    fTrackEndZ = this_track->End().Z();

    // MCS momentum:
    std::vector<recob::Track::Point_t> posv_rm10 = this_track->Trajectory().Trajectory().Positions();
    std::vector<recob::Track::Vector_t> momv_rm10 = this_track->Trajectory().Trajectory().Momenta();
    std::vector<recob::TrajectoryPointFlags> flgv_rm10 = this_track->Trajectory().Flags();
    size_t init_size_rm10 = posv_rm10.size();
    for (size_t i=0;i<init_size_rm10;++i) {
      if (posv_rm10[init_size_rm10-1-i].X()>-1.) break;
      posv_rm10.pop_back();
      momv_rm10.pop_back();
      flgv_rm10.pop_back();
    }
    init_size_rm10 = posv_rm10.size();
    for (size_t i=0;i<init_size_rm10;++i) {
      if ((this_track->End()-posv_rm10[init_size_rm10-1-i]).R()>10.) break;
      posv_rm10.pop_back();
      momv_rm10.pop_back();
      flgv_rm10.pop_back();
    }

    if (posv_rm10.size()>4) {
      recob::TrackTrajectory tt_rm10(std::move(posv_rm10),std::move(momv_rm10),std::move(flgv_rm10),this_track->HasMomentum());
      const recob::MCSFitResult& mcsMu_rm10 = mcsfitter.fitMcs(tt_rm10,13);
      fTrackLength_rm10 = tt_rm10.Length();
      fTrackMom_MuFwd_rm10    = mcsMu_rm10.fwdMomentum();
      fTrackMomErr_MuFwd_rm10 = mcsMu_rm10.fwdMomUncertainty();
      fTrackLL_MuFwd_rm10     = mcsMu_rm10.fwdLogLikelihood();
      fTrackMom_MuBwd_rm10    = mcsMu_rm10.bwdMomentum();
      fTrackMomErr_MuBwd_rm10 = mcsMu_rm10.bwdMomUncertainty();
      fTrackLL_MuBwd_rm10     = mcsMu_rm10.bwdLogLikelihood();
      fTrackMom_Mu_rm10       = mcsMu_rm10.bestMomentum();
      fTrackMomErr_Mu_rm10    = mcsMu_rm10.bestMomUncertainty();
      fTrackLL_Mu_rm10        = mcsMu_rm10.bestLogLikelihood();
      fTrackDeltaLL_Mu_rm10   = mcsMu_rm10.deltaLogLikelihood();
      fTrackIsBestFwd_Mu_rm10 = mcsMu_rm10.isBestFwd();
    }

    const recob::MCSFitResult &mcsMu = MCSMu_handle->at(this_track.key());
    fTrackMCS_mom = mcsMu.fwdMomentum();
    fTrackMCS_err = mcsMu.fwdMomUncertainty();
    fTrackMCS_ll = mcsMu.fwdLogLikelihood();

    trackHelper.getRangeMomentum(fTrackLength, fTrackRange_mom_p, fTrackRange_mom_mu);

      // Split tracks
    // Original track
    auto id = this_track->ID();

    size_t imid = this_track->CountValidPoints() * 0.5;
    recob::tracking::Point_t midtk = this_track->Trajectory().LocationAtPoint(imid);
    recob::tracking::Vector_t middirtk = this_track->Trajectory().DirectionAtPoint(imid);
    recob::tracking::Plane tk_midplane(midtk, middirtk);
    fSplitTrackMidPars = {
      midtk.X(), midtk.Y(), midtk.Z(), middirtk.X(), middirtk.Y(), middirtk.Z()
    };

    // Loop over split tracks
    std::vector<std::pair<art::ValidHandle<std::vector<recob::Track> >, std::vector<double>*> > trackdefs = {
      { track1handle, &fSplitTrack1Pars },
      { track2handle, &fSplitTrack2Pars },
    };

    for (auto& it : trackdefs) {
      const recob::Track* track = nullptr;
      for (size_t itrk=0; itrk<it.first->size(); itrk++) {
        art::Ptr<recob::Track> ttmp(it.first, itrk);
        if (ttmp->ID() != id || ttmp->CountValidPoints() < 3) continue;
        track = ttmp.get();
        break;
      }

      if (!track) continue; 
      // Start
      const recob::tracking::Point_t x1(track->Start().X(), track->Start().Y(), track->Start().Z());
      const recob::tracking::Vector_t d1(track->StartDirection().X(), track->StartDirection().Y(), track->StartDirection().Z());
      auto lpar1 = track->VertexParametersLocal5D();
      auto lcov1 = track->VertexCovarianceLocal5D();
      trkf::TrackState state1(lpar1, lcov1, recob::tracking::Plane(x1, d1), true, 13);

      // End
      const recob::tracking::Point_t x2(track->End().X(), track->End().Y(), track->End().Z());
      const recob::tracking::Vector_t d2(track->EndDirection().X(), track->EndDirection().Y(), track->EndDirection().Z());
      auto lpar2 = track->EndParametersLocal5D();
      auto lcov2 = track->EndCovarianceLocal5D();
      trkf::TrackState state2(lpar2, lcov2, recob::tracking::Plane(x2, d2), true, 13);

      // Midpoint projection
      bool ok = true;  // Success flag passed by reference
      auto dist_mid_x1 = trkprop.distanceToPlane(ok, state1.position(), state1.momentum().Unit(), tk_midplane);
      auto dist_mid_x2 = trkprop.distanceToPlane(ok, state2.position(), state2.momentum().Unit(), tk_midplane);
      bool flip_mid = fabs(dist_mid_x2) < fabs(dist_mid_x1);
      auto state_mid = trkprop.propagateToPlane(ok, (flip_mid ? state2 : state1), tk_midplane, true, true, trkf::TrackStatePropagator::UNKNOWN);
      if (ok) {
        auto p6 = state_mid.parameters6D();
        it.second->assign(p6.begin(), p6.end());
      }
    }

    // PID information:
    std::map<std::string, float> pid_map;
    if (trackHelper.getPID(pid_map, this_track, trackPIDAssn))
    {
      fTrackPID_chiproton = pid_map.at("chi2_proton");
      fTrackPID_chimuon = pid_map.at("chi2_muon");
      std::cout << "[NuCCanalyzer::FillDaughters] fTrackPID_chiproton: " << fTrackPID_chiproton << ", fTrackPID_chimuon: " << fTrackPID_chimuon;
      std::cout << ", fTrackRange_mom_p: " << fTrackRange_mom_p << ", fTrackRange_mom_mu: " << fTrackRange_mom_mu << std::endl;
    }
    else
    {
      std::cout << "[NuCCanalyzer::FillDaughters] Track has no PID attached to it" << std::endl;
    }

    if (IsMuonCandidate())
    {
      // add pfp pointer to vector
      m_muon_candidates.push_back(pfp);
    }
  }

  // Shower-like fields
  if (particlesToShowers.find(pfp) != particlesToShowers.end())
  {
    fIsShower = true;
    fNumShowers++;
    const art::Ptr<recob::Shower> this_shower = particlesToShowers.at(pfp).front();
    if (this_shower->has_length() && this_shower->has_open_angle())
    {
      const TVector3 &shower_dir = this_shower->Direction();
      fShowerLength = this_shower->Length();
      fShowerOpenAngle = this_shower->OpenAngle();
      fShowerDirX = shower_dir.X();
      fShowerDirY = shower_dir.Y();
      fShowerDirZ = shower_dir.Z();

      std::vector<float> pitches(3, std::numeric_limits<float>::lowest());
      std::vector<float> dqdx(3, std::numeric_limits<float>::lowest());
      std::vector<std::vector<float>> dqdx_hits(3, std::vector<float>());
      energyHelper.dQdx(shower_dir, cluster_vec, clustersToHits, dqdx, dqdx_hits, pitches);
      std::vector<float> dedx = energyHelper.dEdx_from_dQdx(dqdx);

      fDedxU = dedx[0];
      fDedxV = dedx[1];
      fDedxY = dedx[2];
      fDedxHitsU = dqdx_hits[0].size();
      fDedxHitsV = dqdx_hits[1].size();
      fDedxHitsY = dqdx_hits[2].size();
      fDedxPitchU = pitches[0];
      fDedxPitchV = pitches[1];
      fDedxPitchY = pitches[2];
    }
    else
    {
      std::cout << "[NuCCanalyzer::FillDaughters] Bad shower, no length or opening angle!" << std::endl;
    }
  }

  std::cout << "[NuCCanalyzer::FillDaughters] Trackscore: " << fTrackScore << ", Generation: " << fGeneration;
  std::cout << ", vtx distance: " << fVtxDistance << std::endl;
  std::cout << "[NuCCanalyzer::FillDaughters] U Plane: Hits:" << fNhitsU << ", Energy: " << fCaloU << ", dedx hits: " << fDedxHitsU << ", dedx: " << fDedxU << ", pitch: " << fDedxPitchU << std::endl;
  std::cout << "[NuCCanalyzer::FillDaughters] V Plane: Hits:" << fNhitsV << ", Energy: " << fCaloV << ", dedx hits: " << fDedxHitsV << ", dedx: " << fDedxV << ", pitch: " << fDedxPitchV << std::endl;
  std::cout << "[NuCCanalyzer::FillDaughters] Y Plane: Hits:" << fNhitsY << ", Energy: " << fCaloY << ", dedx hits: " << fDedxHitsY << ", dedx: " << fDedxY << ", pitch: " << fDedxPitchY << std::endl;
  return true;
}

bool NuCCanalyzer::MatchDaughter(art::Event const &evt, const art::Ptr<recob::PFParticle> &pfp)
{
  if (m_isData)
    return false;

  art::Ptr<simb::MCParticle> matched_mcp;
  float matchedHitFraction = 0;

  if (fGeneration == 2)
  {
    if (matchedParticles.find(pfp) == matchedParticles.end())
    {
      fMatchedNeutrino = false;
      fCosmicMatched = true;
      return false;
    }
    matched_mcp = matchedParticles.at(pfp);
    matchedHitFraction = matchedHitFractions.at(pfp);
    fMatchedHits += matchedHits.at(pfp); // only for direct daughters
  }
  else if (fGeneration == 3)
  {
    // Generation 3 particle get matched to its parent.
    const auto iter(particleMap.find(pfp->Parent()));
    if (iter == particleMap.end())
      throw cet::exception("NuCCanalyzer::MatchDaughter") << "Scrambled PFParticle IDs" << std::endl;
    const art::Ptr<recob::PFParticle> &pfp_parent = iter->second;

    if (matchedParticles.find(pfp_parent) == matchedParticles.end())
    {
      fMatchedNeutrino = false;
      fCosmicMatched = true;
      return false;
    }
    matched_mcp = matchedParticles.at(pfp_parent);
    matchedHitFraction = matchedHitFractions.at(pfp_parent);
  }
  else
  {
    std::cout << "[NuCCanalyzer::MatchDaughter] Generation 4 particle is not matched." << std::endl;
    return false;
  }

  // Is this MC particle neutrino?
  const art::Ptr<simb::MCTruth> mctruth = pandoraInterfaceHelper.TrackIDToMCTruth(evt, m_geant_producer, matched_mcp->TrackId());
  if (mctruth->Origin() == simb::kBeamNeutrino)
  {
    fMatchedNeutrino = true;
  }
  else
  {
    fMatchedNeutrino = false;
    fCosmicMatched = true;
  }

  fTruePDG = matched_mcp->PdgCode();
  fTrueHitFraction = matchedHitFraction;
  fTrueEnergy = matched_mcp->E();
  fTrueVx = matched_mcp->Vx();
  fTrueVy = matched_mcp->Vy();
  fTrueVz = matched_mcp->Vz();
  fTruePx = matched_mcp->Px();
  fTruePy = matched_mcp->Py();
  fTruePz = matched_mcp->Pz();
  fTrueLength = (matched_mcp->Position().Vect() - matched_mcp->EndPosition().Vect()).Mag();

  pandoraInterfaceHelper.SCE(fTrueVx, fTrueVy, fTrueVz, matched_mcp->T(),
                             fTrueVxSce, fTrueVySce, fTrueVzSce);
  std::cout << "[NuCCanalyzer::MatchDaughter] Daughter matched with PDG: " << fTruePDG << ", hit purity: " << matchedHitFraction << std::endl;

  return true;
}

void NuCCanalyzer::FillTrueNu(art::Event const &evt)
{
  auto const &generator_handle = evt.getValidHandle<std::vector<simb::MCTruth>>("generator");
  auto const &generator(*generator_handle);
  fNumNu = generator.size();
  std::cout << "[NuCCanalyzer::FillTrueNu] True neutrinos found: " << fNumNu;
  if (generator.size() > 0)
  {
    if (generator.front().Origin() != simb::kBeamNeutrino)
    {
      std::cout << "[NuCCanalyzer::FillTrueNu] Origin of generator particle is not kBeamNeutrino." << std::endl;
      return;
    }
    const simb::MCNeutrino &mcnu = generator.front().GetNeutrino();

    fTrueNu_InteractionType = mcnu.Mode();
    fTrueNu_CCNC = mcnu.CCNC();
    fTrueNu_Target = mcnu.Target();
    fTrueNu_HitNuc = mcnu.HitNuc();
    fTrueNu_HitQuark = mcnu.HitQuark();
    fTrueNu_W = mcnu.W();
    fTrueNu_X = mcnu.X();
    fTrueNu_Y = mcnu.Y();
    fTrueNu_QSqr = mcnu.QSqr();
    fTrueNu_LeptonTheta = mcnu.Theta();

    fTrueNu_PDG = mcnu.Nu().PdgCode();
    fTrueNu_Energy = mcnu.Nu().E();
    fTrueNu_Px = mcnu.Nu().Px();
    fTrueNu_Py = mcnu.Nu().Py();
    fTrueNu_Pz = mcnu.Nu().Pz();
    fTrueNu_LeptonEnergy = mcnu.Lepton().E();
    fTrueNu_LeptonPx = mcnu.Lepton().Px();
    fTrueNu_LeptonPy = mcnu.Lepton().Py();
    fTrueNu_LeptonPz = mcnu.Lepton().Pz();
    fTrueNu_Time = mcnu.Nu().T();
    fTrueNu_Vx = mcnu.Nu().Vx();
    fTrueNu_Vy = mcnu.Nu().Vy();
    fTrueNu_Vz = mcnu.Nu().Vz();
    pandoraInterfaceHelper.SCE(fTrueNu_Vx, fTrueNu_Vy, fTrueNu_Vz, fTrueNu_Time,
                               fTrueNu_VxSce, fTrueNu_VySce, fTrueNu_VzSce);
    std::cout << ", CCNC: " << fTrueNu_CCNC << ", PDG: " << fTrueNu_PDG << ", E: " << fTrueNu_Energy << ", z-vertex: " << fTrueNu_Vz << std::endl;
  }

  lar_pandora::MCParticleVector mcparticles;
  larpandora.CollectMCParticles(evt, m_geant_producer, mcparticles);

  for (auto const &mcparticle : mcparticles)
  {
    if (!(mcparticle->Process() == "primary" &&
          mcparticle->T() != 0 &&
          mcparticle->StatusCode() == 1))
      continue;

    const art::Ptr<simb::MCTruth> mc_truth = pandoraInterfaceHelper.TrackIDToMCTruth(evt, m_geant_producer, mcparticle->TrackId());
    if (mc_truth->Origin() == simb::kBeamNeutrino)
    {
      fTrueNu_DaughterE.push_back(mcparticle->E());
      fTrueNu_DaughterPDG.push_back(mcparticle->PdgCode());
    }
  }
}

void NuCCanalyzer::FillTrueNuDaughters(art::Event const &evt)
{
  lar_pandora::MCParticleVector mcparticles;
  larpandora.CollectMCParticles(evt, m_geant_producer, mcparticles);

  for (auto const &mcparticle : mcparticles)
  {
    if (!(mcparticle->Process() == "primary" &&
          mcparticle->T() != 0 &&
          mcparticle->StatusCode() == 1))
      continue;

    const art::Ptr<simb::MCTruth> mc_truth = pandoraInterfaceHelper.TrackIDToMCTruth(evt, m_geant_producer, mcparticle->TrackId());
    if (mc_truth->Origin() == simb::kBeamNeutrino)
    {
      bool daughter_matched_neutrino_pfp = false;
      if (matchedMCParticles.find(mcparticle) != matchedMCParticles.end())
      {
        // Check if the corresponding pfparticle is also attached to the neutrino:
        for (auto const &[key, val] : matchedParticles)
        {
          if (val->TrackId() == mcparticle->TrackId())
          {
            if (larpandora.IsNeutrino(larpandora.GetParentPFParticle(particleMap, key)))
            {
              daughter_matched_neutrino_pfp = true;
              break;
            }
          }
        }
      }
      fTrueNu_DaughterMatched.push_back(daughter_matched_neutrino_pfp);
      std::cout << "[NuCCanalyzer::FillTrueNuDaughters] << PDG: " << mcparticle->PdgCode() << ", E: " << mcparticle->E() << ", was matched? " << fTrueNu_DaughterMatched.back() << std::endl;
    }
  }
}

void NuCCanalyzer::FillReconTruthMatching(art::Event const &evt)
{
  m_total_mc_hits = pandoraInterfaceHelper.Configure(evt, m_pfp_producer, m_pfp_producer, m_hitfinder_producer, m_geant_producer, m_hit_mcp_producer);
  pandoraInterfaceHelper.GetRecoToTrueMatches(matchedParticles, matchedHitFractions, matchedHits);
  std::cout << "[NuCCanalyzer::FillReconTruthMatching] ";
  std::cout << "Number of PFPparticles in event: " << pfparticles.size() << std::endl;
  for (auto it = matchedParticles.begin(); it != matchedParticles.end(); ++it)
  {
    matchedMCParticles.insert(it->second);
  }
  std::cout << "[NuCCanalyzer::FillReconTruthMatching] ";
  std::cout << "PFParticlesToMCParticles constructed: Number of PFPparticles matched: " << matchedParticles.size() << std::endl;
}

bool NuCCanalyzer::IsContained(float x, float y, float z, const std::vector<float> &borders) const
{
  float fidvolXstart = borders[0];
  float fidvolYstart = borders[1];
  float fidvolZstart = borders[2];
  float fidvolXend = borders[3];
  float fidvolYend = borders[4];
  float fidvolZend = borders[5];

  art::ServiceHandle<geo::Geometry> geo;
  std::vector<double> bnd = {
      0., 2. * geo->DetHalfWidth(), -geo->DetHalfHeight(), geo->DetHalfHeight(),
      0., geo->DetLength()};

  bool is_x = x > (bnd[0] + fidvolXstart) && x < (bnd[1] - fidvolXend);
  bool is_y = y > (bnd[2] + fidvolYstart) && y < (bnd[3] - fidvolYend);
  bool is_z = z > (bnd[4] + fidvolZstart) && z < (bnd[5] - fidvolZend);

  return is_x && is_y && is_z;
}

bool NuCCanalyzer::IsMuonCandidate()
{
  fIsMuonCandidate = fGeneration == 2 &&
                     m_muon_cut_trackscore < fTrackScore &&
                     m_muon_cut_vtxdistance > fVtxDistance &&
                     m_muon_cut_protonchi2 < fTrackPID_chiproton &&
                     m_muon_cut_muonchi2 > fTrackPID_chimuon &&
                     m_muon_cut_length < fTrackLength &&
                     m_muon_cut_chiratio < (fTrackPID_chiproton / fTrackPID_chimuon);

  return fIsMuonCandidate;
}

bool NuCCanalyzer::IsNuMuCC(art::Event const &evt)
{
  art::Ptr<recob::PFParticle> pfnu = pfneutrinos.front();
  art::Handle<std::vector<recob::PFParticle>> pfparticles_handle;
  evt.getByLabel(m_pfp_producer, pfparticles_handle);
  art::FindManyP<anab::T0> pfp_muon_assn(pfparticles_handle, evt, m_muon_producer);

  for (size_t daughter_id : pfnu->Daughters())
  {
    const std::vector<art::Ptr<anab::T0>> T0_muon = pfp_muon_assn.at(particleMap.at(daughter_id).key());
    if (T0_muon.size() != 0)
    {
      std::cout << "[NuCCfilter::filter] Muon neutrino daughter found! Event passed filter." << std::endl;
      return true;
    }
  }
  return false;
}
