import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler, LabelEncoder
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Dense, Dropout
from tensorflow.keras.callbacks import EarlyStopping
import tensorflow as tf
import os

def load_data(excel_path):
    df = pd.read_excel(excel_path)
    feature_cols = ['MQ135V', 'MQ2V', 'MQ3V', 'MQ4V', 'MQ5V', 'MQ6V', 'MQ7V', 'MQ8V']
    X = df[feature_cols].values
    y = df['Unnamed: 1'].values
    return X, y

def build_model(input_dim, output_dim):
    model = Sequential([
        Dense(16, activation='relu', input_shape=(input_dim,)),
        Dropout(0.2),
        Dense(8, activation='relu'),
        Dropout(0.2),
        Dense(output_dim, activation='softmax')
    ])
    model.compile(optimizer='adam', loss='categorical_crossentropy', metrics=['accuracy'])
    return model

def save_model_as_tflite(model, scaler, output_dir):
    os.makedirs(output_dir, exist_ok=True)
    
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.target_spec.supported_types = [tf.float16]
    tflite_model = converter.convert()
    
    model_cc_path = os.path.join(output_dir, 'model_data.cc')
    with open(model_cc_path, 'w') as f:
        f.write('unsigned char model_tflite[] = {\n')
        for i, byte in enumerate(tflite_model):
            if i > 0:
                f.write(', ')
            if i % 12 == 0:
                f.write('\n')
            f.write(f'0x{byte:02x}')
        f.write('\n};\n')
        f.write(f'unsigned int model_tflite_len = {len(tflite_model)};\n')
    
    scaler_cc_path = os.path.join(output_dir, 'scaler_params.cc')
    with open(scaler_cc_path, 'w') as f:
        f.write('#include "scaler_params.h"\n')
        f.write(f'const float feature_means[{scaler.n_features_in_}] = {{')
        f.write(', '.join([f'{x:.10f}' for x in scaler.mean_]))
        f.write('};\n')
        f.write(f'const float feature_stds[{scaler.n_features_in_}] = {{')
        f.write(', '.join([f'{x:.10f}' for x in scaler.scale_]))
        f.write('};\n')
    
    scaler_h_path = os.path.join(output_dir, 'scaler_params.h')
    with open(scaler_h_path, 'w') as f:
        f.write('#ifndef SCALER_PARAMS_H\n')
        f.write('#define SCALER_PARAMS_H\n')
        f.write('extern const float feature_means[8];\n')
        f.write('extern const float feature_stds[8];\n')
        f.write('#endif\n')
    
    model_h_path = os.path.join(output_dir, 'model_data.h')
    with open(model_h_path, 'w') as f:
        f.write('extern unsigned char model_tflite[];\n')
        f.write('extern unsigned int model_tflite_len;\n')
    
    print(f'Model saved to {output_dir}')

def main():
    excel_path = 'Board1/Board1.xlsx'
    output_dir = 'Board1'
    
    X, y = load_data(excel_path)
    print(f'Loaded {X.shape[0]} samples, {X.shape[1]} features')
    print(f'Classes: {np.unique(y)}')
    
    label_encoder = LabelEncoder()
    y_encoded = label_encoder.fit_transform(y)
    num_classes = len(label_encoder.classes_)
    print(f'Num classes: {num_classes}')
    
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)
    
    y_cat = tf.keras.utils.to_categorical(y_encoded, num_classes)
    
    X_train, X_test, y_train, y_test = train_test_split(
        X_scaled, y_cat, test_size=0.2, random_state=42, stratify=y_encoded
    )
    
    model = build_model(X_train.shape[1], num_classes)
    model.summary()
    
    early_stop = EarlyStopping(monitor='val_loss', patience=20, restore_best_weights=True)
    
    history = model.fit(
        X_train, y_train,
        epochs=200,
        batch_size=32,
        validation_split=0.2,
        callbacks=[early_stop],
        verbose=1
    )
    
    test_loss, test_acc = model.evaluate(X_test, y_test, verbose=0)
    print(f'Test accuracy: {test_acc:.4f}')
    
    save_model_as_tflite(model, scaler, output_dir)
    print('Training complete!')

if __name__ == '__main__':
    main()