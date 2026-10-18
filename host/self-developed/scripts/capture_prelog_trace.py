#!/usr/bin/env python3
"""Capture one continuous board-NPU pass and split it into replay trace files."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from typing import Any
from urllib.error import URLError
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REMOTE_VIDEO = "/opt/edgefocus/assets/demo/no_npu/prelog_source.mp4"
PHONE_EVENTS = [
    (0, "OFF"),
    (302_000, "ENTERTAINMENT"),
    (322_000, "GENERAL_OR_UNKNOWN"),
    (342_000, "LEARNING"),
    (352_000, "OFF"),
]


def request_json(url: str, data: dict[str, Any] | None = None) -> dict[str, Any]:
    payload = None if data is None else json.dumps(data).encode("utf-8")
    request = Request(url, data=payload)
    if payload is not None:
        request.add_header("Content-Type", "application/json")
    with urlopen(request, timeout=4) as response:
        return json.loads(response.read().decode("utf-8"))


def ssh(board: str, command: str) -> None:
    subprocess.run(
        ["ssh", "-o", "BatchMode=yes", board, command],
        check=True,
    )


def wait_for_http(base_url: str, timeout_s: float = 25.0) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            request_json(f"{base_url}/api/demo/state")
            return
        except (OSError, URLError, json.JSONDecodeError):
            time.sleep(0.5)
    raise TimeoutError("focus_app HTTP service did not become ready")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, separators=(",", ":")),
        encoding="utf-8",
    )
    temporary.replace(path)


def sample_from_state(state: dict[str, Any]) -> dict[str, Any]:
    media_ms = int(state["source"].get("media_timestamp_ms") or 0)
    return {
        "media_timestamp_ms": media_ms,
        "visual": state["visual"],
        "phone": state["phone"],
        "assessment": state["assessment"],
    }


def split_trace(full_trace: dict[str, Any], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    samples = full_trace["samples"]
    for index in range(12):
        start_ms = index * 30_000
        end_ms = start_ms + 30_000
        clip_samples = []
        for source in samples:
            timestamp = int(source["media_timestamp_ms"])
            if not start_ms <= timestamp < end_ms:
                continue
            sample = json.loads(json.dumps(source))
            sample["media_timestamp_ms"] = timestamp - start_ms
            sample["visual"]["media_timestamp_ms"] = max(
                0, int(sample["visual"].get("media_timestamp_ms") or timestamp) - start_ms
            )
            clip_samples.append(sample)
        trace = {
            "version": 1,
            "source": "BOARD_NPU_PRELOG",
            "clip": f"r{index + 1:02d}.mp4",
            "duration_ms": 30_000,
            "model": full_trace["model"],
            "video_sha256": full_trace["video_sha256"],
            "phone_schedule_source": "manual",
            "samples": clip_samples,
        }
        if not clip_samples:
            raise RuntimeError(f"no NPU samples captured for r{index + 1:02d}.mp4")
        write_json(output_dir / f"r{index + 1:02d}.trace.json", trace)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", default="root@192.168.10.174")
    parser.add_argument("--base-url", default="http://192.168.10.174:18080")
    parser.add_argument("--remote-video", default=DEFAULT_REMOTE_VIDEO)
    parser.add_argument("--duration-ms", type=int, default=360_000)
    args = parser.parse_args()

    local_video = ROOT / "assets" / "demo" / "no_npu" / "预录.mp4"
    output_dir = ROOT / "assets" / "demo" / "no_npu" / "segments"
    raw_path = ROOT / "assets" / "demo" / "no_npu" / "prelog.trace.json"
    partial_path = raw_path.with_suffix(".partial.json")
    samples: list[dict[str, Any]] = []
    last_frame_id = -1
    next_phone_event = 0
    last_progress_s = -1

    print("Configuring focus_app for one-pass pre-log input")
    ssh(
        args.board,
        "systemctl set-environment "
        f"EDGEFOCUS_DEMO_VIDEO={args.remote_video}; systemctl restart focus_app",
    )
    try:
        wait_for_http(args.base_url)
        start = request_json(f"{args.base_url}/api/demo/mode", {"mode": "recorded"})
        if not start.get("success"):
            raise RuntimeError(f"cannot start recorded NPU mode: {start}")
        wall_deadline = time.monotonic() + args.duration_ms / 1000.0 + 45.0
        while time.monotonic() < wall_deadline:
            state = request_json(f"{args.base_url}/api/demo/state")
            mode = state["source"]["mode"]
            if mode == "FRONTEND_REPLAY":
                raise RuntimeError(
                    "NPU pre-log unexpectedly entered fallback: "
                    + str(state.get("health", {}).get("fallback_reason"))
                )
            media_ms = int(state["source"].get("media_timestamp_ms") or 0)
            while (
                next_phone_event < len(PHONE_EVENTS)
                and media_ms >= PHONE_EVENTS[next_phone_event][0]
            ):
                _, category = PHONE_EVENTS[next_phone_event]
                response = request_json(
                    f"{args.base_url}/api/demo/phone", {"category": category}
                )
                if not response.get("accepted"):
                    raise RuntimeError(f"phone event rejected: {response}")
                print(f"phone @ {media_ms} ms -> {category}")
                next_phone_event += 1
            frame_id = int(state["visual"].get("frame_id") or 0)
            if frame_id > 0 and frame_id != last_frame_id:
                last_frame_id = frame_id
                samples.append(sample_from_state(state))
                if len(samples) % 100 == 0:
                    write_json(partial_path, {"samples": samples})
            progress_s = media_ms // 5000
            if progress_s != last_progress_s:
                last_progress_s = progress_s
                print(
                    f"media={media_ms / 1000:6.1f}s "
                    f"samples={len(samples):4d} "
                    f"npu={state['health'].get('inference_fps', 0):.1f}fps"
                )
            if media_ms >= args.duration_ms - 600:
                break
            time.sleep(0.12)
        else:
            raise TimeoutError("pre-log capture exceeded the wall-clock deadline")

        full_trace = {
            "version": 1,
            "source": "BOARD_NPU_PRELOG",
            "duration_ms": args.duration_ms,
            "model": "yolo11n_pose_256_float.nb + prone_mlp_v1.json",
            "video_sha256": sha256(local_video),
            "phone_schedule": PHONE_EVENTS,
            "samples": samples,
        }
        write_json(raw_path, full_trace)
        split_trace(full_trace, output_dir)
        partial_path.unlink(missing_ok=True)
        print(f"Captured {len(samples)} samples -> {raw_path}")
    finally:
        try:
            request_json(f"{args.base_url}/api/demo/mode", {"mode": "live"})
        except Exception:
            pass
        ssh(
            args.board,
            "systemctl unset-environment EDGEFOCUS_DEMO_VIDEO; "
            "systemctl restart focus_app",
        )
        wait_for_http(args.base_url)
        print("Restored normal focus_app environment")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
