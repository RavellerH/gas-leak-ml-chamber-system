import json
import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import tensorflow as tf
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix, mean_absolute_error
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler


BASE_PATH = Path("src/Gasleak")
REPORTS_PATH = Path("reports")
BOARDS = [
    "Board1",
    "Board2-2",
    "Board3",
    "Board4",
    "Board5",
    "Board6",
    "Board7",
    "Board9",
    "Board10",
    "Board11",
]
FEATURE_COLS = ["MQ135V", "MQ2V", "MQ3V", "MQ4V", "MQ7V", "MQ5V", "MQ6V", "MQ8V"]
GAS_LABELS = ["normal", "methane", "lpg"]
SEVERITY_LABELS = ["normal", "low", "medium", "high"]


def parse_param(sequence):
    match = re.search(r"param:(\d+)-(\d+)-(\d+)-(\d+)-(\d+)", str(sequence))
    if not match:
        return None
    return tuple(int(x) for x in match.groups())


def gas_label_from_param(param):
    if param[0] == 1:
        return 1
    if param[2] == 1:
        return 2
    return 0


def load_board_data(board):
    xlsx_path = BASE_PATH / board / f"{board}.xlsx"
    df = pd.read_excel(xlsx_path)
    if "sequence" not in df.columns:
        raise ValueError(f"{xlsx_path} does not contain a sequence column")

    missing = [col for col in FEATURE_COLS if col not in df.columns]
    if missing:
        raise ValueError(f"{xlsx_path} missing feature columns: {missing}")

    df["param_tuple"] = df["sequence"].apply(parse_param)
    df = df.dropna(subset=["param_tuple"]).copy()
    df["gas_type"] = df["param_tuple"].apply(gas_label_from_param).astype(int)
    df["leak_present"] = (df["gas_type"] != 0).astype(int)

    for col in FEATURE_COLS:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna(subset=FEATURE_COLS).copy()
    if df.empty:
        raise ValueError(f"{xlsx_path} has no usable rows")

    return df


def add_derived_targets(df):
    normal = df[df["gas_type"] == 0]
    if normal.empty:
        baseline = df[FEATURE_COLS].median()
        spread = df[FEATURE_COLS].std(ddof=0).replace(0, 1.0)
    else:
        baseline = normal[FEATURE_COLS].median()
        spread = normal[FEATURE_COLS].std(ddof=0).replace(0, 1.0).fillna(1.0)

    response = ((df[FEATURE_COLS] - baseline).abs() / spread).mean(axis=1)
    df["response_index"] = response.astype(float)
    df["severity"] = 0

    leak_mask = df["leak_present"] == 1
    leak_response = df.loc[leak_mask, "response_index"]
    if len(leak_response) >= 3:
        q1, q2 = leak_response.quantile([1 / 3, 2 / 3]).values
        df.loc[leak_mask & (df["response_index"] <= q1), "severity"] = 1
        df.loc[leak_mask & (df["response_index"] > q1) & (df["response_index"] <= q2), "severity"] = 2
        df.loc[leak_mask & (df["response_index"] > q2), "severity"] = 3
    else:
        df.loc[leak_mask, "severity"] = 2

    max_response = float(leak_response.max()) if len(leak_response) else 1.0
    if max_response <= 0:
        max_response = 1.0

    # This is a board-local proxy because the provided datasets do not include calibrated ppm labels.
    df["ppm_estimate"] = np.where(
        df["leak_present"] == 1,
        np.clip((df["response_index"] / max_response) * 1000.0, 1.0, 1000.0),
        0.0,
    ).astype(float)
    return df


