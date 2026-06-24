"""Convert live gas leak recorder CSV files to Excel workbooks.

Usage:
  python tools/convert_live_dataset.py downloads/Board12_live_training.csv
  python tools/convert_live_dataset.py downloads/*.csv --out datasets/live
"""

from __future__ import annotations

import argparse
from pathlib import Path

import pandas as pd


FEATURE_COLS = ["MQ135V", "MQ2V", "MQ3V", "MQ4V", "MQ7V", "MQ5V", "MQ6V", "MQ8V"]
REQUIRED_COLS = ["timestamp_ms", "board_name", "board_id", "phase", "gas", *FEATURE_COLS]


def load_dataset(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    missing = [col for col in REQUIRED_COLS if col not in df.columns]
    if missing:
        raise ValueError(f"{path} missing columns: {missing}")

    for col in FEATURE_COLS:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna(subset=FEATURE_COLS).copy()
    df["phase"] = df["phase"].astype(str).str.strip()
    df["gas"] = df["gas"].astype(str).str.strip()
    return df


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert live training CSV files to XLSX.")
    parser.add_argument("csv", nargs="+", help="CSV file path(s), wildcards are expanded by the shell.")
    parser.add_argument("--out", default="datasets/live", help="Output directory for XLSX files.")
    args = parser.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    for item in args.csv:
        path = Path(item)
        df = load_dataset(path)
        out_path = out_dir / f"{path.stem}.xlsx"
        with pd.ExcelWriter(out_path, engine="openpyxl") as writer:
            df.to_excel(writer, index=False, sheet_name="live_training")
        print(f"{path}: {len(df)} rows -> {out_path}")


if __name__ == "__main__":
    main()
