# Improved Gas Leak Program

This folder contains the rebuilt program path without modifying the existing `src/Gasleak/Board*` firmware folders.

## Contents

- `gasleak_improved/common.py` shared board config, dataset parsing, target derivation, and LoRa payload codec
- `train_multitask.py` full TensorFlow multi-task training/export pipeline
- `simulate.py` fast local simulation for all active boards
- `firmware/payload_contract.h` C/C++ payload struct and constants for embedded firmware integration

## Active Boards

The active training/simulation boards are:

- `Board1`
- `Board3`
- `Board4`
- `Board5`
- `Board6`
- `Board7`
- `Board9`
- `Board10`
- `Board11`

Skipped boards:

- `Board2-2`: firmware folder exists, but no `Board2-2.xlsx` dataset was found
- `Board8`: `Board8.xlsx` is empty

## Important Limitation

The current Excel datasets do not contain calibrated ppm labels. The `ppm_estimate` output is therefore a board-local response proxy, not a real calibrated concentration.

The production gas class contract is:

- `0` normal
- `1` methane
- `2` H2S
- `3` butane / LPG-related gas
- `4` propane
- `5` CO

The current prototype datasets mostly populate normal, methane, and butane/LPG-related rows. Missing classes remain in the model output contract so new chamber data can be added without changing firmware payload IDs.

## Run Simulation

From the repository root:

```powershell
python improvement_program\simulate.py
```

Outputs are written to:

```text
improvement_program/output/simulation/
```

## Train Full Multi-Task Models

From the repository root:

```powershell
python improvement_program\train_multitask.py
```

Outputs are written to:

```text
improvement_program/output/models/<Board>/
improvement_program/output/reports/<Board>/
```

The training script does not overwrite existing firmware folders. Generated files can be copied into a board firmware folder after review.
