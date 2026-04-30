import pandas as pd
import numpy as np
from sklearn.preprocessing import StandardScaler
import tensorflow as tf

def load_data(excel_path):
    df = pd.read_excel(excel_path)
    feature_cols = ['MQ135V', 'MQ2V', 'MQ3V', 'MQ4V', 'MQ5V', 'MQ6V', 'MQ7V', 'MQ8V']
    X = df[feature_cols].values
    y = df['Unnamed: 1'].values
    return X, y

def test_model():
    excel_path = 'Board1/Board1.xlsx'
    X, y = load_data(excel_path)
    
    feature_means = [0.7676586674, 0.5908490433, 0.2665936674, 0.6067533554, 0.2177913986, 0.0042675444, 0.8116444829, 0.6276564100]
    feature_stds = [0.9338861017, 0.8336589928, 0.3823871348, 0.8826827505, 0.2501364680, 0.0012603047, 0.8401193598, 0.9156371752]
    scaler = StandardScaler()
    scaler.mean_ = np.array(feature_means)
    scaler.scale_ = np.array(feature_stds)
    scaler.n_features_in_ = 8
    
    X_scaled = scaler.transform(X)
    
    model_path = 'Board1/model_data.cc'
    start = False
    bytes_list = []
    with open(model_path, 'r') as f:
        for line in f:
            if 'unsigned char model_tflite[] = {' in line:
                start = True
                continue
            if start:
                line = line.strip().rstrip(',')
                if line == '};':
                    break
                for byte in line.split(','):
                    byte = byte.strip()
                    if byte.startswith('0x'):
                        bytes_list.append(int(byte, 16))
    
    tflite_model = bytes(bytes_list)
    
    interpreter = tf.lite.Interpreter(model_content=tflite_model)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    
    labels = ['LPG', 'Metana', 'Udara']
    unique_labels = list(np.unique(y))
    confusion = {y_true: {y_pred: 0 for y_pred in unique_labels} for y_true in unique_labels}
    correct = 0
    
    for sample, true_label in zip(X_scaled, y):
        interpreter.set_tensor(input_details[0]['index'], [sample.astype(np.float32)])
        interpreter.invoke()
        output = interpreter.get_tensor(output_details[0]['index'])[0]
        pred_idx = np.argmax(output)
        pred_label = labels[pred_idx]
        confusion[true_label][pred_label] += 1
        if pred_label == true_label:
            correct += 1
    
    print(f'\n{"":<12} Predicted')
    print(f'{"":<12}', end='')
    for label in unique_labels:
        print(f'{label:>8}', end='')
    print(f'{"Total":>8}')
    print('-' * 48)
    for true_label in unique_labels:
        print(f'{true_label:<12}', end='')
        total = 0
        for pred_label in unique_labels:
            count = confusion[true_label][pred_label]
            print(f'{count:>8}', end='')
            total += count
        print(f'{total:>8}')
    
    print(f'\nTotal: {correct}/{len(X_scaled)} = {correct/len(X_scaled)*100:.1f}% accuracy')

if __name__ == '__main__':
    test_model()