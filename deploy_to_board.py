#!/usr/bin/env python3
"""Cross-build and atomically deploy focus_app to the STM32MP257 board.

The script intentionally does not contain passwords. Configure SSH keys for the
compiler and board before running it.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import time
import zipfile


ROOT = Path(__file__).resolve().parent
SDK_ENV = "/opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux"


def run(command: list[str], *, dry_run: bool = False) -> None:
    print("+", " ".join(command))
    if not dry_run:
        subprocess.run(command, check=True)


def source_archive(destination: Path) -> None:
    excluded_dirs = {".git", "build", "build-arm", ".superpowers", "__pycache__"}
    excluded_suffixes = {".obj", ".pdb", ".lib", ".exe", ".zip"}
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in ROOT.rglob("*"):
            relative = path.relative_to(ROOT)
            if path.is_dir() or any(part in excluded_dirs for part in relative.parts):
                continue
            if path.suffix.lower() in excluded_suffixes:
                continue
            archive.write(path, relative.as_posix())


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="hu@192.168.10.6")
    parser.add_argument("--board", default="root@192.168.10.174")
    parser.add_argument("--demo-port", type=int, default=18080)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    release = time.strftime("%Y%m%d-%H%M%S")
    compiler_root = f"/tmp/edgefocus-{release}"
    board_archive = f"/tmp/edgefocus-{release}.tar.gz"
    board_release = f"/opt/edgefocus/releases/{release}"

    with tempfile.TemporaryDirectory(prefix="edgefocus-deploy-") as temp_text:
        temp = Path(temp_text)
        archive = temp / "focus_app_src.zip"
        bundle = temp / "focus_app_release.tar.gz"
        source_archive(archive)

        run(["scp", str(archive), f"{args.compiler}:{compiler_root}.zip"], dry_run=args.dry_run)
        compile_command = (
            f"set -e; mkdir -p {compiler_root}; cd {compiler_root}; "
            f"unzip -q {compiler_root}.zip; source {SDK_ENV}; "
            "cmake -S . -B build-arm -DCMAKE_BUILD_TYPE=Release "
            "-DEDGEFOCUS_WITH_OPENCV=ON -DEDGEFOCUS_WITH_STAI_MPU=ON "
            "-DEDGEFOCUS_WITH_DEMO_HTTP=ON; "
            "cmake --build build-arm --target focus_app -j4; "
            "DESTDIR=$PWD/stage cmake --install build-arm; "
            "tar -C stage/usr/local -czf focus_app_release.tar.gz ."
        )
        remote_compile = f"bash -lc {shlex.quote(compile_command)}"
        run(["ssh", "-o", "BatchMode=yes", args.compiler, remote_compile],
            dry_run=args.dry_run)
        run(["scp", f"{args.compiler}:{compiler_root}/focus_app_release.tar.gz", str(bundle)],
            dry_run=args.dry_run)
        if not args.dry_run:
            print("bundle sha256:", sha256(bundle))
        run(["scp", str(bundle), f"{args.board}:{board_archive}"], dry_run=args.dry_run)

        deploy_command = (
            "set -e; test -d /opt/edgefocus; "
            f"mkdir -p {board_release} {board_release}/rollback; "
            f"tar -xzf {board_archive} -C {board_release}; "
            f"test -x {board_release}/bin/focus_app; "
            f"test -f {board_release}/models/distraction/yolo11n_pose_256_float.nb; "
            f"test -f {board_release}/models/distraction/prone_mlp_v1.json; "
            f"cp -a /opt/edgefocus/focus_app {board_release}/rollback/focus_app; "
            f"cp -a /opt/edgefocus/models {board_release}/rollback/models; "
            "systemctl stop focus_app; "
            f"install -m 755 {board_release}/bin/focus_app /opt/edgefocus/focus_app; "
            "mkdir -p /opt/edgefocus/models/distraction; "
            f"cp -a {board_release}/models/distraction/. /opt/edgefocus/models/distraction/; "
            "if systemctl start focus_app && sleep 4 && "
            "systemctl is-active --quiet focus_app && "
            f"curl -fsS --max-time 3 http://127.0.0.1:{args.demo_port}/api/demo/state; then "
            "true; else "
            f"install -m 755 {board_release}/rollback/focus_app /opt/edgefocus/focus_app; "
            f"cp -a {board_release}/rollback/models/. /opt/edgefocus/models/; "
            "systemctl start focus_app; exit 1; fi; "
            "systemctl is-active focus_app"
        )
        run(["ssh", "-o", "BatchMode=yes", args.board, deploy_command], dry_run=args.dry_run)

    print(f"deployed release {release} to {args.board}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
