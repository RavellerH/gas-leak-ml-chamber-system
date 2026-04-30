import json
import re
import struct
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np
import pandas as pd


REPO_ROOT = Path(__file__).resolve().parents[2]
DATASET_ROOT = REPO_ROOT / "src" / "Gasleak"
OUTPUT_ROOT = REPO_ROOT / "improvement_program" / "output"

ACTIVE_BOARDS = [
    "Board1",
    "Board3",
    "Board4",
    "Board5",
    "Board6",
    "Board7",
    "Board9",
    "Board10",
    "Board11",
]

KNOWN_SKIPPED_BOARDS = {
    "Board2-2": "no Board2-2.xlsx dataset found",
    "Board8": "Board8.xlsx is empty",
}

FEATURE_COLS = ["MQ135V", "MQ2V", "MQ3V", "MQ4V", "MQ7V", "MQ5V", "MQ6V", "MQ8V"]
GAS_LABELS = ["normal", "methane", "h2s", "butane", "propane", "co"]
SEVERITY_LABELS = ["normal", "low", "medium", "high"]

PAYLOAD_VERSION = 1
PAYLOAD_STRUCT = struct.Struct("<BBBBHHHHI8h")
PAYLOAD_SIZE_BYTES = PAYLOAD_STRUCT.size


@dataclass
class Prediction:
    board: str
    gas_type: int
    gas_confidence: float
    leak_present: int
    leak_probability: float
    severity: int
    severity_confidence: float
    ppm_estimate: float
    inference_time_us: int
    voltages: list[float]

    @property
    def gas_name(self) -> str:
        return GAS_LABELS[self.gas_type]

    @property
    def severity_name(self) -> str:
        return SEVERITY_LABELS[self.severity]

    def to_dict(self):
        data = asdict(self)
        data["gas_name"] = self.gas_name
        data["severity_name"] = self.severity_name
        return data


def board_dataset_path(board: str) -> Path:
    return DATASET_ROOT / board / f"{board}.xlsx"


def parse_param(sequence) -> tuple[int, int, int, int, int] | None:
    match = re.search(r"param:(\d+)-(\d+)-(\d+)-(\d+)-(\d+)", str(sequence))
    if not match:
        return None
    return tuple(int(item) for item in match.groups())


def gas_type_from_param(param: tuple[int, int, int, int, int]) -> int:
    if param[0] == 1:
        return 1
    if param[1] == 1:
        return 2
    if param[2] == 1:
        return 3
    if param[3] == 1:
        return 4
    if param[4] == 1:
        return 5
    return 0


def load_board_dataset(board: str) -> pd.DataFrame:
    path = board_dataset_path(board)
    df = pd.read_excel(path)
    if "sequence" not in df.columns:
        raise ValueError(f"{path} does not contain a sequence column")

    missing = [col for col in FEATURE_COLS if col not in df.columns]
    if missing:
        raise ValueError(f"{path} missing feature columns: {missing}")

    df["param_tuple"] = df["sequence"].apply(parse_param)
    df = df.dropna(subset=["param_tuple"]).copy()
    for col in FEATURE_COLS:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna(subset=FEATURE_COLS).copy()
    if df.empty:
        raise ValueError(f"{path} has no usable rows")

    df["gas_type"] = df["param_tuple"].apply(gas_type_from_param).astype(int)
    df["leak_present"] = (df["gas_type"] != 0).astype(int)
    return derive_response_targets(df)


def derive_response_targets(df: pd.DataFrame) -> pd.DataFrame:
    normal = df[df["gas_type"] == 0]
    if normal.empty:
        baseline = df[FEATURE_COLS].median()
        spread = df[FEATURE_COLS].std(ddof=0).replace(0, 1.0).fillna(1.0)
    else:
        baseline = normal[FEATURE_COLS].median()
        spread = normal[FEATURE_COLS].std(ddof=0).replace(0, 1.0).fillna(1.0)

    response = ((df[FEATURE_COLS] - baseline).abs() / spread).mean(axis=1)
    df["response_index"] = response.astype(float)
    df["severity"] = 0

    leak_mask = df["leak_present"] == 1
    leak_response = df.loc[leak_mask, "response_index"]
    if len(leak_response) >= 3:
        low_cut, high_cut = leak_response.quantile([1 / 3, 2 / 3]).values
        df.loc[leak_mask & (df["response_index"] <= low_cut), "severity"] = 1
        df.loc[leak_mask & (df["response_index"] > low_cut) & (df["response_index"] <= high_cut), "severity"] = 2
        df.loc[leak_mask & (df["response_index"] > high_cut), "severity"] = 3
    else:
        df.loc[leak_mask, "severity"] = 2

    max_response = float(leak_response.max()) if len(leak_response) else 1.0
    if max_response <= 0:
        max_response = 1.0
    df["ppm_estimate"] = np.where(
        leak_mask,
        np.clip((df["response_index"] / max_response) * 1000.0, 1.0, 1000.0),
        0.0,
    )
    return df


def encode_payload(prediction: Prediction) -> bytes:
    mv = [int(np.clip(round(v * 1000.0), -32768, 32767)) for v in prediction.voltages]
    return PAYLOAD_STRUCT.pack(
        PAYLOAD_VERSION,
        int(prediction.gas_type),
        int(prediction.leak_present),
        int(prediction.severity),
        int(np.clip(round(prediction.gas_confidence * 1000.0), 0, 1000)),
        int(np.clip(round(prediction.leak_probability * 1000.0), 0, 1000)),
        int(np.clip(round(prediction.severity_confidence * 1000.0), 0, 1000)),
        int(np.clip(round(prediction.ppm_estimate), 0, 65535)),
        int(np.clip(prediction.inference_time_us, 0, 2**32 - 1)),
        *mv,
    )


def decode_payload(payload: bytes) -> dict:
    values = PAYLOAD_STRUCT.unpack(payload)
    version, gas_type, leak_present, severity = values[:4]
    gas_conf, leak_prob, sev_conf, ppm, inference_time = values[4:9]
    millivolts = values[9:]
    return {
        "version": version,
        "gas_type": gas_type,
        "gas_name": GAS_LABELS[gas_type] if gas_type < len(GAS_LABELS) else "unknown",
        "leak_present": leak_present,
        "severity": severity,
        "severity_name": SEVERITY_LABELS[severity] if severity < len(SEVERITY_LABELS) else "unknown",
        "gas_confidence": gas_conf / 1000.0,
        "leak_probability": leak_prob / 1000.0,
        "severity_confidence": sev_conf / 1000.0,
        "ppm_estimate": ppm,
        "inference_time_us": inference_time,
        "voltages": [mv / 1000.0 for mv in millivolts],
        "payload_size": len(payload),
    }


def write_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")
