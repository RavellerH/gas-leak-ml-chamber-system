#ifndef __NeuralNetwork__
#define __NeuralNetwork__

#include <stdint.h>

namespace tflite
{
    template <unsigned int tOpCount>
    class MicroMutableOpResolver;
    class ErrorReporter;
    class Model;
    class MicroInterpreter;
} // namespace tflite

struct TfLiteTensor;

struct GasLeakPrediction
{
    int gas_type;
    float gas_confidence;
    bool leak_present;
    float leak_probability;
    int severity;
    float severity_confidence;
    float ppm_estimate;
};

class NeuralNetwork
{
private:
    tflite::MicroMutableOpResolver<10> *resolver;
    tflite::ErrorReporter *error_reporter;
    const tflite::Model *model;
    tflite::MicroInterpreter *interpreter;
    TfLiteTensor *input;
    TfLiteTensor *gasOutput;
    TfLiteTensor *leakOutput;
    TfLiteTensor *severityOutput;
    TfLiteTensor *ppmOutput;
    uint8_t *tensor_arena;

    bool initialized;

public:
    NeuralNetwork();
    ~NeuralNetwork();
    float *getInputBuffer();
    bool predict(GasLeakPrediction &prediction);
    bool isInitialized();
};

#endif
