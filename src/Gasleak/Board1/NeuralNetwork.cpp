#include "NeuralNetwork.h"
#include "model_data.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include <cstring>

// Define a static tensor arena for better alignment and to avoid heap fragmentation
const int kArenaSize = 40 * 1024; // Adjust size as needed
uint8_t static_tensor_arena[kArenaSize] __attribute__((aligned(16))); // 16-byte alignment

NeuralNetwork::NeuralNetwork()
    : resolver(nullptr), error_reporter(nullptr), model(nullptr),
      interpreter(nullptr), input(nullptr), gasOutput(nullptr), leakOutput(nullptr),
      severityOutput(nullptr), ppmOutput(nullptr), tensor_arena(nullptr), initialized(false)
{
    error_reporter = new tflite::MicroErrorReporter();
    if (!error_reporter) {
        return;
    }

    model = tflite::GetModel(model_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        TF_LITE_REPORT_ERROR(error_reporter, "Model provided is schema version %d not equal to supported version %d.",
                            model->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    resolver = new tflite::MicroMutableOpResolver<10>();
    if (!resolver) {
        TF_LITE_REPORT_ERROR(error_reporter, "Could not allocate resolver");
        return;
    }

    resolver->AddFullyConnected();
    resolver->AddMul();
    resolver->AddAdd();
    resolver->AddLogistic();
    resolver->AddReshape();
    resolver->AddQuantize();
    resolver->AddDequantize();
    resolver->AddSoftmax();

    tensor_arena = static_tensor_arena;

    interpreter = new tflite::MicroInterpreter(
        model, *resolver, tensor_arena, kArenaSize, error_reporter);
    if (!interpreter) {
        TF_LITE_REPORT_ERROR(error_reporter, "Could not allocate interpreter");
        return;
    }

    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk)
    {
        TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
        return;
    }

    size_t used_bytes = interpreter->arena_used_bytes();
    TF_LITE_REPORT_ERROR(error_reporter, "Used bytes %d\n", used_bytes);

    input = interpreter->input(0);

    if (!input) {
        TF_LITE_REPORT_ERROR(error_reporter, "Input tensor not found.");
        return;
    }

    for (int i = 0; i < interpreter->outputs_size(); i++) {
        TfLiteTensor *candidate = interpreter->output(i);
        const char *name = candidate->name ? candidate->name : "";
        if (strstr(name, "gas_type") != nullptr) {
            gasOutput = candidate;
        } else if (strstr(name, "leak_present") != nullptr) {
            leakOutput = candidate;
        } else if (strstr(name, "severity") != nullptr) {
            severityOutput = candidate;
        } else if (strstr(name, "ppm_estimate") != nullptr) {
            ppmOutput = candidate;
        }
    }

    if (!gasOutput && interpreter->outputs_size() > 0) gasOutput = interpreter->output(0);
    if (!leakOutput && interpreter->outputs_size() > 1) leakOutput = interpreter->output(1);
    if (!severityOutput && interpreter->outputs_size() > 2) severityOutput = interpreter->output(2);
    if (!ppmOutput && interpreter->outputs_size() > 3) ppmOutput = interpreter->output(3);

    if (!gasOutput || !leakOutput || !severityOutput || !ppmOutput) {
        TF_LITE_REPORT_ERROR(error_reporter, "Expected four model outputs: gas_type, leak_present, severity, ppm_estimate.");
        return;
    }

    initialized = true;
}

// Destructor to clean up dynamically allocated memory
NeuralNetwork::~NeuralNetwork() {
    if (interpreter) {
        delete interpreter;
    }
    if (resolver) {
        delete resolver;
    }
    if (error_reporter) {
        delete error_reporter;
    }
}

float *NeuralNetwork::getInputBuffer()
{
    if (!initialized || !input) {
        TF_LITE_REPORT_ERROR(error_reporter, "Network not initialized or input buffer not available.");
        return nullptr;
    }
    return input->data.f;
}

bool NeuralNetwork::predict(GasLeakPrediction &prediction)
{
    if (!initialized) {
        TF_LITE_REPORT_ERROR(error_reporter, "Network not initialized. Cannot predict.");
        return false;
    }

    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
        TF_LITE_REPORT_ERROR(error_reporter, "Interpreter invoke failed.");
        return false;
    }

    prediction.gas_type = 0;
    prediction.gas_confidence = gasOutput->data.f[0];
    for (int i = 1; i < 3; i++) {
        if (gasOutput->data.f[i] > prediction.gas_confidence) {
            prediction.gas_confidence = gasOutput->data.f[i];
            prediction.gas_type = i;
        }
    }

    prediction.leak_probability = leakOutput->data.f[0];
    prediction.leak_present = prediction.leak_probability >= 0.5f;

    prediction.severity = 0;
    prediction.severity_confidence = severityOutput->data.f[0];
    for (int i = 1; i < 4; i++) {
        if (severityOutput->data.f[i] > prediction.severity_confidence) {
            prediction.severity_confidence = severityOutput->data.f[i];
            prediction.severity = i;
        }
    }

    prediction.ppm_estimate = ppmOutput->data.f[0];
    if (prediction.ppm_estimate < 0.0f) {
        prediction.ppm_estimate = 0.0f;
    }
    return true;
}

bool NeuralNetwork::isInitialized() {
    return initialized;
}
