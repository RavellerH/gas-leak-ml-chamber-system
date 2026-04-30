#ifndef MODEL_DATA_H
#define MODEL_DATA_H

#include <stddef.h>

extern const unsigned char model_tflite[];
extern const unsigned int model_tflite_len;

extern const float feature_means[8];
extern const float feature_stds[8];

#endif