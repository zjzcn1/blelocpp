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

#include "GaussianProcessLDPLMultiModel.hpp"
#include "ArrayUtils.hpp"
#include "SerializeUtils.hpp"
#include "DataLogger.hpp"
//#include "ExtendedDataUtils.hpp"

namespace loc{
    /**
     ITUModelFunction
     **/
    ITUModelFunction& ITUModelFunction::distanceOffset(double distanceOffset){
        distanceOffset_ = distanceOffset;
        return *this;
    }
    
    /*
    void ITUModelFunction::transformFeature(const Location& stateReceiver, const Location& stateTransmitter, double feats[ndim_]) const{
        
        double distOffsetTmp = distanceOffset_;
        double dist = Location::distance(stateReceiver, stateTransmitter, distOffsetTmp);
        double floorDiff = Location::floorDifference(stateReceiver, stateTransmitter);
        
        feats[0] = -10.0*log10(dist);
        feats[1] = 1.0;

        if(floorDiff<1){
            feats[2] = 0.0;
            feats[3] = 0.0;
        }else{
            feats[2] = -floorDiff;
            feats[3] = -1.0;
        }
    }
    */
    
    std::vector<double> ITUModelFunction::transformFeature(const Location& stateReceiver, const Location& stateTransmitter) const{
        std::vector<double> feats(ndim_);
        double distOffsetTmp = distanceOffset_;
        double dist = Location::distance(stateReceiver, stateTransmitter, distOffsetTmp);
        double floorDiff = Location::floorDifference(stateReceiver, stateTransmitter);
        
        feats[0] = -10.0*log10(dist);
        feats[1] = 1.0;
        if(floorDiff<1){
            feats[2] = 0.0;
            feats[3] = 0.0;
        }else{
            feats[2] = -floorDiff;
            feats[3] = -1.0;
        }
        return feats;
    }
    
    double ITUModelFunction::predict(const double *parameters, const double *features) const{
        double ypred = 0;
        for(int i=0; i<ndim_; i++){
            ypred += parameters[i]*features[i];
        }
        ypred = ypred<BeaconConfig::minRssi() ? BeaconConfig::minRssi() : ypred;
        return ypred;
    }
    
    double ITUModelFunction::predict(const std::vector<double>& parameters, const std::vector<double>& features) const{
        return predict(parameters.data(), features.data());
    }
    
    
    template<class Archive>
    void ITUModelFunction::serialize(Archive& ar){
        ar(CEREAL_NVP(distanceOffset_));
    }
    
    template void ITUModelFunction::serialize<cereal::JSONInputArchive> (cereal::JSONInputArchive& archive);
    template void ITUModelFunction::serialize<cereal::JSONOutputArchive> (cereal::JSONOutputArchive& archive);
    
    
    /**
     Implementation of GaussianProcessLDPLMultiModel
     **/
    
    template<class Tstate, class Tinput>
    GaussianProcessLDPLMultiModel<Tstate, Tinput>& GaussianProcessLDPLMultiModel<Tstate, Tinput>::
    bleBeacons(BLEBeacons bleBeacons){
        mBLEBeacons = bleBeacons;
        // construct beacon id to index map
        mBeaconIdIndexMap = BLEBeacon::constructBeaconIdToIndexMap(mBLEBeacons);
        
        for(const auto& bleBeacon:mBLEBeacons){
            long id = bleBeacon.id();
            mITUModelMap[id] = ITUModelFunction();
        }
        return *this;
    }
    
    /*
    template<class Tstate, class Tinput>
    GaussianProcessLDPLMultiModel<Tstate, Tinput>& GaussianProcessLDPLMultiModel<Tstate, Tinput>::kernelFunction(std::shared_ptr<KernelFunction> kernel){
        mKernel = kernel;
        return *this;
    }
    */
    
