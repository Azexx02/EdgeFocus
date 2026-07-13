# Distraction Core Implementation Plan

> Design source: `../specs/2026-07-13-distraction-core-design.md`

## Goal

Build a standalone Python project at `C:\Users\Aze\Desktop\self_developed\code\distraction_core` that can replay local videos, produce timestamped visual evidence, fuse it with a manual phone timeline, emit deterministic JSONL assessments, and evaluate typed episodes before any hardware port.

## Implementation order

### Task 1: Project and contract foundation

Files:

- `pyproject.toml`
- `src/distraction_core/contracts.py`
- `src/distraction_core/config.py`
- `configs/default.yaml`
- `tests/test_contracts.py`

Work:

1. Define enums and immutable dataclasses for frame metadata, evidence, phone observations, typed episodes and assessments.
2. Validate nullability, ranges, timestamp monotonic assumptions and category/active consistency.
3. Add deterministic JSON serialization.
4. Write contract validation tests first, then implement until green.

### Task 2: Time-based episode and fusion core

Files:

- `src/distraction_core/fusion/episodes.py`
- `src/distraction_core/fusion/core.py`
- `tests/test_episodes.py`
- `tests/test_fusion.py`

Work:

1. Implement typed `PRONE`, `HEAD_AWAY` and `NOT_PRESENT` episode machines using media time, not frame counts.
2. Implement freshness expiry and 1,000 ms evidence-gap tolerance without accruing gap duration.
3. Implement `ACTIVE/UNKNOWN/NOT_PRESENT` sufficiency matrix.
4. Implement dynamic 0–3 level with asymmetric escalation/recovery, reason lists and `would_remind` proxy events.
5. Cover variable FPS, stale evidence, overlapping reasons, learning/entertainment phone inputs and deterministic replay.

### Task 3: Replay, phone timeline and JSONL CLI

Files:

- `src/distraction_core/capture/video.py`
- `src/distraction_core/simulation/phone.py`
- `src/distraction_core/pipeline.py`
- `src/distraction_core/cli.py`
- `tests/test_phone_timeline.py`
- `tests/test_pipeline.py`

Work:

1. Read media timestamps and normalize rotation/mirroring.
2. Load manual phone observations from YAML/JSON.
3. Feed adapters and fusion in timestamp order.
4. Emit one assessment per processed sample as JSONL.
5. Provide a fast deterministic replay command independent of wall-clock speed.

### Task 4: Adapter boundary and deterministic test adapters

Files:

- `src/distraction_core/adapters/base.py`
- `src/distraction_core/adapters/scripted.py`
- `tests/golden/*.json`
- `tests/test_golden_replay.py`

Work:

1. Define isolated adapter error/health semantics.
2. Add scripted evidence adapters for complete fusion testing without pretending they are visual models.
3. Create golden traces for normal study, transient head turn, sustained head away, prone, absence, unknown gaps and phone categories.
4. Compare normalized business trajectories while excluding runtime latency.

### Task 5: Real person/Pose baseline

Files:

- `src/distraction_core/adapters/ultralytics_pose.py`
- `src/distraction_core/features/keypoints.py`
- `configs/models.yaml`
- `tests/test_keypoint_features.py`

Work:

1. Use a real pretrained Ultralytics pose model for person box and keypoints.
2. Select the primary subject by seat ROI and temporal continuity.
3. Save normalized keypoint vectors, visibility masks and desk-relative features.
4. Surface missing weights or inference failures as invalid evidence; never use a brightness fallback.

### Task 6: Real face/head-pose baseline

Files:

- `src/distraction_core/adapters/face_head.py`
- `src/distraction_core/calibration.py`
- `tests/test_head_away.py`

Work:

1. Choose an available landmark backend behind one adapter interface.
2. Calibrate neutral yaw from a three-second quality window.
3. Produce continuous `HEAD_AWAY` evidence and diagnostic yaw/pitch.
4. Verify rotation/mirror handling and never equate face loss with head-away.

### Task 7: Learned keypoint sequence classifier

Files:

- `src/distraction_core/learning/dataset.py`
- `src/distraction_core/learning/model.py`
- `tools/extract_features.py`
- `tools/train_pose_state.py`
- `tests/test_sequence_dataset.py`

Work:

1. Build fixed-duration windows with missing masks and no cross-session leakage.
2. Train a small MLP/TCN candidate for `NORMAL_STUDY/PRONE/AMBIGUOUS`.
3. Export model metadata and held-out metrics.
4. Promote to runtime only if it meets the frozen-set false-reminder gate and does not underperform the explicit baseline.

### Task 8: Evaluation and demo freeze tools

Files:

- `src/distraction_core/evaluation/episodes.py`
- `src/distraction_core/evaluation/metrics.py`
- `tools/evaluate.py`
- `tools/render_overlay.py`
- `tests/test_metrics.py`

Work:

1. Match typed episodes by kind and IoU.
2. Report precision, recall, P95 latency, false would-reminds, transition jitter, UNKNOWN coverage and model timing.
3. Render explainable overlays and prepare one-command demo inputs.
4. Freeze configs, hashes, dependency lock and known limitations on day 4.

## Verification commands

```powershell
python -m unittest discover -s tests -v
python -m distraction_core replay --input <video> --config configs/default.yaml --output outputs/run.jsonl
python tools/evaluate.py --predictions outputs/run.jsonl --labels <labels.json>
```

## Delivery gates

- Every phase must leave tests green.
- Missing optional models degrade only their own coverage.
- No real adapter may silently become a heuristic adapter.
- Do not start hardware or old-product integration before the host acceptance report exists.
