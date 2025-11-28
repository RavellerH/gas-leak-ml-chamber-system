#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    1.151226426190476, // MQ135V
    0.9155299452380952, // MQ2V
    0.8747941047619049, // MQ3V
    0.849981730952381, // MQ4V
    0.8076531452380953, // MQ7V
    3.120307857142857, // MQ5V
    0.8551023309523809, // MQ6V
    1.0096667, // MQ8V
};

const float feature_stds[8] = {
    0.6836354136532563, // MQ135V
    0.8188832805994214, // MQ2V
    0.5358539933242062, // MQ3V
    0.7398564334928974, // MQ4V
    0.4721864452266815, // MQ7V
    0.4780387180480543, // MQ5V
    0.669980479449558, // MQ6V
    0.9062927429386844, // MQ8V
};
