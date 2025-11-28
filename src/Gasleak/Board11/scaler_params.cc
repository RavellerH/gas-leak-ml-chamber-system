#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    0.6311325380952382, // MQ135V
    0.03488164285714286, // MQ2V
    0.6044549761904763, // MQ3V
    0.572445080952381, // MQ4V
    0.5978266761904761, // MQ7V
    0.501043880952381, // MQ5V
    0.035675123809523805, // MQ6V
    0.5282970571428571, // MQ8V
};

const float feature_stds[8] = {
    0.6298812770155302, // MQ135V
    0.007528124045146682, // MQ2V
    0.6033318000860558, // MQ3V
    0.570909154757791, // MQ4V
    0.5965702969641749, // MQ7V
    0.2783427064766265, // MQ5V
    0.005454404212054762, // MQ6V
    0.528655686858079, // MQ8V
};