    template<class Tstate, class Tinput>
    std::vector<std::vector<double>> GaussianProcessLDPLMultiModel<Tstate, Tinput>::fitITUModel(Samples samples){
        std::vector<Sample> samplesAveraged = Sample::mean(Sample::splitSamplesToConsecutiveSamples(samples)); // averaging consecutive samples
        std::cout << "#samplesAveraged = " << samplesAveraged.size() << std::endl;
        if(samplesAveraged.size()==0){
            throw std::runtime_error("No valid sample [samplesAveraged.size()==0]");
        }
        
        // convert samples to X, Y matrices
        size_t n = samplesAveraged.size();
        size_t m = mBeaconIdIndexMap.size();
        static const int ndim = ITUModelFunction::ndim_;
        Eigen::MatrixXd X(n, ndim);
        Eigen::MatrixXd Y(n, m);
        Eigen::MatrixXd Actives(n,m);
        
        bool usesMinRssiObs = false;
        
        for(int i=0; i<n; i++){
            Sample smp = samplesAveraged.at(i);
            Location loc = smp.location();
            Beacons beacons = smp.beacons();
            // convert to X
            X.row(i) << loc.x(), loc.y(), loc.z(), loc.floor();
            // convert to Y
            // initialize rssi values by minRssi
            for(int j=0; j<m; j++){
                Y(i, j) = BeaconConfig::minRssi();
                Actives(i,j) = 0.0;
            }
            // Assign active rssi values to Y matrix.
            for(Beacon b: beacons){
                long id = b.id();
                int index = mBeaconIdIndexMap.at(id);
                Y(i, index) = b.rssi();
                // Active matrix
                if(usesMinRssiObs){
                    Actives(i,index) = 1.0;
                }
                else{
                    if(BeaconConfig::checkInRssiRange(b)){
                        Actives(i,index) = 1.0;
                    }
                }
            }
        }
        
        // Fit ITU mean parameter
        Eigen::VectorXd params0;
        {
            // count the number of active observation values
            int nActive = 0;
            for(int i=0; i<samplesAveraged.size(); i++){
                Beacons bs = samplesAveraged.at(i).beacons();
                for(Beacon b: bs){
                    if(usesMinRssiObs){
                        nActive++;
                    }
                    else if(BeaconConfig::checkInRssiRange(b)){
                        nActive++;
                    }
                }
            }
            // least square estimation
            Eigen::MatrixXd Phi(nActive,ndim);
            Eigen::VectorXd Ytmp(nActive);
            nActive = 0;
            for(int i=0; i<samplesAveraged.size(); i++){
                Sample smp = samplesAveraged.at(i);
                Location loc = smp.location();
                Beacons bs = smp.beacons();
                for(Beacon b: bs){
                    bool bIsActive = false;
                    if(usesMinRssiObs){
                        bIsActive = true;
                    } else if(BeaconConfig::checkInRssiRange(b)){
                        bIsActive = true;
                    }
                    if(bIsActive){
                        long id = b.id();
                        int index = mBeaconIdIndexMap.at(id);
                        BLEBeacon bleBeacon = mBLEBeacons.at(index);
                        auto features = mITUModelMap[id].transformFeature(loc, bleBeacon);
                        
                        for(int j= 0; j<ndim;j++){
                            Phi(nActive, j) = features[j];
                        }
                        Ytmp(nActive) = b.rssi();
                        nActive++;
                    }
                }
            }
            Eigen::MatrixXd A = (Phi.transpose()*Phi).array();
            Eigen::VectorXd b = Phi.transpose()*Ytmp;
            params0 = A.colPivHouseholderQr().solve(b);
        }
        std::cout << "Initial value of ITU parameters = " << params0.transpose() <<std::endl;
        
        // Fit parameters for each BLE beacon
        std::vector<std::vector<double>> ITUParameters(m);
        {
            // Prepare matrices
            std::vector<Eigen::MatrixXd> Xmats(m);
            std::vector<Eigen::VectorXd> Ymats(m);
            Eigen::VectorXd lambdavec = ArrayUtils::vectorToEigenVector(trainParams.lambdas);
            Eigen::VectorXd rhovec = ArrayUtils::vectorToEigenVector(trainParams.rhos);
            
            Eigen::MatrixXd Lambdamat = lambdavec.asDiagonal();
            Eigen::MatrixXd Rhomat = rhovec.asDiagonal();
            
            // convert to feature
            for(int j=0; j<m; j++){
                Eigen::MatrixXd Xmat(n, ndim);
                Eigen::VectorXd Ymat(n);
                BLEBeacon bleBeacon = mBLEBeacons.at(j);
                for(int i=0; i<n; i++){
                    long id = bleBeacon.id();
                    Location loc(X(i,0), X(i,1), X(i,2), X(i,3));
                    auto features = mITUModelMap[id].transformFeature(loc, bleBeacon);
                    for(int k = 0; k<ndim;k++){
                        Xmat(i, k) = features[k];
                    }
                    Ymat(i) = Y(i,j);
                }
                Xmats[j] = Xmat;
                Ymats[j] = Ymat;
            }
            
            // iteration
            Eigen::MatrixXd paramsMatrix(m,ndim);
            Eigen::VectorXd activevec(n);
            // initialize parameters
            for(int j=0; j<m; j++){
                paramsMatrix.row(j) = params0;
            }
            bool wasConverged = false;
            for(int k=0; k<trainParams.maxIteration_; k++){
                // Update parameters for each beacon
                for(int j=0; j<m; j++){
                    Eigen::VectorXd paramsTmp = paramsMatrix.row(j);
                    if(Xmats.at(j).rows()>0){
                        for(int i=0; i<n; i++){
                            double ypred = Xmats.at(j).row(i)*paramsTmp;
                            if(BeaconConfig::minRssi()<ypred){
                                activevec(i)=1.0;
                            }else{
                                activevec(i)=0.0;
                            }
                        }
                        Eigen::MatrixXd A = Xmats.at(j).transpose()*(activevec.asDiagonal())*Xmats.at(j) + Lambdamat;
                        Eigen::VectorXd b = Xmats.at(j).transpose()*(activevec.asDiagonal())*Ymats.at(j) + Lambdamat*params0;
                        paramsTmp = A.colPivHouseholderQr().solve(b);
                    }else{
                        paramsTmp = paramsMatrix.row(j);
                    }
                    paramsMatrix.row(j) = paramsTmp;
                }
                {
                    // Update mean ITU parameters;
                    Eigen::VectorXd paramsMean(ndim);
                    for(int j=0; j<ndim; j++){
                        paramsMean(j) = paramsMatrix.col(j).mean();
                    }
                    Eigen::MatrixXd A = Lambdamat + Rhomat;
                    Eigen::VectorXd b = Lambdamat*paramsMean;
                    Eigen::VectorXd params0new = A.colPivHouseholderQr().solve(b);
                    Eigen::VectorXd diff = params0-params0new;
                    params0 = params0new;
                    //std::cout << "params0=" << params0;
                    //std::cout << ", diff.norm()=" << diff.norm() << std::endl;
                    if(diff.norm() < trainParams.tolranceOptimization_){
                        //std::cout << "Optimization loop is finished." << std::endl;
                        wasConverged = true;
                        break;
                    }
                }
            }
            if(!wasConverged){
                std::cout << "ITU parameters were not converged." << std::endl;
            }
            std::cout << "mean(parameters) = " << params0.transpose() << std::endl;
            //std::cout << "parameters = " << paramsMatrix << std::endl;
            for(auto & ble: mBLEBeacons){
                int index = mBeaconIdIndexMap.at(ble.id());
                std::cout << "parameters(" << ble.major() << "," << ble.minor() << ") = " <<paramsMatrix.row(index) << std::endl;
            }
            
            // Update ITUParameters by optimized parameters
            {
                for(int i=0; i<m; i++){
                    std::vector<double> params(ndim);
                    for( int j=0; j<ndim; j++){
                        params[j] = paramsMatrix(i, j);
                    }
                    ITUParameters[i] = params;
                }
            }
        }

        return ITUParameters;
    }
    
    
    template<class Tstate, class Tinput>
    GaussianProcessLDPLMultiModel<Tstate, Tinput>& GaussianProcessLDPLMultiModel<Tstate, Tinput>::train(Samples samples){
        std::vector<Sample> samplesAveraged = Sample::mean(Sample::splitSamplesToConsecutiveSamples(samples)); // averaging consecutive samples
        std::cout << "#samplesAveraged = " << samplesAveraged.size() << std::endl;
        
        // construct beacon id to index map
        if(mBLEBeacons.size() <= 0){
            BOOST_THROW_EXCEPTION(LocException("BLEBeacons have not been set to this instance."));
        }
        
        // convert samples to X, Y matrices
        size_t n = samplesAveraged.size();
        size_t m = mBeaconIdIndexMap.size();
        static const int ndim = ITUModelFunction::ndim_;
        Eigen::MatrixXd X(n, ndim);
        Eigen::MatrixXd Y(n, m);
        Eigen::MatrixXd Actives(n,m);
        
        bool usesMinRssiObs = true;
        
        // FIT ITU model parameters
        mITUParameters = fitITUModel(samples);
        
        for(int i=0; i<n; i++){
            Sample smp = samplesAveraged.at(i);
            Location loc = smp.location();
            Beacons beacons = smp.beacons();
            // convert to X
            X.row(i) << loc.x(), loc.y(), loc.z(), loc.floor();
            // convert to Y
            // initialize rssi values by minRssi
            for(int j=0; j<m; j++){
                Y(i, j) = BeaconConfig::minRssi();
                Actives(i,j) = 0.0;
            }
            // Assign active rssi values to Y matrix.
            for(Beacon b: beacons){
                long id = b.id();
                int index = mBeaconIdIndexMap.at(id);
                Y(i, index) = b.rssi();
                // Active matrix
                if(usesMinRssiObs){
                    Actives(i,index) = 1.0;
                }
                else{
                    if(BeaconConfig::checkInRssiRange(b)){
                        Actives(i,index) = 1.0;
                    }
                }
            }
        }
        
        // Compute dY = Y - m(X)
        Eigen::MatrixXd dY(n, m);
        for(int i=0; i<n; i++){
            Sample smp = samplesAveraged.at(i);
            Location loc = smp.location();
            for(int j=0; j<m; j++){
                BLEBeacon bleBeacon = mBLEBeacons.at(j);
                long id = bleBeacon.id();
                auto features = mITUModelMap[id].transformFeature(loc, bleBeacon);
                std::vector<double> params = mITUParameters.at(j);
                double ymean = mITUModelMap[id].predict(params, features);
                dY(i, j)=Y(i,j)-ymean;
            }
        }
        
        // Training with selection of kernel parameters
        mGP.fitCV(X, dY, Actives);
        
        // Estimate variance parameter (sigma_n) by using raw (=not averaged) data
        mRssiStandardDeviations = computeRssiStandardDeviations(samples);
        for(auto& ble: mBLEBeacons){
            long id = ble.id();
            int index = mBeaconIdIndexMap.at(id);
            std::cout << "stdev(" <<ble.major() << "," << ble.minor() << ") = " << mRssiStandardDeviations.at(index) <<std::endl;
        }
        
        if(mStdevRssiForUnknownBeacon==0){
            mStdevRssiForUnknownBeacon = computeNormalStandardDeviation(mRssiStandardDeviations);
        }
        
        return *this;
    }
    