def build_model(input_dim):
    inputs = tf.keras.Input(shape=(input_dim,), name="sensor_input")
    x = tf.keras.layers.Dense(32, activation="relu")(inputs)
    x = tf.keras.layers.Dropout(0.15)(x)
    x = tf.keras.layers.Dense(16, activation="relu")(x)
    x = tf.keras.layers.Dropout(0.10)(x)
    shared = tf.keras.layers.Dense(12, activation="relu")(x)

    gas_type = tf.keras.layers.Dense(3, activation="softmax", name="gas_type")(shared)
    leak_present = tf.keras.layers.Dense(1, activation="sigmoid", name="leak_present")(shared)
    severity = tf.keras.layers.Dense(4, activation="softmax", name="severity")(shared)
    ppm_estimate = tf.keras.layers.Dense(1, activation="linear", name="ppm_estimate")(shared)

    model = tf.keras.Model(
        inputs=inputs,
        outputs=[gas_type, leak_present, severity, ppm_estimate],
    )
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
        loss={
            "gas_type": "sparse_categorical_crossentropy",
            "leak_present": "binary_crossentropy",
            "severity": "sparse_categorical_crossentropy",
            "ppm_estimate": "mse",
        },
        loss_weights={
            "gas_type": 1.0,
            "leak_present": 0.6,
            "severity": 0.6,
            "ppm_estimate": 0.001,
        },
        metrics={
            "gas_type": ["accuracy"],
            "leak_present": ["accuracy"],
            "severity": ["accuracy"],
            "ppm_estimate": ["mae"],
        },
    )
    return model


def write_array_header(path, means, stds):
    means_str = ", ".join(f"{x:.10f}f" for x in means)
    stds_str = ", ".join(f"{x:.10f}f" for x in stds)
    path.write_text(
        '#include "scaler_params.h"\n'
        f"const float feature_means[8] = {{{means_str}}};\n"
        f"const float feature_stds[8] = {{{stds_str}}};\n",
        encoding="utf-8",
    )


def write_scaler_header(path):
    path.write_text(
        "#ifndef SCALER_PARAMS_H\n"
        "#define SCALER_PARAMS_H\n"
        "extern const float feature_means[8];\n"
        "extern const float feature_stds[8];\n"
        "#endif\n",
        encoding="utf-8",
    )


def write_model_files(board_dir, tflite_model):
    hex_arr = ", ".join(f"0x{byte:02x}" for byte in tflite_model)
    (board_dir / "model_data.cc").write_text(
        f"// Multi-task gas leak model generated for {board_dir.name}\n"
        '#include "model_data.h"\n\n'
        f"const unsigned char model_tflite[] = {{{hex_arr}}};\n"
        f"const unsigned int model_tflite_len = {len(tflite_model)};\n",
        encoding="utf-8",
    )
    (board_dir / "model_data.h").write_text(
        "#ifndef MODEL_DATA_H\n"
        "#define MODEL_DATA_H\n"
        "extern const unsigned char model_tflite[];\n"
        "extern const unsigned int model_tflite_len;\n"
        "#endif\n",
        encoding="utf-8",
    )


def plot_confusion(cm, labels, title, path):
    fig, ax = plt.subplots(figsize=(5, 4))
    image = ax.imshow(cm, cmap="Blues")
    ax.figure.colorbar(image, ax=ax)
    ax.set(
        xticks=np.arange(len(labels)),
        yticks=np.arange(len(labels)),
        xticklabels=labels,
        yticklabels=labels,
        ylabel="Actual",
        xlabel="Predicted",
        title=title,
    )
    plt.setp(ax.get_xticklabels(), rotation=30, ha="right", rotation_mode="anchor")
    for i in range(cm.shape[0]):
        for j in range(cm.shape[1]):
            ax.text(j, i, cm[i, j], ha="center", va="center", color="black")
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def convert_to_tflite(model):
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    return converter.convert()


def invoke_tflite(model_path, x):
    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()

    predictions = []
    for row in x.astype(np.float32):
        interpreter.set_tensor(input_details[0]["index"], row.reshape(1, -1))
        interpreter.invoke()
        outputs = [interpreter.get_tensor(item["index"]) for item in output_details]
        predictions.append(outputs)
    return predictions, output_details


