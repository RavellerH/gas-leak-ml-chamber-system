#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    1.3513906444444443, // MQ135V
    1.1140197238095237, // MQ2V
    0.6178702730158729, // MQ3V
    0.45814606984126977, // MQ4V
    1.2045301365079366, // MQ7V
    0.03835588888888889, // MQ5V
    1.044794761904762, // MQ6V
    0.9064623555555555, // MQ8V
};

const float feature_stds[8] = {
    0.9479378026781403, // MQ135V
    0.7818868749651521, // MQ2V
    0.5056660716738457, // MQ3V
    0.4506963559096906, // MQ4V
    0.8522247755867218, // MQ7V
    0.006752426949731767, // MQ5V
    0.7384325120567712, // MQ6V
    0.8437297953837353, // MQ8V
};