    // compute standard deviation of RSSI for each ble beacon
    template<class Tstate, class Tinput>
    std::vector<double> GaussianProcessLDPLMultiModel<Tstate, Tinput>::computeRssiStandardDeviations(Samples samples){
        
        std::map<int, int> indexCount;
        std::map<int, double> indexRssiSum;
        for(auto iter=mBeaconIdIndexMap.begin(); iter!=mBeaconIdIndexMap.end(); iter++){
            int index = iter->second;
            indexCount[index] = 0;
            indexRssiSum[index] = 0;
        }
        
        for(Sample smp: samples){
            Location loc = smp.location();
            Beacons bs = smp.beacons();
            
            std::vector<double> xvec = MLAdapter::locationToVec(loc);
            std::vector<int> indices = extractKnownBeaconIndices(bs);
            std::vector<double> dypreds = mGP.predict(xvec.data(), indices);
            
            int i = 0;
            for(const Beacon& b: bs){
                long id = b.id();
                int index = mBeaconIdIndexMap.at(id);
                BLEBeacon ble = mBLEBeacons.at(index);
                std::vector<double> features = mITUModelMap[id].transformFeature(loc, ble);
                std::vector<double> params = mITUParameters.at(index);
                double mean = mITUModelMap[id].predict(params, features);
                
                double dypred = dypreds.at(i);
                double ypred = mean + dypred;
                double rssi = b.rssi();
                double difference = rssi - ypred;
                
                indexCount[index] += 1;
                indexRssiSum[index] += difference*difference;
                
                i++;
            }
            
        }
        
        std::vector<double> stdevs;
        for(auto& ble: mBLEBeacons){
            long id = ble.id();
            int index = mBeaconIdIndexMap.at(id);
            double var = indexRssiSum[index] /(indexCount[index]);
            if (isnan(var)) {
                std::cerr << "Stdev is NaN for beacon(" << ble.major() << ", " << ble.minor() << ")" << std::endl;
            }
            double stdev = sqrt(var);
            stdevs.push_back(stdev);
        }

        return stdevs;
    }
    
