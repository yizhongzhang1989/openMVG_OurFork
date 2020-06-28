// This file is part of OpenMVG_IMU , a branch of OpenMVG
// Author: Bao Chong
// Date:2020/06

#include "openMVG_IMU/sfm/pipelines/global/sfm_global_engine_iterative_pose_augmentation.hpp"
#include "openMVG_IMU/sfm/pipelines/global/myoutput.hpp"
#include "openMVG_IMU/matching_image_collection/ComputeMatchesController.hpp"

#include "openMVG/cameras/Camera_Common.hpp"
#include "openMVG/graph/graph.hpp"
#include "openMVG/features/feature.hpp"
#include "openMVG/matching/indMatch.hpp"

#include "openMVG/sfm/pipelines/relative_pose_engine.hpp"
#include "openMVG/sfm/pipelines/sfm_features_provider.hpp"
#include "openMVG/sfm/pipelines/sfm_matches_provider.hpp"
#include "openMVG/sfm/pipelines/global/GlobalSfM_rotation_averaging.hpp"
#include "openMVG/sfm/sfm_data_BA.hpp"
#include "openMVG/sfm/sfm_data_BA_ceres.hpp"
#include "openMVG/sfm/sfm_data_io.hpp"
#include "openMVG/sfm/sfm_data_filters.hpp"
#include "openMVG/sfm/sfm_data_triangulation.hpp"
#include "openMVG/sfm/sfm_filters.hpp"
#include "openMVG/stl/stl.hpp"
#include "openMVG/system/timer.hpp"
#include "openMVG/tracks/tracks.hpp"
#include "openMVG/types.hpp"


#include "third_party/histogram/histogram.hpp"
#include "third_party/htmlDoc/htmlDoc.hpp"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"

#include <ceres/types.h>

#include <iostream>

#ifdef _MSC_VER
#pragma warning( once : 4267 ) //warning C4267: 'argument' : conversion from 'size_t' to 'const int', possible loss of data
#endif

