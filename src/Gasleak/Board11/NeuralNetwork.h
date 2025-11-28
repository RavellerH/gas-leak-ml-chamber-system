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

class NeuralNetwork
{
private:
    tflite::MicroMutableOpResolver<10> *resolver;
    tflite::ErrorReporter *error_reporter;
    const tflite::Model *model;
    tflite::MicroInterpreter *interpreter;
    TfLiteTensor *input;
    TfLiteTensor *output;
    uint8_t *tensor_arena;

    int outputSize;
    bool initialized;

public:
    NeuralNetwork();
    ~NeuralNetwork();
    float *getInputBuffer();
    // Modified: predict now takes a float reference to return the confidence score
    int predict(float &confidence_score); // <--- MODIFIED LINE
    int getOutputSize();
    bool isInitialized();
    float *getOutputBuffer(); // Still useful if you want all scores later
};

#endif
