import argparse
import json
from datetime import datetime
from pathlib import Path

import numpy as np
import pandas as pd
import tensorflow as tf
from sklearn.metrics import accuracy_score, confusion_matrix, mean_absolute_error
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler

from gasleak_improved.common import (
    ACTIVE_BOARDS,
    FEATURE_COLS,
    GAS_LABELS,
    OUTPUT_ROOT,
    SEVERITY_LABELS,
    load_board_dataset,
)


def build_model(input_dim: int) -> tf.keras.Model:
    inputs = tf.keras.Input(shape=(input_dim,), name="sensor_input")
    x = tf.keras.layers.Dense(32, activation="relu")(inputs)
    x = tf.keras.layers.Dropout(0.15)(x)
    x = tf.keras.layers.Dense(16, activation="relu")(x)
    shared = tf.keras.layers.Dense(12, activation="relu")(x)
    outputs = {
        "gas_type": tf.keras.layers.Dense(len(GAS_LABELS), activation="softmax", name="gas_type")(shared),
        "leak_present": tf.keras.layers.Dense(1, activation="sigmoid", name="leak_present")(shared),
        "severity": tf.keras.layers.Dense(4, activation="softmax", name="severity")(shared),
        "ppm_estimate": tf.keras.layers.Dense(1, activation="linear", name="ppm_estimate")(shared),
    }
    model = tf.keras.Model(inputs=inputs, outputs=outputs)
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
        loss={
            "gas_type": "sparse_categorical_crossentropy",
            "leak_present": "binary_crossentropy",
            "severity": "sparse_categorical_crossentropy",
            "ppm_estimate": "mse",
        },
        loss_weights={"gas_type": 1.0, "leak_present": 0.6, "severity": 0.6, "ppm_estimate": 0.001},
        metrics={
            "gas_type": ["accuracy"],
            "leak_present": ["accuracy"],
            "severity": ["accuracy"],
            "ppm_estimate": ["mae"],
        },
    )
    return model


def c_float_array(values) -> str:
    return ", ".join(f"{float(v):.10f}f" for v in values)


def write_c_artifacts(board_dir: Path, artifact_dir: Path, tflite_model: bytes, scaler: StandardScaler) -> None:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    (artifact_dir / "gasleak_model.tflite").write_bytes(tflite_model)
    (artifact_dir / "model_data.h").write_text(
        "#ifndef MODEL_DATA_H\n"
        "#define MODEL_DATA_H\n"
        "extern const unsigned char model_tflite[];\n"
        "extern const unsigned int model_tflite_len;\n"
        "#endif\n",
        encoding="utf-8",
    )
    hex_bytes = ", ".join(f"0x{byte:02x}" for byte in tflite_model)
    (artifact_dir / "model_data.cc").write_text(
        f"// Generated multi-task model for {board_dir.name}\n"
        '#include "model_data.h"\n\n'
        f"const unsigned char model_tflite[] = {{{hex_bytes}}};\n"
        f"const unsigned int model_tflite_len = {len(tflite_model)};\n",
        encoding="utf-8",
    )
    (artifact_dir / "scaler_params.h").write_text(
        "#ifndef SCALER_PARAMS_H\n"
        "#define SCALER_PARAMS_H\n"
        "extern const float feature_means[8];\n"
        "extern const float feature_stds[8];\n"
        "#endif\n",
        encoding="utf-8",
    )
    (artifact_dir / "scaler_params.cc").write_text(
        '#include "scaler_params.h"\n'
        f"const float feature_means[8] = {{{c_float_array(scaler.mean_)}}};\n"
        f"const float feature_stds[8] = {{{c_float_array(scaler.scale_)}}};\n",
        encoding="utf-8",
    )


def write_summary_outputs(summary_dir: Path, metrics: list[dict]) -> None:
    summary_dir.mkdir(parents=True, exist_ok=True)
    csv_path = summary_dir / "all_boards_summary.csv"
    json_path = summary_dir / "all_boards_summary.json"
    try:
        pd.DataFrame(metrics).to_csv(csv_path, index=False)
        json_path.write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    except PermissionError:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        fallback_csv = summary_dir / f"all_boards_summary_{timestamp}.csv"
        fallback_json = summary_dir / f"all_boards_summary_{timestamp}.json"
        pd.DataFrame(metrics).to_csv(fallback_csv, index=False)
        fallback_json.write_text(json.dumps(metrics, indent=2), encoding="utf-8")
        print(f"Summary files were locked; wrote {fallback_csv.name} and {fallback_json.name} instead.")


