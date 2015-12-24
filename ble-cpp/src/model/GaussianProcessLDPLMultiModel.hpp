/*******************************************************************************
 * Copyright (c) 2014, 2015  IBM Corporation and others
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

#ifndef GaussianProcessLDPLMultiModel_hpp
#define GaussianProcessLDPLMultiModel_hpp

#include <stdio.h>
#include <cmath>

#include <boost/bimap/bimap.hpp>

#include "bleloc.h"
#include "KernelFunction.hpp"
#include "GaussianProcess.hpp"
#include "ObservationModel.hpp"
#include "ObservationModelTrainer.hpp"

namespace loc{
    
    /**
     ITUModelFunction
     **/
    
    class ITUModelFunction{
    
    private:
        double distanceOffset_=1.0;
    public:
        static const int ndim_ = 4;
        int ndim(){return ndim_;}
        
        ITUModelFunction& distanceOffset(double distanceOffset);
        void transformFeature(const Location& stateReceiver, const Location& stateTransmitter, double features[]) const;
        double predict(const double parameters[], const double features[]) const;
        
        template<class Archive>
        void serialize(Archive& ar);
    };
    
    
    /**
      GaussianProcess based model
     **/
    struct GaussianProcessLDPLMultiModelParameters{
        // Parameters for optimization
        int maxIteration_ = 10000;
        double tolranceOptimization_ = 0.00001;
        
        std::vector<double> lambdas{1000.0, 0.001, 1000, 1000};
        std::vector<double> rhos{0, 0, 100, 100};
    };
    
    template<class Tstate, class Tinput>
    class GaussianProcessLDPLMultiModel;
    
    template<class Tstate, class Tinput>
    class GaussianProcessLDPLMultiModelTrainer;
    
    template<class Tstate, class Tinput>
    class GaussianProcessLDPLMultiModel : public ObservationModel<Tstate, Tinput>{
    private:

        GaussianProcessLDPLMultiModelParameters trainParams;
        
        //std::shared_ptr<KernelFunction> mKernel;
        BLEBeacons mBLEBeacons;
        
        ITUModelFunction mITUModel;
        std::vector<std::vector<double>> mITUParameters;
        
        GaussianProcess mGP;
        std::map<long, int> mBeaconIdIndexMap;
        //boost::bimaps::bimap<long, int> mBeaconIdIndexBimap;
        std::vector<double> mRssiStandardDeviations;
        double mNormalRssiStandardDeviation;
        double computeNormalStandardDeviation(std::vector<double> standardDeviations);
        
        // Private function to train the model
        //GaussianProcessLDPLMultiModel& kernelFunction(std::shared_ptr<KernelFunction> kernel);
        GaussianProcessLDPLMultiModel& bleBeacons(BLEBeacons bleBeacons);
        GaussianProcessLDPLMultiModel& train(Samples samples);
        std::vector<std::vector<double>> fitITUModel(Samples samples);
        std::vector<double> computeRssiStandardDeviations(Samples samples);

        friend class GaussianProcessLDPLMultiModelTrainer<Tstate, Tinput>;
        
    public:
        GaussianProcessLDPLMultiModel() = default;
        ~GaussianProcessLDPLMultiModel() = default;
        
        std::vector<Tstate>* update(const std::vector<Tstate> & states, const Tinput & input) override {
            std::cout << "GaussianProcessLDPLMultiModel::update is not supported." << std::endl;
            std::vector<Tstate>* statesCopy = new std::vector<Tstate>(states);
            return statesCopy;
        }
        
        Tinput convertInput(const Tinput& input);
        double computeLogLikelihood(const Tstate& state, const Tinput& input);
        std::vector<double> computeLogLikelihood(const std::vector<Tstate> & states, const Tinput& input) override;
        
        template<class Archive>
        void save(Archive& ar) const;
        template<class Archive>
        void load(Archive& ar);
        
        void save(std::ofstream& ofs) const;
        void load(std::ifstream& ifs);

    };
    
    
    class MLAdapter{
    public:
        static void locationToVec(const Location& location, double x[]){
            x[0] = location.x();
            x[1] = location.y();
            x[2] = location.z();
            x[3] = location.floor();
        }
    };
    
    /**
     Definition of trainer
     **/
    template<class Tstate, class Tinput>
    class GaussianProcessLDPLMultiModelTrainer : public ObservationModelTrainer<Tstate, Tinput>{
        
    public:
        ~GaussianProcessLDPLMultiModelTrainer() override {}
        GaussianProcessLDPLMultiModel<Tstate, Tinput>* train() override;
        GaussianProcessLDPLMultiModelTrainer<Tstate, Tinput>& dataStore(std::shared_ptr<DataStore> dataStore) override{
            mDataStore = dataStore;
            return *this;
        }
    private:
        std::shared_ptr<DataStore> mDataStore;
    };
    
}
#endif /* GaussianProcessLDPLMultiModel_hpp */