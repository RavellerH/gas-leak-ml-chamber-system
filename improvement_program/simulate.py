import argparse
import time
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier, RandomForestRegressor
from sklearn.metrics import accuracy_score, confusion_matrix, mean_absolute_error
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler

from gasleak_improved.common import (
    ACTIVE_BOARDS,
    FEATURE_COLS,
    GAS_LABELS,
    OUTPUT_ROOT,
    PAYLOAD_SIZE_BYTES,
    SEVERITY_LABELS,
    Prediction,
    decode_payload,
    encode_payload,
    load_board_dataset,
    write_json,
)


def confidence_from_proba(proba_row) -> float:
    return float(np.max(proba_row))


def simulate_board(board: str, output_dir: Path) -> dict:
    df = load_board_dataset(board)
    stratify = df["gas_type"] if df["gas_type"].nunique() > 1 else None
    train_idx, test_idx = train_test_split(
        np.arange(len(df)),
        test_size=0.25,
        random_state=42,
        stratify=stratify,
    )

    scaler = StandardScaler()
    x_train = scaler.fit_transform(df.iloc[train_idx][FEATURE_COLS].to_numpy(dtype=np.float32))
    x_test = scaler.transform(df.iloc[test_idx][FEATURE_COLS].to_numpy(dtype=np.float32))
    train_df = df.iloc[train_idx]
    test_df = df.iloc[test_idx]

    gas_model = RandomForestClassifier(n_estimators=120, random_state=42, class_weight="balanced")
    leak_model = RandomForestClassifier(n_estimators=120, random_state=43, class_weight="balanced")
    severity_model = RandomForestClassifier(n_estimators=120, random_state=44, class_weight="balanced")
    ppm_model = RandomForestRegressor(n_estimators=120, random_state=45)

    gas_model.fit(x_train, train_df["gas_type"])
    leak_model.fit(x_train, train_df["leak_present"])
    severity_model.fit(x_train, train_df["severity"])
    ppm_model.fit(x_train, train_df["ppm_estimate"])

    start = time.perf_counter()
    gas_pred = gas_model.predict(x_test)
    leak_pred = leak_model.predict(x_test)
    severity_pred = severity_model.predict(x_test)
    ppm_pred = np.maximum(ppm_model.predict(x_test), 0.0)
    elapsed_us = max(1, int((time.perf_counter() - start) * 1_000_000 / max(len(x_test), 1)))

    gas_proba = gas_model.predict_proba(x_test)
    leak_proba_all = leak_model.predict_proba(x_test)
    severity_proba = severity_model.predict_proba(x_test)

    packets = []
    decoded_packets = []
    for row_number, idx in enumerate(test_idx[:10]):
        leak_classes = list(leak_model.classes_)
        leak_yes_index = leak_classes.index(1) if 1 in leak_classes else 0
        prediction = Prediction(
            board=board,
            gas_type=int(gas_pred[row_number]),
            gas_confidence=confidence_from_proba(gas_proba[row_number]),
            leak_present=int(leak_pred[row_number]),
            leak_probability=float(leak_proba_all[row_number][leak_yes_index]),
            severity=int(severity_pred[row_number]),
            severity_confidence=confidence_from_proba(severity_proba[row_number]),
            ppm_estimate=float(ppm_pred[row_number]),
            inference_time_us=elapsed_us,
            voltages=df.iloc[idx][FEATURE_COLS].astype(float).tolist(),
        )
        payload = encode_payload(prediction)
        decoded = decode_payload(payload)
        decoded["source_board"] = board
        decoded["actual_gas_type"] = int(df.iloc[idx]["gas_type"])
        decoded["actual_gas_name"] = GAS_LABELS[int(df.iloc[idx]["gas_type"])]
        decoded["actual_severity"] = int(df.iloc[idx]["severity"])
        decoded["actual_severity_name"] = SEVERITY_LABELS[int(df.iloc[idx]["severity"])]
        packets.append(payload.hex())
        decoded_packets.append(decoded)

    board_dir = output_dir / board
    board_dir.mkdir(parents=True, exist_ok=True)
    gas_class_ids = list(range(len(GAS_LABELS)))
    pd.DataFrame(confusion_matrix(test_df["gas_type"], gas_pred, labels=gas_class_ids), index=GAS_LABELS, columns=GAS_LABELS).to_csv(
        board_dir / "gas_type_confusion_matrix.csv"
    )
    pd.DataFrame(
        confusion_matrix(test_df["severity"], severity_pred, labels=[0, 1, 2, 3]),
        index=SEVERITY_LABELS,
        columns=SEVERITY_LABELS,
    ).to_csv(board_dir / "severity_confusion_matrix.csv")
    pd.DataFrame(decoded_packets).to_csv(board_dir / "sample_decoded_lora_packets.csv", index=False)
    (board_dir / "sample_lora_payloads_hex.txt").write_text("\n".join(packets), encoding="utf-8")

    metrics = {
        "board": board,
        "samples": int(len(df)),
        "test_samples": int(len(test_df)),
        "gas_type_accuracy": float(accuracy_score(test_df["gas_type"], gas_pred)),
        "leak_present_accuracy": float(accuracy_score(test_df["leak_present"], leak_pred)),
        "severity_accuracy": float(accuracy_score(test_df["severity"], severity_pred)),
        "ppm_proxy_mae": float(mean_absolute_error(test_df["ppm_estimate"], ppm_pred)),
        "simulated_inference_time_us_per_sample": elapsed_us,
        "payload_size_bytes": PAYLOAD_SIZE_BYTES,
        "ppm_note": "ppm_estimate is a response proxy because calibrated ppm labels are not present in the datasets.",
    }
    write_json(board_dir / "metrics.json", metrics)
    return metrics


def main():
    parser = argparse.ArgumentParser(description="Simulate improved gas leak detection locally.")
    parser.add_argument("--boards", nargs="*", default=ACTIVE_BOARDS, help="Board names to simulate")
    args = parser.parse_args()

    output_dir = OUTPUT_ROOT / "simulation"
    output_dir.mkdir(parents=True, exist_ok=True)

    results = []
    for board in args.boards:
        print(f"Simulating {board}...")
        metrics = simulate_board(board, output_dir)
        results.append(metrics)
        print(
            f"  gas={metrics['gas_type_accuracy']:.3f} "
            f"leak={metrics['leak_present_accuracy']:.3f} "
            f"severity={metrics['severity_accuracy']:.3f} "
            f"ppm_mae={metrics['ppm_proxy_mae']:.1f}"
        )

    summary = pd.DataFrame(results)
    summary.to_csv(output_dir / "summary.csv", index=False)
    write_json(output_dir / "summary.json", results)
    print(f"\nSimulation summary: {output_dir / 'summary.csv'}")


if __name__ == "__main__":
    main()