    template<class Tstate, class Tinput>
    double GaussianProcessLDPLMultiModel<Tstate, Tinput>::computeNormalStandardDeviation(std::vector<double> standardDeviations){
        double sq = 0;
        for(double stdev: standardDeviations){
            sq += stdev*stdev;
        }
        sq/=(standardDeviations.size());
        return std::sqrt(sq);
    }
    
    
    template<class Tstate, class Tinput>
    Tinput GaussianProcessLDPLMultiModel<Tstate, Tinput>::convertInput(const Tinput& input){
        Tinput inputConverted;
        for(auto iter=input.begin(); iter!=input.end(); iter++){
            long id = iter->id();
            if(mBeaconIdIndexMap.count(id)==1){
                inputConverted.push_back(*iter);
            }
        }
        return inputConverted;
    }
    
    template<class Tstate, class Tinput>
    std::vector<int> GaussianProcessLDPLMultiModel<Tstate, Tinput>::extractKnownBeaconIndices(const Tinput& input) const{
        std::vector<int> indices;
        for(auto iter=input.begin(); iter!=input.end(); iter++){
            Beacon b = *iter;
            long id = b.id();
            if(mBeaconIdIndexMap.count(id)==1){
                int index = mBeaconIdIndexMap.at(id);
                indices.push_back(index);
            }
        }
        return indices;
    }
    