def train_board(board: str, epochs: int) -> dict:
    df = load_board_dataset(board)
    train_idx, test_idx = train_test_split(
        np.arange(len(df)),
        test_size=0.2,
        random_state=42,
        stratify=df["gas_type"],
    )
    train_df = df.iloc[train_idx]
    test_df = df.iloc[test_idx]
    scaler = StandardScaler()
    x_train = scaler.fit_transform(train_df[FEATURE_COLS].to_numpy(dtype=np.float32))
    x_test = scaler.transform(test_df[FEATURE_COLS].to_numpy(dtype=np.float32))

    model = build_model(len(FEATURE_COLS))
    history = model.fit(
        x_train,
        {
            "gas_type": train_df["gas_type"].to_numpy(np.int32),
            "leak_present": train_df["leak_present"].to_numpy(np.float32),
            "severity": train_df["severity"].to_numpy(np.int32),
            "ppm_estimate": train_df["ppm_estimate"].to_numpy(np.float32),
        },
        validation_data=(
            x_test,
            {
                "gas_type": test_df["gas_type"].to_numpy(np.int32),
                "leak_present": test_df["leak_present"].to_numpy(np.float32),
                "severity": test_df["severity"].to_numpy(np.int32),
                "ppm_estimate": test_df["ppm_estimate"].to_numpy(np.float32),
            },
        ),
        epochs=epochs,
        batch_size=32,
        verbose=0,
    )

    predictions = model.predict(x_test, verbose=0)
    pred_gas = np.argmax(predictions["gas_type"], axis=1)
    pred_leak = (predictions["leak_present"].reshape(-1) >= 0.5).astype(int)
    pred_severity = np.argmax(predictions["severity"], axis=1)
    pred_ppm = np.maximum(predictions["ppm_estimate"].reshape(-1), 0.0)

    @tf.function(input_signature=[tf.TensorSpec(shape=[None, len(FEATURE_COLS)], dtype=tf.float32)])
    def serving_fn(sensor_input):
        return model(sensor_input, training=False)

    concrete_fn = serving_fn.get_concrete_function()
    converter = tf.lite.TFLiteConverter.from_concrete_functions([concrete_fn], model)
    tflite_model = converter.convert()

    model_dir = OUTPUT_ROOT / "models" / board
    report_dir = OUTPUT_ROOT / "reports" / board
    report_dir.mkdir(parents=True, exist_ok=True)
    write_c_artifacts(Path(board), model_dir, tflite_model, scaler)

    pd.DataFrame(history.history).to_csv(report_dir / "training_history.csv", index=False)
    gas_class_ids = list(range(len(GAS_LABELS)))
    pd.DataFrame(confusion_matrix(test_df["gas_type"], pred_gas, labels=gas_class_ids), index=GAS_LABELS, columns=GAS_LABELS).to_csv(
        report_dir / "gas_type_confusion_matrix.csv"
    )
    pd.DataFrame(
        confusion_matrix(test_df["severity"], pred_severity, labels=[0, 1, 2, 3]),
        index=SEVERITY_LABELS,
        columns=SEVERITY_LABELS,
    ).to_csv(report_dir / "severity_confusion_matrix.csv")

    metrics = {
        "board": board,
        "samples": int(len(df)),
        "gas_type_accuracy": float(accuracy_score(test_df["gas_type"], pred_gas)),
        "leak_present_accuracy": float(accuracy_score(test_df["leak_present"], pred_leak)),
        "severity_accuracy": float(accuracy_score(test_df["severity"], pred_severity)),
        "ppm_proxy_mae": float(mean_absolute_error(test_df["ppm_estimate"], pred_ppm)),
        "model_size_bytes": len(tflite_model),
    }
    (report_dir / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    return metrics


def main():
    parser = argparse.ArgumentParser(description="Train full TensorFlow multi-task gas leak models.")
    parser.add_argument("--boards", nargs="*", default=ACTIVE_BOARDS)
    parser.add_argument("--epochs", type=int, default=180)
    args = parser.parse_args()

    np.random.seed(42)
    tf.random.set_seed(42)

    all_metrics = []
    for board in args.boards:
        print(f"Training {board}...")
        metrics = train_board(board, args.epochs)
        all_metrics.append(metrics)
        print(
            f"  gas={metrics['gas_type_accuracy']:.3f} "
            f"leak={metrics['leak_present_accuracy']:.3f} "
            f"severity={metrics['severity_accuracy']:.3f}"
        )

    summary_dir = OUTPUT_ROOT / "reports"
    write_summary_outputs(summary_dir, all_metrics)


if __name__ == "__main__":
    main()
