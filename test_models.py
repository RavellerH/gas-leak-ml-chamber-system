import os
import pandas as pd
import numpy as np
import re
import tensorflow as tf

BASE_PATH = 'C:/Users/HP/Documents/GitHub/ApplyGasleak/src/Gasleak'
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

print('='*70)
print('TESTING ALL MODELS WITH ACTUAL DATA')
print('='*70)

results = {}
all_predictions = []

for board in ['Board3', 'Board4', 'Board5', 'Board6', 'Board7', 'Board9', 'Board10', 'Board11']:
    df = pd.read_excel(f'{BASE_PATH}/{board}/{board}.xlsx')
    df['label'] = df['sequence'].apply(parse_label)
    df = df.dropna(subset=['label'])
    
    with open(f'{BASE_PATH}/{board}/model_data.cc', 'r') as f:
        content = f.read()
    
    means_match = re.search(r'feature_means\[8\] = \{([^}]+)\}', content)
    stds_match = re.search(r'feature_stds\[8\] = \{([^}]+)\}', content)
    
    means = [float(x.strip()) for x in means_match.group(1).split(',')]
    stds = [float(x.strip()) for x in stds_match.group(1).split(',')]
    
    np.random.seed(42)
    indices = np.random.choice(len(df), min(20, len(df)), replace=False)
    
    X_test = df.iloc[indices][FEATURE_COLS].values
    y_test = df.iloc[indices]['label'].values.astype(int)
    
    X_scaled = (X_test - means) / stds
    
    scale = 0.01335611566901207
    zero_point = -32
    X_quantized = np.clip(np.round(X_scaled / scale + zero_point), -128, 127).astype(np.int8)
    
    interpreter = tf.lite.Interpreter(model_path=f'{BASE_PATH}/{board}/gasleak_model.tflite')
    interpreter.allocate_tensors()
    
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    
    correct = 0
    predictions = []
    for i in range(len(X_scaled)):
        interpreter.set_tensor(0, X_quantized[i:i+1])
        interpreter.invoke()
        output = interpreter.get_tensor(output_details[0]['index'])
        pred = np.argmax(output)
        predictions.append(pred)
        if pred == y_test[i]:
            correct += 1
    
    accuracy = correct / len(y_test)
    results[board] = {'correct': correct, 'total': len(y_test), 'accuracy': accuracy, 'predictions': predictions, 'actual': y_test.tolist()}
    print(f'{board}: {correct}/{len(y_test)} correct = {accuracy*100:.1f}%')
    print(f'  Sample predictions: {predictions[:5]} -> actual: {y_test[:5].tolist()}')

print()
print('='*70)
print('DETAILED RESULTS')
print('='*70)
for board, res in results.items():
    print(f"\n{board}:")
    print(f"  Accuracy: {res['accuracy']*100:.1f}%")
    print(f"  Correct: {res['correct']}/{res['total']}")
    
    # Show confusion
    confusion = {0:0, 1:0, 2:0}
    for p, a in zip(res['predictions'], res['actual']):
        confusion[f"{a}->{p}"] = confusion.get(f"{a}->{p}", 0) + 1
    print(f"  Details: {confusion}")

print()
print('='*70)
print('SUMMARY TABLE')
print('='*70)
print(f"{'Board':<12} {'Accuracy':<12} {'Correct/Total':<15}")
print('-'*40)
for board, res in results.items():
    print(f"{board:<12} {res['accuracy']*100:.1f}%{'':<8} {res['correct']}/{res['total']}")