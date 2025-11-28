#include "scaler_params.h"

// Your pre-calculated mean and standard deviation values from StandardScaler
const float feature_means[8] = {
    0.6938559357142856, // MQ135V
    0.592807092857143, // MQ2V
    0.6904387071428572, // MQ3V
    0.6569249928571429, // MQ4V
    0.6596193857142857, // MQ7V
    0.7429535214285715, // MQ5V
    0.7464565428571429, // MQ6V
    0.8050743285714286, // MQ8V
};

const float feature_stds[8] = {
    0.43135199276265934, // MQ135V
    0.38460203563000206, // MQ2V
    0.39776097133079985, // MQ3V
    0.3871451378935387, // MQ4V
    0.3988859468973482, // MQ7V
    0.4292369964209827, // MQ5V
    0.4417989119238747, // MQ6V
    0.4850650777435134, // MQ8V
};
