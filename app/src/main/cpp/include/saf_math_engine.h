#pragma once

#include <cmath>
#include <algorithm>


struct SAFState {

    double gain;
    double deltaE;
    double metricNorm;
    double memory;
    
    double Gt;
    double lambda;
    double epsilon;

    SAFState():
        gain(1.0),
        deltaE(0.0),
        metricNorm(0.0),
        memory(0.0),
        Gt(1.0),
        lambda(0.05),
        epsilon(0.000001)
    {}
};


inline double SAFProjection(double x)
{
    return std::max(0.0,std::min(2.0,x));
}



inline double SAFUpdate(
        SAFState &s,
        double p,
        double target)
{

    double delta =
        target - p;


    double dEnergy =
        std::abs(delta);


    double normGt =
        s.Gt *
        delta *
        delta;


    s.memory =
        0.9*s.memory +
        0.1*dEnergy;



    double alpha =
        dEnergy /
        (
        dEnergy +
        normGt +
        s.lambda*s.memory +
        s.epsilon
        );


    double correction =
        alpha *
        (1.0/s.Gt) *
        delta;



    double phi =
        p + correction;


    phi =
        SAFProjection(phi);


    s.deltaE =
        dEnergy;

    s.metricNorm =
        normGt;

    s.gain =
        phi;


    return phi;
}
