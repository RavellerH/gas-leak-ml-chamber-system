#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    0.8113478888888889, // MQ135V
    0.9337725936507936, // MQ2V
    0.4120299682539683, // MQ3V
    0.4234810158730159, // MQ4V
    0.7259628476190476, // MQ7V
    0.44957947936507936, // MQ5V
    0.3869291238095238, // MQ6V
    0.7419927142857143, // MQ8V
};

const float feature_stds[8] = {
    0.5793286123288686, // MQ135V
    0.8359959123324764, // MQ2V
    0.4084227252358886, // MQ3V
    0.5960387866597299, // MQ4V
    1.0151374095797294, // MQ7V
    0.631572883747263, // MQ5V
    0.5469983938518777, // MQ6V
    1.0394672780712615, // MQ8V
};