    template<class Tstate, class Tinput>
    std::map<long, NormalParameter>  GaussianProcessLDPLMultiModel<Tstate, Tinput>::predict(const Tstate& state, const Tinput& input) const{
        //Assuming Tinput = Beacons
        std::map<long, NormalParameter> beaconIdRssiStatsMap;
        
        std::vector<double> xvec = MLAdapter::locationToVec(state);
        std::vector<int> indices = extractKnownBeaconIndices(input);
        std::vector<double> dypreds = mGP.predict(xvec.data(), indices);
        
        int idx_local=0;
        for(auto iter=input.begin(); iter!=input.end(); iter++){
            auto b = *iter;
            long id = b.id();
            // RSSI of known beacons are predicted by a model.
            if(mBeaconIdIndexMap.count(id)==1){
                int idx_global = mBeaconIdIndexMap.at(id);
                const BLEBeacon& bleBeacon = mBLEBeacons.at(idx_global);
                
                const auto& ituModel = mITUModelMap.at(id);
                const auto& features = ituModel.transformFeature(state, bleBeacon);
                const auto& params = mITUParameters.at(idx_global);
                double mean = ituModel.predict(params, features);
                double dypred = dypreds.at(idx_local);
                
                double ypred = mean + dypred;
                double stdev = mRssiStandardDeviations[idx_global];
                
                if(mCoeffDiffFloorStdev!=1.0 && Location::checkDifferentFloor(state, bleBeacon)){
                    stdev = stdev*mCoeffDiffFloorStdev ;
                }
                
                NormalParameter rssiStats(ypred, stdev);
                beaconIdRssiStatsMap[id] = rssiStats;
                
                idx_local++;
            }
            
        }
        return beaconIdRssiStatsMap;
    }
    
    
    template<class Tstate, class Tinput>
    double GaussianProcessLDPLMultiModel<Tstate, Tinput>::computeLogLikelihood(const Tstate& state, const Tinput& input){
        std::vector<double> values = this->computeLogLikelihoodRelatedValues(state, input);
        return values.at(0);
    }
    
