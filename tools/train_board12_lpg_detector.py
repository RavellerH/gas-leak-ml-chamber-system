"""Train a Board12 clean-air vs LPG/butane detector from raw recorder CSV files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import classification_report, confusion_matrix
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler


FEATURE_COLS = ["MQ135V", "MQ2V", "MQ3V", "MQ4V", "MQ7V", "MQ5V", "MQ6V", "MQ8V"]
ENGINEERED_COLS = ["mean_v", "max_v", "min_v", "span_v"]
MODEL_FEATURES = FEATURE_COLS + ENGINEERED_COLS


def load_raw_csvs(folder: Path) -> pd.DataFrame:
    frames = []
    for path in sorted(folder.glob("Board12_raw_dataset*.csv")):
        if "analyzed" in path.name.lower():
            continue
        df = pd.read_csv(path)
        missing = [col for col in ["elapsed_ms", *FEATURE_COLS] if col not in df.columns]
        if missing:
            print(f"skip {path.name}: missing {missing}")
            continue
        for col in FEATURE_COLS:
            df[col] = pd.to_numeric(df[col], errors="coerce")
        df["elapsed_ms"] = pd.to_numeric(df["elapsed_ms"], errors="coerce")
        df = df.dropna(subset=["elapsed_ms", *FEATURE_COLS]).copy()
        if df.empty:
            continue
        df["source_file"] = path.name
        df["elapsed_s"] = df["elapsed_ms"] / 1000.0
        frames.append(df)
    if not frames:
        raise ValueError(f"No usable raw CSV files found in {folder}")
    return pd.concat(frames, ignore_index=True)


def add_features(df: pd.DataFrame) -> pd.DataFrame:
    df = df.copy()
    df["mean_v"] = df[FEATURE_COLS].mean(axis=1)
    df["max_v"] = df[FEATURE_COLS].max(axis=1)
    df["min_v"] = df[FEATURE_COLS].min(axis=1)
    df["span_v"] = df["max_v"] - df["min_v"]
    return df


def auto_label(df: pd.DataFrame) -> tuple[pd.DataFrame, dict]:
    df = add_features(df)

    low_pool = df[df["mean_v"] <= df["mean_v"].quantile(0.20)]
    low_pool = low_pool[low_pool["mean_v"] <= 0.025]
    if len(low_pool) < 50:
        low_pool = df.nsmallest(max(50, len(df) // 10), "mean_v")

    clean_mean = float(low_pool["mean_v"].median())
    clean_std = float(max(low_pool["mean_v"].std(ddof=0), 0.0005))
    clean_limit = clean_mean + max(4.0 * clean_std, 0.006)
    lpg_limit = clean_mean + max(8.0 * clean_std, 0.018)

    df["auto_label"] = "ambiguous"
    df.loc[df["mean_v"] <= clean_limit, "auto_label"] = "clean_air"
    df.loc[df["mean_v"] >= lpg_limit, "auto_label"] = "lpg_detected"

    # Rows in files that start already elevated are LPG/clearing, not clean-air.
    first_by_file = df.groupby("source_file")["mean_v"].transform("first")
    contaminated_file = first_by_file > lpg_limit
    df.loc[contaminated_file & (df["mean_v"] > clean_limit), "auto_label"] = "lpg_detected"

    summary = {
        "clean_reference_mean_v": clean_mean,
        "clean_reference_std_v": clean_std,
        "clean_limit_v": float(clean_limit),
        "lpg_limit_v": float(lpg_limit),
        "label_counts": df["auto_label"].value_counts().to_dict(),
        "source_files": sorted(df["source_file"].unique().tolist()),
    }
    return df, summary


def split_train_test(labeled: pd.DataFrame):
    x = labeled[MODEL_FEATURES].to_numpy(dtype=np.float32)
    y = (labeled["auto_label"] == "lpg_detected").astype(int).to_numpy()
    return train_test_split(x, y, test_size=0.25, random_state=42, stratify=y)


def export_model_json(path: Path, scaler: StandardScaler, model: LogisticRegression, summary: dict, metrics: dict) -> None:
    model_json = {
        "model_type": "standard_scaled_logistic_regression",
        "name": "Board12 LPG / liquified butane detector",
        "version": 1,
        "classes": ["clean_air", "lpg_detected"],
        "feature_cols": MODEL_FEATURES,
        "sensor_cols": FEATURE_COLS,
        "engineered_cols": ENGINEERED_COLS,
        "scaler_mean": scaler.mean_.tolist(),
        "scaler_scale": scaler.scale_.tolist(),
        "coef": model.coef_[0].tolist(),
        "intercept": float(model.intercept_[0]),
        "probability_thresholds": {
            "clean_air_max": 0.30,
            "lpg_detected_min": 0.70,
        },
        "auto_label_summary": summary,
        "training_metrics": metrics,
    }
    path.write_text(json.dumps(model_json, indent=2), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Train Board12 LPG detector.")
    parser.add_argument("--data-dir", default="test LPG board 12")
    parser.add_argument("--out-dir", default="test LPG board 12/ml_board12_lpg_detector")
    args = parser.parse_args()

    data_dir = Path(args.data_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    df = load_raw_csvs(data_dir)
    df, label_summary = auto_label(df)
    train_df = df[df["auto_label"].isin(["clean_air", "lpg_detected"])].copy()
    if train_df["auto_label"].nunique() != 2:
        raise ValueError("Need both clean_air and lpg_detected rows after auto-labeling")

    x_train, x_test, y_train, y_test = split_train_test(train_df)
    scaler = StandardScaler()
    x_train_s = scaler.fit_transform(x_train)
    x_test_s = scaler.transform(x_test)

    model = LogisticRegression(class_weight="balanced", max_iter=1000, random_state=42)
    model.fit(x_train_s, y_train)

    pred = model.predict(x_test_s)
    prob = model.predict_proba(x_test_s)[:, 1]
    report = classification_report(y_test, pred, target_names=["clean_air", "lpg_detected"], output_dict=True, zero_division=0)
    cm = confusion_matrix(y_test, pred, labels=[0, 1])
    metrics = {
        "test_accuracy": float((pred == y_test).mean()),
        "test_rows": int(len(y_test)),
        "train_rows": int(len(y_train)),
        "used_rows": int(len(train_df)),
        "excluded_ambiguous_rows": int((df["auto_label"] == "ambiguous").sum()),
        "probability_min": float(prob.min()),
        "probability_max": float(prob.max()),
        "classification_report": report,
        "confusion_matrix": cm.tolist(),
    }

    df.to_csv(out_dir / "Board12_lpg_auto_labeled_all_rows.csv", index=False)
    train_df.to_csv(out_dir / "Board12_lpg_training_rows.csv", index=False)
    pd.DataFrame(cm, index=["actual_clean_air", "actual_lpg_detected"], columns=["pred_clean_air", "pred_lpg_detected"]).to_csv(
        out_dir / "confusion_matrix.csv"
    )
    with pd.ExcelWriter(out_dir / "Board12_lpg_labeled_dataset.xlsx", engine="openpyxl") as writer:
        df.to_excel(writer, sheet_name="all_auto_labeled", index=False)
        train_df.to_excel(writer, sheet_name="training_rows", index=False)
        pd.DataFrame(cm, index=["actual_clean_air", "actual_lpg_detected"], columns=["pred_clean_air", "pred_lpg_detected"]).to_excel(
            writer, sheet_name="confusion_matrix"
        )

    (out_dir / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    (out_dir / "auto_label_summary.json").write_text(json.dumps(label_summary, indent=2), encoding="utf-8")
    export_model_json(out_dir / "board12_lpg_detector_model.json", scaler, model, label_summary, metrics)

    report_lines = [
        "# Board12 LPG Detector Training Report",
        "",
        f"Rows loaded: {len(df)}",
        f"Rows used for training/evaluation: {len(train_df)}",
        f"Ambiguous rows excluded: {metrics['excluded_ambiguous_rows']}",
        f"Clean limit: {label_summary['clean_limit_v']:.6f} V mean",
        f"LPG limit: {label_summary['lpg_limit_v']:.6f} V mean",
        f"Test accuracy: {metrics['test_accuracy']:.4f}",
        "",
        "## Label Counts",
    ]
    for label, count in label_summary["label_counts"].items():
        report_lines.append(f"- {label}: {count}")
    report_lines.extend(
        [
            "",
            "## Confusion Matrix",
            "Rows are actual labels, columns are predicted labels.",
            "",
            pd.DataFrame(cm, index=["actual_clean_air", "actual_lpg_detected"], columns=["pred_clean_air", "pred_lpg_detected"]).to_markdown(),
            "",
            "## Browser Thresholds",
            "- LPG detected: probability >= 0.70",
            "- Clean air: probability <= 0.30",
            "- Otherwise: uncertain",
        ]
    )
    (out_dir / "training_report.md").write_text("\n".join(report_lines), encoding="utf-8")

    print(json.dumps({"label_summary": label_summary, "metrics": metrics}, indent=2))
    print(f"Wrote {out_dir}")


if __name__ == "__main__":
    main()
