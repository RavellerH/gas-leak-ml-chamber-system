import pandas as pd
import numpy as np
import re
import os
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
import tensorflow as tf

BASE_PATH = 'C:/Users/HP/Documents/GitHub/ApplyGasleak/src/Gasleak'
BOARDS = ['Board3', 'Board4', 'Board5', 'Board6', 'Board7', 'Board9', 'Board10', 'Board11']
FEATURE_COLS = ['MQ135V', 'MQ2V', 'MQ3V', 'MQ4V', 'MQ7V', 'MQ5V', 'MQ6V', 'MQ8V']

def parse_label(seq):
    match = re.search(r'param:(\d)-(\d)-(\d)-(\d)-(\d)', str(seq))
    if match:
        labels = [int(x) for x in match.groups()]
        if labels[0] == 1:
            return 1
        elif labels[2] == 1:
            return 2
        else:
            return 0
    return None

def build_model(input_dim, num_classes):
    model = tf.keras.Sequential([
        tf.keras.layers.Input(shape=(input_dim,)),
        tf.keras.layers.Dense(16, activation='relu'),
        tf.keras.layers.Dropout(0.2),
        tf.keras.layers.Dense(8, activation='relu'),
        tf.keras.layers.Dropout(0.2),
        tf.keras.layers.Dense(num_classes, activation='softmax')
    ])
    model.compile(optimizer='adam', loss='sparse_categorical_crossentropy', metrics=['accuracy'])
    return model

def train_board(board_name):
    print(f"\n{'='*50}")
    print(f"Training model for {board_name}")
    print('='*50)
    
    file_path = f'{BASE_PATH}/{board_name}/{board_name}.xlsx'
    df = pd.read_excel(file_path)
    
    df['label'] = df['sequence'].apply(parse_label)
    df = df.dropna(subset=['label'])
    df = df[FEATURE_COLS + ['label']]
    
    print(f"Total samples: {len(df)}")
    print(f"Label distribution: {df['label'].value_counts().sort_index().to_dict()}")
    
    X = df[FEATURE_COLS].values
    y = df['label'].values.astype(int)
    
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42, stratify=y)
    
    scaler = StandardScaler()
    X_train_scaled = scaler.fit_transform(X_train)
    X_test_scaled = scaler.transform(X_test)
    
    print(f"Train: {len(X_train)}, Test: {len(X_test)}")
    
    model = build_model(8, 3)
    
    history = model.fit(X_train_scaled, y_train, epochs=50, batch_size=32, 
                       validation_data=(X_test_scaled, y_test), verbose=0)
    
    loss, accuracy = model.evaluate(X_test_scaled, y_test, verbose=0)
    print(f"Test Accuracy: {accuracy:.4f}")
    
    # Quantize
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
    
    # Save model file
    with open(f'{BASE_PATH}/{board_name}/gasleak_model.tflite', 'wb') as f:
        f.write(tflite_model)
    
    # Generate model_data.cc
    hex_arr = [f'0x{b:02x}' for b in tflite_model]
    
    with open(f'{BASE_PATH}/{board_name}/model_data.cc', 'w') as f:
        f.write(f'// Model for {board_name}\n')
        f.write('#include "model_data.h"\n\n')
        f.write('const unsigned char model_tflite[] = {\n  ')
        f.write(', '.join(hex_arr))
        f.write(f'\n}};\nconst unsigned int model_tflite_len = {len(tflite_model)};\n\n')
        
        f.write('const float feature_means[8] = {\n')
        f.write('    ' + ', '.join([str(x) for x in scaler.mean_]) + '\n};\n\n')
        f.write('const float feature_stds[8] = {\n')
        f.write('    ' + ', '.join([str(x) for x in scaler.scale_]) + '\n};\n')
    
    # Update model_data.h if needed
    header_path = f'{BASE_PATH}/{board_name}/model_data.h'
    if not os.path.exists(header_path):
        with open(header_path, 'w') as f:
            f.write('#ifndef MODEL_DATA_H\n#define MODEL_DATA_H\n')
            f.write('extern const unsigned char model_tflite[];\n')
            f.write('extern const unsigned int model_tflite_len;\n')
            f.write('extern const float feature_means[8];\n')
            f.write('extern const float feature_stds[8];\n')
            f.write('#endif\n')
    
    print(f"Saved: {board_name}/gasleak_model.tflite ({len(tflite_model)} bytes)")
    print(f"Saved: {board_name}/model_data.cc")
    
    return accuracy, scaler.mean_, scaler.scale_

if __name__ == '__main__':
    results = {}
    for board in BOARDS:
        try:
            acc, means, stds = train_board(board)
            results[board] = {'accuracy': acc, 'means': means, 'stds': stds}
        except Exception as e:
            print(f"Error training {board}: {e}")
    
    print("\n" + "="*50)
    print("SUMMARY")
    print("="*50)
    for board, res in results.items():
        print(f"{board}: {res['accuracy']:.4f}")