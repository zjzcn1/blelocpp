/*******************************************************************************
 * Copyright (c) 2014, 2016  IBM Corporation and others
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************/

#ifndef StreamParticleFilterBuilder_hpp
#define StreamParticleFilterBuilder_hpp

#include <iostream>
#include <string>

#include "StreamLocalizer.hpp"
#include "StreamParticleFilter.hpp"

#include "PoseRandomWalker.hpp"
#include "RandomWalker.hpp"

#include "GridResampler.hpp"
#include "StatusInitializerStub.hpp"
#include "StatusInitializerImpl.hpp"

#include "DataStore.hpp"
#include "DataStoreImpl.hpp"

#include "OrientationMeterAverage.hpp"
#include "PedometerWalkingState.hpp"

#include "GaussianProcessLDPLMultiModel.hpp"

// for pose random walker in building
#include "Building.hpp"
#include "SystemModelInBuilding.hpp"

#include "BeaconFilterChain.hpp"
#include "CleansingBeaconFilter.hpp"
#include "StrongestBeaconFilter.hpp"

#include "ObservationDependentInitializer.hpp"
#include "MetropolisSampler.hpp"

#include "SerializeUtils.hpp"

namespace loc {
    class StreamParticleFilterBuilder{
    private:
        std::string mTrainDataPath;
        std::string mBeaconDataPath;
        std::string mMapDataPath;
        
        std::shared_ptr<DataStore> mDataStore;
        std::shared_ptr<GaussianProcessLDPLMultiModel<State, Beacons>> mObsModel;
        
    public:
        
        double mixProbability = 0.0;
        bool usesObservationDependentInitializer = false;
        bool shortCSV = false;
        bool jsonSample = false;
        float unit = 1.0;
        float alphaWeaken = 0.3;
        bool randomWalker = false;
        bool considerBias = false;
        std::string trainedModelPath;
        
        double minRssiBias = -10;
        double maxRssiBias = 10;
        double meanRssiBias = 0.0;
        double stdRssiBias = 2.0;
        double diffusionRssiBias = 0.2;
        double poseProperty_stdX = 0.0;
        double poseProperty_stdY = 2.0;
        
        double tDistribution = -1;
        
        StreamParticleFilterBuilder& trainDataPath(std::string trainDataPath){
            mTrainDataPath = trainDataPath;
            return *this;
        }        
        
        StreamParticleFilterBuilder& beaconDataPath(std::string beaconDataPath){
            mBeaconDataPath = beaconDataPath;
            return *this;
        }
        
        StreamParticleFilterBuilder& mapDataPath(std::string mapDataPath){
            mMapDataPath = mapDataPath;
            return *this;
        }
        
        void saveTrainedModel(std::string trainedModelPath){
            if(trainedModelPath!=""){
                std::ofstream ofs(trainedModelPath);
                mObsModel->save(ofs);
            }
        }
        
        std::shared_ptr<DataStore> buildDataStore();
        std::shared_ptr<StreamLocalizer> build();
        
        const std::shared_ptr<DataStore>& dataStore(){
            return mDataStore;
        }
        
        const std::shared_ptr<GaussianProcessLDPLMultiModel<State, Beacons>>& obsModel(){
            return mObsModel;
        }

    };
}


#endif /* StreamParticleFilterFactory_hpp */
