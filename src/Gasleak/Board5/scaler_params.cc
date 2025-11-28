#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    1.3048342253968253, // MQ135V
    1.2941591904761904, // MQ2V
    0.5171919492063491, // MQ3V
    0.7323814984126984, // MQ4V
    0.685499980952381, // MQ7V
    0.6334275650793652, // MQ5V
    0.9311511079365079, // MQ6V
    1.1535201079365078, // MQ8V
};

const float feature_stds[8] = {
    0.9211165077236636, // MQ135V
    0.8430539500941264, // MQ2V
    0.4707189439620962, // MQ3V
    0.5169631783236206, // MQ4V
    0.476066530914378, // MQ7V
    0.5148404674786267, // MQ5V
    0.7310591862615313, // MQ6V
    0.7810278161666047, // MQ8V
};