def normalize_tflite_outputs(raw_outputs, output_details):
    by_name = {}
    for idx, detail in enumerate(output_details):
        name = detail["name"].lower()
        values = np.vstack([row[idx] for row in raw_outputs])
        by_name[name] = values

    def find(part, fallback_index):
        for key, value in by_name.items():
            if part in key:
                return value
        return np.vstack([row[fallback_index] for row in raw_outputs])

    return {
        "gas_type": find("gas_type", 0),
        "leak_present": find("leak_present", 1),
        "severity": find("severity", 2),
        "ppm_estimate": find("ppm_estimate", 3),
    }


def train_board(board):
    board_dir = BASE_PATH / board
    report_dir = REPORTS_PATH / board
    report_dir.mkdir(parents=True, exist_ok=True)

    df = add_derived_targets(load_board_data(board))
    y_gas = df["gas_type"].to_numpy(dtype=np.int32)
    stratify = y_gas if len(np.unique(y_gas)) > 1 else None
    train_idx, test_idx = train_test_split(
        np.arange(len(df)),
        test_size=0.2,
        random_state=42,
        stratify=stratify,
    )

    scaler = StandardScaler()
    x_train = scaler.fit_transform(df.iloc[train_idx][FEATURE_COLS].to_numpy(dtype=np.float32))
    x_test = scaler.transform(df.iloc[test_idx][FEATURE_COLS].to_numpy(dtype=np.float32))

    train_df = df.iloc[train_idx]
    test_df = df.iloc[test_idx]
    model = build_model(len(FEATURE_COLS))

    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor="val_gas_type_accuracy",
            mode="max",
            patience=25,
            restore_best_weights=True,
        )
    ]
    history = model.fit(
        x_train,
        {
            "gas_type": train_df["gas_type"].to_numpy(dtype=np.int32),
            "leak_present": train_df["leak_present"].to_numpy(dtype=np.float32),
            "severity": train_df["severity"].to_numpy(dtype=np.int32),
            "ppm_estimate": train_df["ppm_estimate"].to_numpy(dtype=np.float32),
        },
        validation_data=(
            x_test,
            {
                "gas_type": test_df["gas_type"].to_numpy(dtype=np.int32),
                "leak_present": test_df["leak_present"].to_numpy(dtype=np.float32),
                "severity": test_df["severity"].to_numpy(dtype=np.int32),
                "ppm_estimate": test_df["ppm_estimate"].to_numpy(dtype=np.float32),
            },
        ),
        epochs=250,
        batch_size=32,
        callbacks=callbacks,
        verbose=0,
    )

    tflite_model = convert_to_tflite(model)
    tflite_path = board_dir / "gasleak_model.tflite"
    tflite_path.write_bytes(tflite_model)
    write_model_files(board_dir, tflite_model)
    write_scaler_header(board_dir / "scaler_params.h")
    write_array_header(board_dir / "scaler_params.cc", scaler.mean_, scaler.scale_)

    raw_outputs, output_details = invoke_tflite(tflite_path, x_test)
    outputs = normalize_tflite_outputs(raw_outputs, output_details)
    pred_gas = np.argmax(outputs["gas_type"], axis=1)
    pred_leak = (outputs["leak_present"].reshape(-1) >= 0.5).astype(int)
    pred_severity = np.argmax(outputs["severity"], axis=1)
    pred_ppm = np.maximum(outputs["ppm_estimate"].reshape(-1), 0.0)

    actual_gas = test_df["gas_type"].to_numpy(dtype=np.int32)
    actual_leak = test_df["leak_present"].to_numpy(dtype=np.int32)
    actual_severity = test_df["severity"].to_numpy(dtype=np.int32)
    actual_ppm = test_df["ppm_estimate"].to_numpy(dtype=np.float32)

    gas_cm = confusion_matrix(actual_gas, pred_gas, labels=[0, 1, 2])
    severity_cm = confusion_matrix(actual_severity, pred_severity, labels=[0, 1, 2, 3])
    pd.DataFrame(gas_cm, index=GAS_LABELS, columns=GAS_LABELS).to_csv(report_dir / "gas_type_confusion_matrix.csv")
    pd.DataFrame(severity_cm, index=SEVERITY_LABELS, columns=SEVERITY_LABELS).to_csv(report_dir / "severity_confusion_matrix.csv")
    plot_confusion(gas_cm, GAS_LABELS, f"{board} Gas Type", report_dir / "gas_type_confusion_matrix.png")
    plot_confusion(severity_cm, SEVERITY_LABELS, f"{board} Severity", report_dir / "severity_confusion_matrix.png")

    predictions_df = test_df[["sequence", *FEATURE_COLS, "gas_type", "leak_present", "severity", "ppm_estimate"]].copy()
    predictions_df["pred_gas_type"] = pred_gas
    predictions_df["pred_leak_present"] = pred_leak
    predictions_df["pred_severity"] = pred_severity
    predictions_df["pred_ppm_estimate"] = pred_ppm
    predictions_df.to_csv(report_dir / "test_predictions.csv", index=False)

    metrics = {
        "board": board,
        "samples": int(len(df)),
        "train_samples": int(len(train_df)),
        "test_samples": int(len(test_df)),
        "gas_type_accuracy": float(accuracy_score(actual_gas, pred_gas)),
        "leak_present_accuracy": float(accuracy_score(actual_leak, pred_leak)),
        "severity_accuracy": float(accuracy_score(actual_severity, pred_severity)),
        "ppm_proxy_mae": float(mean_absolute_error(actual_ppm, pred_ppm)),
        "model_size_bytes": int(len(tflite_model)),
        "ppm_note": "ppm_estimate is a board-local proxy derived from sensor response; the Excel datasets do not contain calibrated ppm labels.",
        "gas_type_report": classification_report(
            actual_gas,
            pred_gas,
            labels=[0, 1, 2],
            target_names=GAS_LABELS,
            output_dict=True,
            zero_division=0,
        ),
        "severity_report": classification_report(
            actual_severity,
            pred_severity,
            labels=[0, 1, 2, 3],
            target_names=SEVERITY_LABELS,
            output_dict=True,
            zero_division=0,
        ),
    }
    (report_dir / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    pd.DataFrame(history.history).to_csv(report_dir / "training_history.csv", index=False)
    return metrics


def main():
    REPORTS_PATH.mkdir(exist_ok=True)
    all_metrics = []
    for board in BOARDS:
        print(f"Training {board}...")
        metrics = train_board(board)
        all_metrics.append(metrics)
        print(
            f"  gas={metrics['gas_type_accuracy']:.3f} "
            f"leak={metrics['leak_present_accuracy']:.3f} "
            f"severity={metrics['severity_accuracy']:.3f} "
            f"ppm_proxy_mae={metrics['ppm_proxy_mae']:.1f}"
        )

    summary = pd.DataFrame(
        [
            {
                "board": item["board"],
                "samples": item["samples"],
                "gas_type_accuracy": item["gas_type_accuracy"],
                "leak_present_accuracy": item["leak_present_accuracy"],
                "severity_accuracy": item["severity_accuracy"],
                "ppm_proxy_mae": item["ppm_proxy_mae"],
                "model_size_bytes": item["model_size_bytes"],
            }
            for item in all_metrics
        ]
    )
    summary.to_csv(REPORTS_PATH / "all_boards_summary.csv", index=False)
    (REPORTS_PATH / "all_boards_summary.json").write_text(json.dumps(all_metrics, indent=2), encoding="utf-8")
    print("\nSummary written to reports/all_boards_summary.csv")


if __name__ == "__main__":
    np.random.seed(42)
    tf.random.set_seed(42)
    main()