    template<class Tstate, class Tinput>
    std::vector<double> GaussianProcessLDPLMultiModel<Tstate, Tinput>::computeLogLikelihoodRelatedValues(const Tstate& state, const Tinput& input){
        //Assuming Tinput = Beacons
        
        std::vector<double> returnValues(4); // logLikelihood, mahalanobisDistance, #knownBeacons, #unknownBeacons
        
        auto beaconIdRssiStatsMap = this->predict(state, input);
        
        std::vector<int> indices = extractKnownBeaconIndices(input);
        
        size_t countKnown = indices.size();
        size_t countUnknown = input.size() - indices.size();

        if(countKnown==0){
            std::cout << "ObservationModel does not know the input data." << std::endl;
        }
        
        double jointLogLL = 0;
        double sumMahaDist = 0;
        int i=0;
        for(auto iter=input.begin(); iter!=input.end(); iter++){
            const Beacon& b = *iter;
            double rssi = b.rssi();
            
            const State* pState = dynamic_cast<const State*>(&state);
            if(pState){
                double rssiBias = pState->rssiBias();
                rssi = rssi - rssiBias;
            }
            long id = b.id();
            
            // RSSI of known beacons are predicted by a model.
            if(mBeaconIdIndexMap.count(id)==1){
                auto rssiStats = beaconIdRssiStatsMap[id];
                double ypred = rssiStats.mean();
                double stdev = rssiStats.stdev();
                
                //double logLL = MathUtils::logProbaNormal(rssi, ypred, stdev);
                double logLL = normFunc(rssi, ypred, stdev);
                double mahaDist = MathUtils::mahalanobisDistance(rssi, ypred, stdev);
                
                jointLogLL += logLL;
                sumMahaDist += mahaDist;
                
                i++;
            }
            // RSSI of unknown beacons are assumed to be minRssi.
            else if(mFillsUnknownBeaconRssi){
                double ypred = BeaconConfig::minRssi();
                double stdev = mStdevRssiForUnknownBeacon;
                
                //double logLL = MathUtils::logProbaNormal(rssi, ypred, stdev);
                double logLL = normFunc(rssi, ypred, stdev);
                double mahaDist = MathUtils::mahalanobisDistance(rssi, ypred, stdev);
                
                jointLogLL += logLL;
                sumMahaDist += mahaDist;
            }
        }
        returnValues[0] = jointLogLL;
        returnValues[1] = sumMahaDist;
        returnValues[2] = countKnown;
        returnValues[3] = countUnknown;
        
        return returnValues;
    }
    
    template<class Tstate, class Tinput>
    std::vector<double> GaussianProcessLDPLMultiModel<Tstate, Tinput>::computeLogLikelihood(const std::vector<Tstate> & states, const Tinput & input) {
        int n = (int) states.size();
        std::vector<double> logLLs(n);
        for(int i=0; i<n; i++){
            logLLs[i] = this->computeLogLikelihood(states.at(i), input);
        }
        return logLLs;
    }
    
    template<class Tstate, class Tinput>
    std::vector<std::vector<double>> GaussianProcessLDPLMultiModel<Tstate, Tinput>::computeLogLikelihoodRelatedValues(const std::vector<Tstate> & states, const Tinput & input) {
        int n = (int) states.size();
        std::vector<double> logLLs(n);
        
        std::vector<std::vector<double>> values(n);
        for(int i=0; i<n; i++){
            values[i] = this->computeLogLikelihoodRelatedValues(states.at(i), input);
        }
        return values;
    }
    
    template<class Tstate, class Tinput>
    GaussianProcessLDPLMultiModel<Tstate, Tinput>& GaussianProcessLDPLMultiModel<Tstate, Tinput>::fillsUnknownBeaconRssi(bool fills){
        mFillsUnknownBeaconRssi = fills;
        return *this;
    }
    
    template<class Tstate, class Tinput>
    bool GaussianProcessLDPLMultiModel<Tstate, Tinput>::fillsUnknownBeaconRssi() const{
        return mFillsUnknownBeaconRssi;
    }
    
    template<class Tstate, class Tinput>
    GaussianProcessLDPLMultiModel<Tstate, Tinput>& GaussianProcessLDPLMultiModel<Tstate, Tinput>::rssiStandardDeviationForUnknownBeacons(double stdevRssi){
        mStdevRssiForUnknownBeacon = stdevRssi;
        return *this;
    }
    
    template<class Tstate, class Tinput>
    double GaussianProcessLDPLMultiModel<Tstate, Tinput>::rssiStandardDeviationForUnknownBeacons() const{
        return mStdevRssiForUnknownBeacon;
    }
    
    template<class Tstate, class Tinput>
    GaussianProcessLDPLMultiModel<Tstate, Tinput>& GaussianProcessLDPLMultiModel<Tstate, Tinput>::coeffDiffFloorStdev(double coeff){
        mCoeffDiffFloorStdev = coeff;
        return *this;
    }
    
