"""Train a new gas leak phase model from live recorder datasets only.

The model predicts:
  0 clean_air
  1 gas_rising
  2 gas_detected
  3 gas_clearing

Usage:
  python tools/train_live_phase_model.py datasets/live/*.csv datasets/live/*.xlsx
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd
import tensorflow as tf
from sklearn.metrics import classification_report, confusion_matrix
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler


FEATURE_COLS = ["MQ135V", "MQ2V", "MQ3V", "MQ4V", "MQ7V", "MQ5V", "MQ6V", "MQ8V"]
PHASES = ["clean_air", "gas_rising", "gas_detected", "gas_clearing"]
PHASE_TO_ID = {name: index for index, name in enumerate(PHASES)}


def load_one(path: Path) -> pd.DataFrame:
    if path.suffix.lower() in {".xlsx", ".xls"}:
        df = pd.read_excel(path)
    else:
        df = pd.read_csv(path)

    missing = [col for col in ["phase", *FEATURE_COLS] if col not in df.columns]
    if missing:
        raise ValueError(f"{path} missing columns: {missing}")

    df = df.copy()
    df["phase"] = df["phase"].astype(str).str.strip()
    df = df[df["phase"].isin(PHASE_TO_ID)].copy()
    for col in FEATURE_COLS:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna(subset=FEATURE_COLS)
    if df.empty:
        raise ValueError(f"{path} has no usable rows after filtering")
    df["phase_id"] = df["phase"].map(PHASE_TO_ID).astype(int)
    df["source_file"] = str(path)
    return df


def load_all(paths: list[Path]) -> pd.DataFrame:
    frames = [load_one(path) for path in paths]
    df = pd.concat(frames, ignore_index=True)
    counts = df["phase"].value_counts().reindex(PHASES, fill_value=0)
    missing = counts[counts == 0].index.tolist()
    if missing:
        raise ValueError(f"Missing phase data: {missing}")
    print("Rows by phase:")
    for phase, count in counts.items():
        print(f"  {phase}: {count}")
    return df


def build_model(input_dim: int, class_count: int) -> tf.keras.Model:
    model = tf.keras.Sequential(
        [
            tf.keras.layers.Input(shape=(input_dim,)),
            tf.keras.layers.Dense(24, activation="relu"),
            tf.keras.layers.Dropout(0.15),
            tf.keras.layers.Dense(16, activation="relu"),
            tf.keras.layers.Dropout(0.10),
            tf.keras.layers.Dense(class_count, activation="softmax"),
        ]
    )
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )
    return model


def write_c_array(path: Path, array_name: str, data: bytes) -> None:
    values = [f"0x{byte:02x}" for byte in data]
    lines = []
    for index in range(0, len(values), 12):
        lines.append("  " + ", ".join(values[index : index + 12]))
    wrapped = ",\n".join(lines)
    path.write_text(
        f"""#include <stddef.h>

const unsigned char {array_name}[] = {{
{wrapped}
}};

const size_t {array_name}_len = {len(data)};
""",
        encoding="utf-8",
    )


def export_tflite(model: tf.keras.Model, representative: np.ndarray, out_dir: Path) -> bytes:
    def representative_data_gen():
        for row in representative[: min(200, len(representative))]:
            yield [row.reshape(1, -1).astype(np.float32)]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_data_gen
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    tflite_model = converter.convert()
    (out_dir / "gasleak_phase_model_int8.tflite").write_bytes(tflite_model)
    write_c_array(out_dir / "gasleak_phase_model.cc", "gasleak_phase_model_tflite", tflite_model)
    return tflite_model


def main() -> None:
    parser = argparse.ArgumentParser(description="Train a live LPG/butane phase model.")
    parser.add_argument("datasets", nargs="+", help="CSV/XLSX live training dataset paths.")
    parser.add_argument("--out", default="improvement_program/output/live_phase_model", help="Output directory.")
    parser.add_argument("--epochs", type=int, default=80)
    args = parser.parse_args()

    paths = [Path(item) for item in args.datasets]
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    df = load_all(paths)
    x = df[FEATURE_COLS].to_numpy(dtype=np.float32)
    y = df["phase_id"].to_numpy(dtype=np.int64)

    stratify = y if min(np.bincount(y, minlength=len(PHASES))) >= 2 else None
    x_train, x_test, y_train, y_test = train_test_split(
        x,
        y,
        test_size=0.2,
        random_state=42,
        stratify=stratify,
    )

    scaler = StandardScaler()
    x_train_scaled = scaler.fit_transform(x_train).astype(np.float32)
    x_test_scaled = scaler.transform(x_test).astype(np.float32)

    model = build_model(len(FEATURE_COLS), len(PHASES))
    callbacks = [
        tf.keras.callbacks.EarlyStopping(monitor="val_loss", patience=12, restore_best_weights=True),
    ]
    history = model.fit(
        x_train_scaled,
        y_train,
        validation_data=(x_test_scaled, y_test),
        epochs=args.epochs,
        batch_size=32,
        callbacks=callbacks,
        verbose=1,
    )

    loss, accuracy = model.evaluate(x_test_scaled, y_test, verbose=0)
    pred = np.argmax(model.predict(x_test_scaled, verbose=0), axis=1)
    report = classification_report(y_test, pred, target_names=PHASES, output_dict=True, zero_division=0)
    cm = confusion_matrix(y_test, pred, labels=list(range(len(PHASES))))

    model.save(out_dir / "gasleak_phase_model.keras")
    tflite_model = export_tflite(model, x_train_scaled, out_dir)

    pd.DataFrame(history.history).to_csv(out_dir / "training_history.csv", index=False)
    pd.DataFrame(cm, index=PHASES, columns=PHASES).to_csv(out_dir / "confusion_matrix.csv")
    scaler_data = {
        "feature_cols": FEATURE_COLS,
        "feature_means": scaler.mean_.tolist(),
        "feature_stds": scaler.scale_.tolist(),
    }
    (out_dir / "scaler_params.json").write_text(json.dumps(scaler_data, indent=2), encoding="utf-8")
    (out_dir / "classes.json").write_text(json.dumps({"classes": PHASES}, indent=2), encoding="utf-8")
    metrics = {
        "test_loss": float(loss),
        "test_accuracy": float(accuracy),
        "rows": int(len(df)),
        "rows_by_phase": df["phase"].value_counts().reindex(PHASES, fill_value=0).astype(int).to_dict(),
        "classification_report": report,
        "tflite_size_bytes": len(tflite_model),
        "uses_old_data": False,
    }
    (out_dir / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")

    print(f"Saved model outputs to {out_dir}")
    print(f"Test accuracy: {accuracy:.4f}")


if __name__ == "__main__":
    main()
