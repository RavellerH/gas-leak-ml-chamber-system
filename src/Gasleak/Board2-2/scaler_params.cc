#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    0.6498294761904763, // MQ135V
    1.2136539206349206, // MQ2V
    0.3180771396825397, // MQ3V
    0.7199682317460318, // MQ4V
    0.7600832031746032, // MQ7V
    0.5213950285714285, // MQ5V
    0.7026849365079365, // MQ6V
    0.5678650412698413, // MQ8V
};

const float feature_stds[8] = {
    0.9050757936196381, // MQ135V
    0.9017448975334872, // MQ2V
    0.349978149021761, // MQ3V
    0.8075848916169941, // MQ4V
    0.9411179737538079, // MQ7V
    0.6983488177213419, // MQ5V
    0.8269760910606303, // MQ6V
    0.776508990012212, // MQ8V
};
