#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    0.7739941746031747, // MQ135V
    0.7034832825396825, // MQ2V
    0.5320859873015873, // MQ3V
    0.9959208126984127, // MQ4V
    0.8565440984126984, // MQ7V
    1.5986619809523808, // MQ5V
    0.8132963555555556, // MQ6V
    2.3260593015873017, // MQ8V
};

const float feature_stds[8] = {
    0.637401338192865, // MQ135V
    0.4948857756054705, // MQ2V
    0.4477629724967056, // MQ3V
    0.7016630622678963, // MQ4V
    0.604648481854406, // MQ7V
    0.5228515635515172, // MQ5V
    0.5723295885308394, // MQ6V
    0.3542854495264784, // MQ8V
};