namespace openMVG{
namespace sfm{

using namespace openMVG::cameras;
using namespace openMVG::geometry;
using namespace openMVG::features;

GlobalSfMReconstructionEngine_IterativePoseAugmentation::GlobalSfMReconstructionEngine_IterativePoseAugmentation(
  const SfM_Data & sfm_data,
  const std::string & soutDirectory,
  const std::string & sloggingFile)
  : GlobalSfMReconstructionEngine_RelativeMotions_General(sfm_data,soutDirectory,sloggingFile)
{

  
}

GlobalSfMReconstructionEngine_IterativePoseAugmentation::~GlobalSfMReconstructionEngine_IterativePoseAugmentation()
{
  
}

void GlobalSfMReconstructionEngine_IterativePoseAugmentation::SetMatchesDir(const std::string MatchesDir)
{
    sMatchesDir_=MatchesDir;
}

bool GlobalSfMReconstructionEngine_IterativePoseAugmentation::Run()
{
  Pair_Set extra_pairs;
  unsigned int loop_i = 1;
  //Because the matching of a image pair is only based on the features in the image,
  //even though optimized by ba, the failed image pair in matching phase will still fail.
  //So we develop a set to record the failed or succeeded matches to avoid duplicate matching for speeds
  Pair_Set tried_pairs;
  while(LoopDetection(extra_pairs, tried_pairs))
  {
	  system::Timer augmentation_once_timer;
      std::cout<<"///////////////////////////\n"
               <<"//////////loop "<<loop_i<<"/////////\n"
               <<"///////////////////////////\n";
	  tried_pairs.insert(extra_pairs.begin(), extra_pairs.end());
      std::cout<<"Detect "<<extra_pairs.size()<<" pairs\n";
      ///////////////////////////////////////////////////////
	  ////////////match extra image pair start///////////////
	  ///////////////////////////////////////////////////////
	  system::Timer matching_timer;
	  std::shared_ptr<Matches_Provider> extra_matches_provider = std::make_shared<Matches_Provider>();
	  matching_image_collection::ComputeMatchesController::Process(sfm_data_,
																   sMatchesDir_,extra_matches_provider.get(),
		                                                           "f",
                                                                   true,
																   extra_pairs,
																   true, 
		                                                           stlplus::create_filespec(sOut_directory_, "matches_f_" + std::to_string(loop_i), ".bin"));
	  std::cout << "Matching task done in (s): " << matching_timer.elapsed() << std::endl;

      std::cout<<"Compute "<<extra_matches_provider->pairWise_matches_.size()<<" matches in "<<extra_pairs.size()<<" pairs\n";
      if(extra_matches_provider->pairWise_matches_.size()==0)
      {
        std::cout<<"No more matches are found\n";
        break;
      }
      // add extra matches into total matches
      matching::PairWiseMatches::iterator iter;
 
      for (iter = extra_matches_provider->pairWise_matches_.begin(); iter != extra_matches_provider->pairWise_matches_.end();
        )
      {
        if (matches_provider_->pairWise_matches_.count(iter->first) == 0)
        {
          matches_provider_->pairWise_matches_.insert(*iter);
          iter++;
        }
        else  //remove already existing matches 
        {
          iter=extra_matches_provider->pairWise_matches_.erase(iter);
        }
      }
      

      if(extra_matches_provider->pairWise_matches_.size()==0)
      {
        std::cout<<"No more matches are found\n";
        break;
      }
	   
	  ///////////////////////////////////////////////////////
	  ////////////////////motion averaging///////////////////
	  ///////////////////////////////////////////////////////
	  system::Timer averaging_timer;
      if(!Process(extra_matches_provider))
      {
        std::cout<<"Global sfm augmentation failed\n";
        break;
      }
	  std::cout << "Remain " << extra_matches_provider->pairWise_matches_.size() << " extra pairs\n";
	  std::cout << "Averaging task done in (s): " << averaging_timer.elapsed() << std::endl;

	  
	  ///////////////////////////////////////////////////////
	  ////////////////////////optimize///////////////////////
	  ///////////////////////////////////////////////////////
	  system::Timer optimizing_timer;
	  Optimize();
	  std::cout << "Optimizing task done in (s): " << optimizing_timer.elapsed() << std::endl;
	  
	  //save the sfm scene data
	  Save(sfm_data_,
		  stlplus::create_filespec(sOut_directory_, "sfm_data_augmentation_loop_"  + std::to_string(loop_i), ".json"),
		  ESfM_Data(ALL));
	  loop_i++;
	  std::cout << "This augmentation task done in (s): " << augmentation_once_timer.elapsed() << std::endl;

  }
  return true; 
}

bool GlobalSfMReconstructionEngine_IterativePoseAugmentation::LoopDetection(Pair_Set& extra_pairs,const Pair_Set& tried_pairs)
{
  //clear output container.
  extra_pairs.clear();
	const double MaxAngleThreshold = 25.0;

  // Enumerate any two cameras for searching the proper camera pair
  // whose angular is less than `MaxAngleThreshold`.
  for(const auto& view_i:sfm_data_.GetViews())
  {
	if (!sfm_data_.GetPoses().count(view_i.first))
	{
		std::cout << "View " << view_i.first << " is not calibrated\n";
		continue;
	}
    const Pose3& pose_i = sfm_data_.GetPoseOrDie(view_i.second.get());
    const Mat3 R_i = pose_i.rotation();
    for(const auto& view_j:sfm_data_.GetViews())
    {
      if(view_i.first>=view_j.first) continue;
      Pair pair(view_i.first,view_j.first);
	  Pair pair_inverse(view_j.first, view_i.first);
	  //ignore the pairs already matched
      if(matches_provider_->pairWise_matches_.count(pair)||
		 matches_provider_->pairWise_matches_.count(pair_inverse)) continue;
	  //ignore the pairs already tried
	  if (tried_pairs.count(pair) || tried_pairs.count(pair_inverse)) continue;

	  if (!sfm_data_.GetPoses().count(view_j.first))
	  {
		  std::cout << "View " << view_j.first << " is not calibrated\n";
		  continue;
	  }

	  //compute angular
      const Pose3& pose_j = sfm_data_.GetPoseOrDie(view_j.second.get());
      const Mat3 R_j = pose_j.rotation();
      const double angularErrorDegree = R2D(getRotationMagnitude(R_i * R_j.transpose()));
      if (angularErrorDegree < MaxAngleThreshold)
      {
          extra_pairs.insert(pair);
      }
    }
  }
  return extra_pairs.size() > 0;
}

bool GlobalSfMReconstructionEngine_IterativePoseAugmentation::Optimize() {

   std::cout<<"/////IMU Global SfM BA/////\n";
  
  if (!Adjust())
  {
    std::cerr << "GlobalSfM:: Non-linear adjustment failure!" << std::endl;
    return false;
  }
  
  //-- Export statistics about the SfM process
  std::cout << "Structure from Motion statistics.";


  
  std::cout << "-------------------------------" << "\n"
    << "-- View count: " << sfm_data_.GetViews().size() << "\n"
    << "-- Intrinsic count: " << sfm_data_.GetIntrinsics().size() << "\n"
    << "-- Pose count: " << sfm_data_.GetPoses().size() << "\n"
    << "-- Track count: "  << sfm_data_.GetLandmarks().size() << "\n"
    << "-------------------------------" << "\n";
    

  return true;
}

bool GlobalSfMReconstructionEngine_IterativePoseAugmentation::Process(std::shared_ptr<Matches_Provider> extra_matches_provider_) {


  
  std::cout<<"/////IMU Global SfM Iterative Pose Augmentation/////\n";
  //-------------------
  // Keep only the largest biedge connected subgraph
  //-------------------
  {
    Pair_Set pairs = matches_provider_->getPairs();
    const std::set<IndexT> set_remainingIds = graph::CleanGraph_KeepLargestBiEdge_Nodes<Pair_Set, IndexT>(pairs);
    if (set_remainingIds.empty())
    {
      std::cout << "Invalid input image graph for global SfM" << std::endl;
      return false;
    }
    KeepOnlyReferencedElement(set_remainingIds, matches_provider_->pairWise_matches_);
  }
  ////START(Author: BC)++++++++++++++++++++++++++++++++++++++++++++++
  if(sfm_data_.GetPoses().size()==0)
  {
    std::cout<<"Invalid input pose graph for global sfm\n";
    return false;
  }

  Hash_Map<IndexT, Mat3> global_rotations;
  for(const auto& pose_item: sfm_data_.GetPoses())
  {
      global_rotations.emplace(pose_item.first, pose_item.second.rotation());
  }
  
  matching::PairWiseMatches  tripletWise_matches;
  if (!Compute_Global_PrioTranslations(global_rotations, tripletWise_matches,extra_matches_provider_.get()))
  {
    std::cerr << "GlobalSfM:: Translation Averaging failure!" << std::endl;
    return false;
  }
  //recompute structure
  sfm_data_.structure.clear();    //bc
  //END(Author: BC)===================================================
  if (!Compute_Initial_Structure(tripletWise_matches))
  {
    std::cerr << "GlobalSfM:: Cannot initialize an initial structure!" << std::endl;
    return false;
  }
  

  return true;
}


/// Compute/refine relative translations and compute global translations
bool GlobalSfMReconstructionEngine_IterativePoseAugmentation::Compute_Global_PrioTranslations
(
  const Hash_Map<IndexT, Mat3> & global_rotations,
  matching::PairWiseMatches & tripletWise_matches,
  Matches_Provider* extra_matches_provider_
)
{
  // Translation averaging (compute translations & update them to a global common coordinates system)
  ////START(Author: BC)++++++++++++++++++++++++++++++++++++++++++++++
  GlobalSfM_PrioTranslation_AveragingSolver priotranslation_averaging_solver;
  const bool bTranslationAveraging = priotranslation_averaging_solver.MyRun(
    eTranslation_averaging_method_,
    sfm_data_,
    features_provider_,
    matches_provider_,
    global_rotations,
    tripletWise_matches,
    extra_matches_provider_);
  //END(Author: BC)===================================================
  /*if (!sLogging_file_.empty())
  {
    Save(sfm_data_,
      stlplus::create_filespec(stlplus::folder_part(sLogging_file_), "cameraPath_translation_averaging", "ply"),
      ESfM_Data(EXTRINSICS));
  }*/

  return bTranslationAveraging;
}



} // namespace sfm
} // namespace openMVG
