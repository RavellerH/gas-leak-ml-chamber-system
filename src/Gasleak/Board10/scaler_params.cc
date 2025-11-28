#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    1.1520345047619047, // MQ135V
    1.3109153365079362, // MQ2V
    0.7141227142857144, // MQ3V
    1.1211313746031746, // MQ4V
    1.243005177777778, // MQ7V
    1.071124361904762, // MQ5V
    1.0141550253968254, // MQ6V
    2.7127776507936505, // MQ8V
};

const float feature_stds[8] = {
    0.8141364126256764, // MQ135V
    0.925902293582627, // MQ2V
    0.4173033803621118, // MQ3V
    0.7919310478237633, // MQ4V
    0.7990835497624348, // MQ7V
    0.7564406863654612, // MQ5V
    0.7168992515406087, // MQ6V
    0.8210078849399248, // MQ8V
};
