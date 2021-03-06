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

#include "StreamParticleFilterBuilder.hpp"
#include "SystemModelInBuilding.hpp"

namespace loc{
    
    std::shared_ptr<DataStore> StreamParticleFilterBuilder::buildDataStore(){
        // Create data store
        //            std::shared_ptr<DataStoreImpl> dataStore(new DataStoreImpl());
        std::shared_ptr<DataStoreImpl> dataStore(new DataStoreImpl());
        
        // Sampling data
        std::vector<std::string> trainPathes;
        trainPathes.push_back(mTrainDataPath);
        Samples samples;
        for(std::string trainPath: trainPathes){
            std::ifstream  istream(trainPath);
            
            Samples samplesTmp;
            if (shortCSV) {
                samplesTmp = DataUtils::shortCsvSamplesToSamples(istream);
                
                // assume it is in 3-feet unit
                for(loc::Sample& sample: samplesTmp) {
                    Location l = sample.location();
                    l = Location(l.x()*3*0.3048, l.y()*3*0.3048, l.z()*3*0.3048, l.floor());
                    sample.location(l);
                }
            } else if (jsonSample){
                picojson::value value;
                std::string err = picojson::parse(value, istream);
                picojson::array& array = value.get<picojson::array>();
                samplesTmp = DataUtils::jsonSamplesArrayToSamples(array);
            } else {
                samplesTmp = DataUtils::csvSamplesToSamples(istream);
            }
            samples.insert(samples.end(), samplesTmp.begin(), samplesTmp.end());
        }
        dataStore->samples(samples);
        
        // BLE beacon locations
        std::vector<std::string> bleBeaconPathes;
        bleBeaconPathes.push_back(mBeaconDataPath);
        BLEBeacons bleBeacons;
        for(std::string bleBeaconPath : bleBeaconPathes){
            std::ifstream bleBeaconIStream(bleBeaconPath);
            BLEBeacons bleBeaconsTmp = DataUtils::csvBLEBeaconsToBLEBeacons(bleBeaconIStream);
            bleBeacons.insert(bleBeacons.end(), bleBeaconsTmp.begin(), bleBeaconsTmp.end());
        }
        for(loc::BLEBeacon& beacon: bleBeacons) {
            beacon.Location::x(beacon.Location::x()*unit);
            beacon.Location::y(beacon.Location::y()*unit);
        }
        dataStore->bleBeacons(bleBeacons);
        
        // Building
        ImageHolder::setMode(ImageHolderMode(light));
        BuildingBuilder buildingBuilder;
        
        if (mMapDataPath.find(",") != std::string::npos) {
            std::list<std::string> items;
            std::string delim (",");
            boost::split(items, mMapDataPath, boost::is_any_of(delim));
            for(std::list<std::string>::iterator it = items.begin(); it != items.end(); it++) {
                int floor_num = std::atoi(it->c_str());
                if (++it == items.end()) break;
                double ppmx = std::atof(it->c_str());
                if (++it == items.end()) break;
                double ppmy = std::atof(it->c_str());
                if (++it == items.end()) break;
                double originx = std::atof(it->c_str());
                if (++it == items.end()) break;
                double originy = std::atof(it->c_str());
                if (++it == items.end()) break;
                std::string path = *it;
                CoordinateSystemParameters coordSysParams(ppmx, ppmy, 1, originx, originy, 0);
                buildingBuilder.addFloorCoordinateSystemParametersAndImagePath(floor_num, coordSysParams, path);
            }
        } else {
            int floor_max = 1;
            double ppmx = 8;
            double ppmy = -8;
            double ppmz = 1;
            double originx = 1000;
            double originy = 1000;
            double originz = 0;
            
            CoordinateSystemParameters coordSysParams(ppmx, ppmy, ppmz, originx, originy, originz);
            std::string path = mMapDataPath;
        
            for(int floor_num=0; floor_num<floor_max; floor_num++){
                buildingBuilder.addFloorCoordinateSystemParametersAndImagePath(floor_num, coordSysParams, path);
            }
        }
        
        dataStore->building(buildingBuilder.build());
        
        return dataStore;
    }
    
    
    std::shared_ptr<StreamLocalizer> StreamParticleFilterBuilder::build(){
        
        std::shared_ptr<StreamParticleFilter> localizer(new StreamParticleFilter());
        
        int nStates = 1000;
        localizer->numStates(nStates);
        //double alphaWeaken = 0.3;
        localizer->alphaWeaken(alphaWeaken);
        // set stdev lower bound.
        Location locLB(0.0,0.0,0,0);
        localizer->locationStandardDeviationLowerBound(locLB);
        
        // Create data store
        std::shared_ptr<DataStore> dataStore = buildDataStore();
        mDataStore = dataStore;
        
        // Instantiate sensor data processors
        // Pedometer
        PedometerWalkingStateParameters pedometerWSParams;
        pedometerWSParams.updatePeriod(0.1);
        if (randomWalker) {
            pedometerWSParams.walkDetectSigmaThreshold(0);
        }
        std::shared_ptr<Pedometer> pedometer(new PedometerWalkingState(pedometerWSParams));
        
        // Orientation
        OrientationMeterAverageParameters orientationMeterAverageParameters;
        orientationMeterAverageParameters.windowAveraging(0.1)
        .interval(0.01);
        
        std::shared_ptr<OrientationMeter> orientationMeter(new OrientationMeterAverage(orientationMeterAverageParameters));
        
        // Set dependency
        localizer->orientationMeter(orientationMeter);
        localizer->pedometer(pedometer);
        
        // Build System Model
        auto poseProperty = PoseProperty::Ptr(new PoseProperty);
        auto stateProperty = StateProperty::Ptr(new StateProperty);
        // The initial velocity of a particle is sampled from a truncated normal distribution defined by a mean, a standard deviation, a lower bound and an upper bound.
        poseProperty->meanVelocity(1.0); // mean
        poseProperty->stdVelocity(0.6); // standard deviation
        
        // if no effective beacon information, the average speed will be stable aroud (min+max)/2
        poseProperty->minVelocity(0.1); // lower bound
        poseProperty->maxVelocity(3.0); // upper bound
        poseProperty->diffusionVelocity(0.30); // standard deviation of a noise added to the velocity of a particle [m/s/s]
        
        poseProperty->stdOrientation(3.0/180.0*M_PI); // standard deviation of a noise added to the orientation obtained from the smartphone sensor.
        //poseProperty->stdOrientation(10*M_PI); // standard deviation of a noise added to the orientation obtained from the smartphone sensor.
        
        // The initial location of a particle is generated by adding a noise to a location where sampling data were collected. These are parameters of the noise.
        poseProperty->stdX(poseProperty_stdX); // standard deviation in x axis. (Zero in 1D cases.)
        poseProperty->stdY(poseProperty_stdY); // standard deviation in y axis.
        
        // The difference between the observed and the predicted RSSI is described by a RSSI bias parameter. The initial value is sampled from a normal distribution.
        stateProperty->meanRssiBias(meanRssiBias); // mean
        stateProperty->stdRssiBias(stdRssiBias); // standard deviation
        stateProperty->diffusionRssiBias(diffusionRssiBias); // standard deviation of a noise added to the rssi bias [dBm/s]
        stateProperty->minRssiBias(minRssiBias);
        stateProperty->maxRssiBias(maxRssiBias);
        // The difference between the observed and the actual orientation is described by a orientation bias parameter.
        stateProperty->diffusionOrientationBias(10.0/180*M_PI); // standard deviation of a noise added to the orientation bias [rad/s]
        
        // Build poseRandomWalker
        auto poseRandomWalkerProperty = PoseRandomWalkerProperty::Ptr(new PoseRandomWalkerProperty);
        std::shared_ptr<PoseRandomWalker>poseRandomWalker(new PoseRandomWalker());
        poseRandomWalkerProperty->orientationMeter(orientationMeter.get());
        poseRandomWalkerProperty->pedometer(pedometer.get());
        poseRandomWalkerProperty->angularVelocityLimit(30.0/180.0*M_PI);
        
        if (randomWalker) {
            poseProperty->meanVelocity(1.5); // mean
            poseProperty->stdVelocity(1.5); // standard deviation
            poseProperty->diffusionVelocity(1.5); // standard deviation of a noise added to the velocity of a particle [m/s/s]
            poseProperty->stdOrientation(2*M_PI); // standard deviation of a noise added to the orientation obtained from the smartphone sensor.
            stateProperty->diffusionOrientationBias(2*M_PI); // standard deviation of a noise added to the orientation bias [rad/s]
            poseRandomWalkerProperty->angularVelocityLimit(2*M_PI);
        }
        
        poseRandomWalker->setProperty(poseRandomWalkerProperty);
        poseRandomWalker->setPoseProperty(poseProperty);
        poseRandomWalker->setStateProperty(stateProperty);
        
        // Combine poseRandomWalker and building
        PoseRandomWalkerInBuildingProperty::Ptr prwBuildingProperty(new PoseRandomWalkerInBuildingProperty);
        // TODO
        prwBuildingProperty->maxIncidenceAngle(45.0/180.0*M_PI);
        prwBuildingProperty->weightDecayRate(0.9);
        // END TODO
        Building building = dataStore->getBuilding();
        Building::Ptr buildingPtr(new Building(building));
        
        std::shared_ptr<PoseRandomWalkerInBuilding> poseRandomWalkerInBuilding(new PoseRandomWalkerInBuilding());
        poseRandomWalkerInBuilding->poseRandomWalker(poseRandomWalker);
        poseRandomWalkerInBuilding->building(buildingPtr);
        poseRandomWalkerInBuilding->poseRandomWalkerInBuildingProperty(prwBuildingProperty);
        
        localizer->systemModel(poseRandomWalkerInBuilding);
        
        // set resampler
        std::shared_ptr<Resampler<State>> resampler(new GridResampler<State>());
        localizer->resampler(resampler);
        
        // Set status initializer
        
        std::shared_ptr<StatusInitializerImpl> statusInitializer(new StatusInitializerImpl());
        statusInitializer->dataStore(dataStore)
        .poseProperty(poseProperty).stateProperty(stateProperty);
        localizer->statusInitializer(statusInitializer);
        
        
        std::shared_ptr<GaussianProcessLDPLMultiModel<State, Beacons>> obsModel(new GaussianProcessLDPLMultiModel<State, Beacons>());
        {
            std::ifstream ifs(trainedModelPath);
            if (ifs.is_open()) {
                std::cout << "De-serializing observationModel" <<std::endl;
                obsModel->load(ifs);
                this->mObsModel = obsModel;
                localizer->observationModel(obsModel);
            } else {
                // Train observation model
                std::shared_ptr<GaussianProcessLDPLMultiModelTrainer<State, Beacons>>obsModelTrainer( new GaussianProcessLDPLMultiModelTrainer<State, Beacons>());
                obsModelTrainer->dataStore(dataStore);
                
                obsModel.reset(obsModelTrainer->train());
                
                this->mObsModel = obsModel;
                saveTrainedModel(trainedModelPath);
                
                // Set localizer
                localizer->observationModel(obsModel);                
            }
        }
        if (tDistribution >= 1) {
            this->mObsModel->normFunc = MathUtils::logProbatDistFunc(tDistribution);
        } else {
            this->mObsModel->normFunc = MathUtils::logProbaNormal;
        }

        if (considerBias) {
            // ObservationDependentInitializer
            std::shared_ptr<MetropolisSampler<State, Beacons>> obsDepInitializer(new MetropolisSampler<State, Beacons>());
            MetropolisSampler<State, Beacons>::Parameters msParams;
            obsDepInitializer->observationModel(mObsModel);
            obsDepInitializer->statusInitializer(statusInitializer);
            msParams.burnIn = 1000;
            msParams.radius2D = 10; // 10[m]
            msParams.interval = 1;
            msParams.withOrdering = true;
            obsDepInitializer->parameters(msParams);
            localizer->observationDependentInitializer(obsDepInitializer);
        }        
        
        // Set beacon filter
        std::shared_ptr<CleansingBeaconFilter> cleansingFilter(new CleansingBeaconFilter());
        std::shared_ptr<StrongestBeaconFilter> beaconFilter(new StrongestBeaconFilter());
        beaconFilter->nStrongest(10);
        
        std::shared_ptr<BeaconFilterChain> filterChain(new BeaconFilterChain());
        filterChain->addFilter(cleansingFilter);
        filterChain->addFilter(beaconFilter);
        
        localizer->beaconFilter(filterChain);
        
        // Set sampler
        if(usesObservationDependentInitializer){
            MetropolisSampler<State, Beacons>::Parameters metroParams;
            metroParams.withOrdering=true;
            std::shared_ptr<MetropolisSampler<State,Beacons>> metro(new MetropolisSampler<State,Beacons>());
            metro->parameters(metroParams);
            metro->observationModel(obsModel);
            metro->statusInitializer(statusInitializer);
            localizer->observationDependentInitializer(metro);
        }
        
        // set mixing rate
        StreamParticleFilter::MixtureParameters mixParams;
        mixParams.mixtureProbability = mixProbability;
        localizer->mixtureParameters(mixParams);
        
        return localizer;
    }
    
}
