#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    1.4762589523809524, // MQ135V
    1.301352361904762, // MQ2V
    0.5292313841269841, // MQ3V
    1.1359096571428573, // MQ4V
    1.081242053968254, // MQ7V
    0.03964641904761904, // MQ5V
    1.1468009714285714, // MQ6V
    1.1792261238095239, // MQ8V
};

const float feature_stds[8] = {
    0.9859975658727453, // MQ135V
    0.8994110130521146, // MQ2V
    0.48160444045871387, // MQ3V
    0.8007586241402427, // MQ4V
    0.7614169499880378, // MQ7V
    0.009123740471922812, // MQ5V
    0.8181170229192963, // MQ6V
    0.8313144750945884, // MQ8V
};