    // CEREAL function
    template<class Tstate, class Tinput>
    template<class Archive>
    void GaussianProcessLDPLMultiModel<Tstate, Tinput>::save(Archive& ar) const{
        //ar(CEREAL_NVP(mKernel));
        ar(CEREAL_NVP(version));
        
        ar(CEREAL_NVP(mBLEBeacons));
        ar(CEREAL_NVP(mITUModelMap));
        ar(CEREAL_NVP(mITUParameters));
        
        ar(CEREAL_NVP(mGP));
        ar(CEREAL_NVP(mRssiStandardDeviations));
    }
    
    template<class Tstate, class Tinput>
    template<class Archive>
    void GaussianProcessLDPLMultiModel<Tstate, Tinput>::load(Archive& ar){
        //ar(CEREAL_NVP(mKernel));
        ar(CEREAL_NVP(mBLEBeacons));
        
        try{
            ar(CEREAL_NVP(version));
        }catch(cereal::Exception& e){
            version = 0;
        }
        if(version==0){
            ITUModelFunction mITUModel;
            ar(CEREAL_NVP(mITUModel));
            for(const auto& bleBeacon:mBLEBeacons){
                long id = bleBeacon.id();
                mITUModelMap[id] = mITUModel;
            }
        }else{
            ar(CEREAL_NVP(mITUModelMap));
        }
            
        ar(CEREAL_NVP(mITUParameters));
        ar(CEREAL_NVP(mGP));
        ar(CEREAL_NVP(mRssiStandardDeviations));
        mBeaconIdIndexMap = BLEBeacon::constructBeaconIdToIndexMap(mBLEBeacons);
        mStdevRssiForUnknownBeacon = computeNormalStandardDeviation(mRssiStandardDeviations);
    }
    
    //explicit instantiation
    template void GaussianProcessLDPLMultiModel<State, Beacons>::load<cereal::JSONInputArchive>(cereal::JSONInputArchive& archive);
    template void GaussianProcessLDPLMultiModel<State, Beacons>::save<cereal::JSONOutputArchive>(cereal::JSONOutputArchive& archive) const;
    
    
    template<class Tstate, class Tinput>
    void GaussianProcessLDPLMultiModel<Tstate, Tinput>::save(std::ofstream& ofs) const{
        cereal::JSONOutputArchive oarchive(ofs);
        oarchive(*this);
    }
    
    template<class Tstate, class Tinput>
    void GaussianProcessLDPLMultiModel<Tstate, Tinput>::save(std::ostringstream& oss) const{
        cereal::JSONOutputArchive oarchive(oss);
        oarchive(*this);
    }
    
    template<class Tstate, class Tinput>
    void GaussianProcessLDPLMultiModel<Tstate, Tinput>::load(std::ifstream& ifs){
        cereal::JSONInputArchive iarchive(ifs);
        iarchive(*this);
    }

    template<class Tstate, class Tinput>
    void GaussianProcessLDPLMultiModel<Tstate, Tinput>::load(std::istringstream& iss){
        cereal::JSONInputArchive iarchive(iss);
        iarchive(*this);
    }
    
    
    /**
     Implementation of GaussianProcessLDPLMultiModelTrainer
     **/
    template<class Tstate, class Tinput>
    GaussianProcessLDPLMultiModel<Tstate, Tinput>* GaussianProcessLDPLMultiModelTrainer<Tstate, Tinput>::train(){
        
        Samples samples = mDataStore->getSamples();
        BLEBeacons bleBeacons = mDataStore->getBLEBeacons();
        if(bleBeacons.size()<=0){
            BOOST_THROW_EXCEPTION(LocException("BLEBeacons have not been set to dataStore."));
        }
        Samples samplesFiltered;
        try{
            samplesFiltered = Sample::filterUnregisteredBeacons(samples, bleBeacons);
        }catch(LocException& ex) {
            throw ex;
        }
        GaussianProcessLDPLMultiModel<Tstate, Tinput>* obsModel = new GaussianProcessLDPLMultiModel<Tstate, Tinput>();
        obsModel->bleBeacons(bleBeacons);
        obsModel->train(samplesFiltered);
        
        return obsModel;
    }
    
    
    //Explicit instantiation
    template class GaussianProcessLDPLMultiModel<State, Beacons>;
    template class GaussianProcessLDPLMultiModelTrainer<State, Beacons>;
    
}
