/*******************************************************************************
 * Copyright (c) 2014-2016  IBM Corporation and others
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

#ifndef ObservationDependentInitializer_hpp
#define ObservationDependentInitializer_hpp

#include "StatusInitializer.hpp"
#include "StatusInitializerImpl.hpp"
#include "ObservationModel.hpp"

namespace loc{
    
    template <class Tstate, class Tinput>
    class ObservationDependentInitializer{
        
    public:
        
        virtual ~ObservationDependentInitializer(){}
        
        virtual void observationModel(std::shared_ptr<ObservationModel<Tstate, Tinput>> obsModel) = 0;
        virtual void statusInitializer(std::shared_ptr<StatusInitializerImpl> statusInitializer) = 0;
        
        virtual void input(const Tinput& input) = 0;
        virtual void startBurnIn() = 0;
        virtual void startBurnIn(int n) = 0;
        virtual std::vector<Tstate> getAllStates() const = 0;
        virtual std::vector<double> getAllLogLLs() const = 0;
        
        virtual std::vector<Tstate> sampling(int n) = 0;
        virtual std::vector<Tstate> sampling(int n, const Location &location) = 0;
        
        virtual void print() const = 0;
        
    };
}
#endif /* ObservationDependentInitializer_hpp */
