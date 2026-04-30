import pandas as pd
import numpy as np
import re
import os
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
import tensorflow as tf

# Configuration
BASE_PATH = 'C:/Users/HP/Documents/GitHub/ApplyGasleak/src/Gasleak'
BOARDS = ['Board3', 'Board4', 'Board5', 'Board6', 'Board7', 'Board9', 'Board10', 'Board11']
FEATURE_COLS = ['MQ135V', 'MQ2V', 'MQ3V', 'MQ4V', 'MQ7V', 'MQ5V', 'MQ6V', 'MQ8V']

def parse_label(seq):
    """Parse label from sequence string"""
    match = re.search(r'param:(\d)-(\d)-(\d)-(\d)-(\d)', str(seq))
    if match:
        labels = [int(x) for x in match.groups()]
        # param format: x-y-z-w-v -> mapped to class
        # 0-0-0-0-0 -> 0 (No Gas)
        # 1-0-0-0-0 -> 1 (Methane)
        # 0-0-1-0-0 -> 2 (LPG)
        if labels[0] == 1:
            return 1  # Methane
        elif labels[2] == 1:
            return 2  # LPG
        else:
            return 0  # No Gas
    return None

def load_all_data():
    """Load and combine data from all boards"""
    all_data = []
    for board in BOARDS:
        file_path = f'{BASE_PATH}/{board}/{board}.xlsx'
        if os.path.exists(file_path):
            df = pd.read_excel(file_path)
            if 'sequence' not in df.columns:
                print(f'{board}: No sequence column, skipping')
                continue
            df['label'] = df['sequence'].apply(parse_label)
            df = df.dropna(subset=['label'])
            df = df[FEATURE_COLS + ['label']]
            all_data.append(df)
            print(f'{board}: {len(df)} rows')
    
    combined = pd.concat(all_data, ignore_index=True)
    print(f'Total: {len(combined)} rows')
    print(f'Label distribution: {combined["label"].value_counts().sort_index().to_dict()}')
    return combined

def build_optimized_model(input_dim, num_classes):
    """Build a small, optimized model"""
    model = tf.keras.Sequential([
        tf.keras.layers.Input(shape=(input_dim,)),
        tf.keras.layers.Dense(16, activation='relu', name='dense1'),
        tf.keras.layers.Dropout(0.2),
        tf.keras.layers.Dense(8, activation='relu', name='dense2'),
        tf.keras.layers.Dropout(0.2),
        tf.keras.layers.Dense(num_classes, activation='softmax', name='output')
    ])
    
    model.compile(
        optimizer='adam',
        loss='sparse_categorical_crossentropy',
        metrics=['accuracy']
    )
    return model

def train_and_quantize():
    print("Loading data...")
    df = load_all_data()
    
    X = df[FEATURE_COLS].values
    y = df['label'].values.astype(int)
    
    # Split data
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=y
    )
    
    # Scale features
    scaler = StandardScaler()
    X_train_scaled = scaler.fit_transform(X_train)
    X_test_scaled = scaler.transform(X_test)
    
    print(f"Training samples: {len(X_train)}, Test samples: {len(X_test)}")
    print(f"Feature means: {scaler.mean_.tolist()}")
    print(f"Feature stds: {scaler.scale_.tolist()}")
    
    # Save scaler parameters for C code
    with open('scaler_params.txt', 'w') as f:
        f.write("feature_means = " + str(scaler.mean_.tolist()) + "\n")
        f.write("feature_stds = " + str(scaler.scale_.tolist()) + "\n")
    
    # Build and train model
    print("\nTraining model...")
    model = build_optimized_model(8, 3)
    model.summary()
    
    history = model.fit(
        X_train_scaled, y_train,
        epochs=50,
        batch_size=32,
        validation_data=(X_test_scaled, y_test),
        verbose=1
    )
    
    # Evaluate
    loss, accuracy = model.evaluate(X_test_scaled, y_test)
    print(f"\nTest accuracy: {accuracy:.4f}")
    
    # Convert to TensorFlow Lite with quantization
    print("\nConverting to TensorFlow Lite (int8 quantization)...")
    
    # Create representative dataset for quantization
    def representative_data_gen():
        for i in range(100):
            yield [X_test_scaled[i:i+1].astype(np.float32)]
    
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_data_gen
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    
    tflite_model = converter.convert()
    
    # Save .tflite model
    with open('gasleak_model.tflite', 'wb') as f:
        f.write(tflite_model)
    print("Saved: gasleak_model.tflite")
    
    # Generate C header
    generate_c_header(tflite_model, scaler)
    
    return model, scaler

def generate_c_header(tflite_model, scaler):
    """Generate C header file from TFLite model"""
    
    # Convert to hex array
    hex_lines = []
    for i, byte in enumerate(tflite_model):
        hex_lines.append(f"0x{byte:02x}")
        if (i + 1) % 12 == 0:
            hex_lines[-1] += "\n"
    
    header = f"""// Auto-generated TensorFlow Lite model
// Model: 8 inputs -> 16 -> 8 -> 3 classes
// Quantized to int8

#ifndef MODEL_DATA_H
#define MODEL_DATA_H

#include <stddef.h>

extern const unsigned char model_tflite[];
extern const size_t model_tflite_len;

// Scaler parameters for normalization
extern const float feature_means[8];
extern const float feature_stds[8];

#endif

// Model data
const unsigned char model_tflite[] = {{
{', '.join(hex_lines)}
}};
const size_t model_tflite_len = {len(tflite_model)};

// Scaler parameters
const float feature_means[8] = {{
    {', '.join([str(x) for x in scaler.mean_])}
}};

const float feature_stds[8] = {{
    {', '.join([str(x) for x in scaler.scale_])}
}};
"""
    
    with open('model_data.h', 'w') as f:
        f.write(header)
    print("Saved: model_data.h")

if __name__ == '__main__':
    train_and_quantize()