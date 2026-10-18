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
import shutil
import subprocess
import tempfile
import time
import zipfile


ROOT = Path(__file__).resolve().parent
SDK_ENV = "/opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux"
REMOTE_DEPS = "/home/hu/.cache/edgefocus-deps"


def run(command: list[str], *, dry_run: bool = False) -> None:
    print("+", " ".join(command))
    if not dry_run:
        subprocess.run(command, check=True)


def source_archive(destination: Path) -> None:
    excluded_dirs = {
        ".git", "build", "build-arm", "build-national-host", ".superpowers",
        "_workspace", "__pycache__", "board_backups"
    }
    excluded_suffixes = {".obj", ".pdb", ".lib", ".exe", ".zip", ".mp4"}
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


def sync_demo_media(board: str, *, dry_run: bool = False) -> None:
    """Atomically copy only changed demo media/trace artifacts."""
    demo_root = ROOT / "assets" / "demo"
    artifacts = sorted(demo_root.glob("p[0-9][0-9].mp4"))
    artifacts += sorted((demo_root / "no_npu" / "segments").glob("r[0-9][0-9].mp4"))
    artifacts += sorted((demo_root / "no_npu" / "segments").glob("r[0-9][0-9].trace.json"))
    if not artifacts:
        return
    run(["ssh", "-o", "BatchMode=yes", board,
         "mkdir -p /opt/edgefocus/assets/demo"], dry_run=dry_run)
    for path in artifacts:
        relative = path.relative_to(demo_root).as_posix()
        remote = f"/opt/edgefocus/assets/demo/{relative}"
        remote_parent = str(Path(remote).parent).replace("\\", "/")
        run(["ssh", "-o", "BatchMode=yes", board,
             f"mkdir -p {shlex.quote(remote_parent)}"], dry_run=dry_run)
        digest = sha256(path)
        check = subprocess.run(
            ["ssh", "-o", "BatchMode=yes", board,
             f"test -f {shlex.quote(remote)} && sha256sum {shlex.quote(remote)}"],
            check=False, capture_output=True, text=True) if not dry_run else None
        if check and check.returncode == 0 and check.stdout.split(maxsplit=1)[0] == digest:
            print(f"= unchanged demo artifact {relative}")
            continue
        temporary = remote + ".uploading"
        run(["scp", str(path), f"{board}:{temporary}"], dry_run=dry_run)
        run(["ssh", "-o", "BatchMode=yes", board,
             f"test \"$(sha256sum {shlex.quote(temporary)} | cut -d' ' -f1)\" = {digest} && "
             f"mv -f {shlex.quote(temporary)} {shlex.quote(remote)}"], dry_run=dry_run)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="hu@192.168.10.6")
    parser.add_argument("--board", default="root@192.168.10.174")
    parser.add_argument("--demo-port", type=int, default=18080)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--build-only-output",
        help="cross-build and copy the release bundle here without touching the board",
    )
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
            f"set -e; mkdir -p {compiler_root} {REMOTE_DEPS}; "
            f"if ! test -f {REMOTE_DEPS}/sqlite3/sqlite3.c; then "
            "sqlite_src=$(find /tmp /home/hu -path '*/_deps/sqlite3-src/sqlite3.c' -print -quit); "
            "test -n \"$sqlite_src\"; cp -a \"$(dirname \"$sqlite_src\")\" "
            f"{REMOTE_DEPS}/sqlite3; fi; "
            f"if ! test -f {REMOTE_DEPS}/json/single_include/nlohmann/json.hpp; then "
            "json_src=$(find /tmp /home/hu -path '*/_deps/json-src/single_include/nlohmann/json.hpp' -print -quit); "
            "test -n \"$json_src\"; json_root=${json_src%/single_include/nlohmann/json.hpp}; "
            f"cp -a \"$json_root\" {REMOTE_DEPS}/json; fi; "
            f"cd {compiler_root}; "
            f"unzip -q {compiler_root}.zip; source {SDK_ENV}; "
            "cmake -S . -B build-arm -DCMAKE_BUILD_TYPE=Release "
            "-DEDGEFOCUS_WITH_OPENCV=ON -DEDGEFOCUS_WITH_STAI_MPU=ON "
            "-DEDGEFOCUS_WITH_DEMO_HTTP=ON -DEDGEFOCUS_WITH_OPENH264=ON "
            f"-DFETCHCONTENT_SOURCE_DIR_SQLITE3={REMOTE_DEPS}/sqlite3 "
            f"-DFETCHCONTENT_SOURCE_DIR_JSON={REMOTE_DEPS}/json; "
            # Runtime deployment builds only installed executables. Host CI/tests
            # remain the compile-time contract and do not belong in the bundle.
            "cmake --build build-arm --target focus_app gimbal_bringup edgefocus-fan-bridge -j4; "
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
        if args.build_only_output:
            output = Path(args.build_only_output).expanduser().resolve()
            if not args.dry_run:
                output.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(bundle, output)
                print("build-only bundle:", output)
            return 0
        run(["scp", str(bundle), f"{args.board}:{board_archive}"], dry_run=args.dry_run)

        deploy_command = (
            "set -e; test -d /opt/edgefocus; "
            f"mkdir -p {board_release} {board_release}/rollback; "
            f"tar -xzf {board_archive} -C {board_release}; "
            f"test -x {board_release}/bin/focus_app; "
            f"test -f {board_release}/models/distraction/yolo11n_pose_256_float.nb; "
            f"test -f {board_release}/models/distraction/prone_mlp_v1.json; "
            f"test -f {board_release}/config/gimbal-board.json; "
            f"test -f {board_release}/lib/libopenh264.so.8; "
            f"cp -a /opt/edgefocus/focus_app {board_release}/rollback/focus_app; "
            f"cp -a /opt/edgefocus/models {board_release}/rollback/models; "
            f"cp -a /etc/systemd/system/focus_app.service {board_release}/rollback/focus_app.service; "
            f"if test -d /opt/edgefocus/config; then cp -a /opt/edgefocus/config {board_release}/rollback/config; fi; "
            # Demo videos are immutable operator-provided media and dominate disk
            # usage.  Deploy copies new traces additively, so rollback does not
            # duplicate the entire media library for every release.
            f"if test -d /opt/edgefocus/lib; then cp -a /opt/edgefocus/lib {board_release}/rollback/lib; fi; "
            "systemctl stop focus_app; "
            # BlueZ can retain an in-progress discovery owned by the bracelet
            # client after focus_app exits. Reset it while the app is stopped so
            # the phone advertisement registers before bracelet discovery resumes.
            "systemctl restart bluetooth; sleep 2; "
            f"install -m 755 {board_release}/bin/focus_app /opt/edgefocus/focus_app; "
            "mkdir -p /opt/edgefocus/models/distraction; "
            f"cp -a {board_release}/models/distraction/. /opt/edgefocus/models/distraction/; "
            "mkdir -p /opt/edgefocus/config; "
            f"cp -a {board_release}/config/. /opt/edgefocus/config/; "
            "mkdir -p /opt/edgefocus/assets/demo; "
            f"cp -a {board_release}/assets/demo/. /opt/edgefocus/assets/demo/; "
            "mkdir -p /opt/edgefocus/lib; "
            f"cp -a {board_release}/lib/. /opt/edgefocus/lib/; "
            "sed -i '/^Environment=EDGEFOCUS_DISABLE_GIMBAL=1$/d' /etc/systemd/system/focus_app.service; "
            "systemctl daemon-reload; "
            "if systemctl start focus_app && sleep 4 && "
            "systemctl is-active --quiet focus_app && "
            f"curl -fsS --max-time 3 http://127.0.0.1:{args.demo_port}/api/demo/state; then "
            "true; else "
            f"install -m 755 {board_release}/rollback/focus_app /opt/edgefocus/focus_app; "
            f"cp -a {board_release}/rollback/models/. /opt/edgefocus/models/; "
            f"if test -d {board_release}/rollback/config; then cp -a {board_release}/rollback/config/. /opt/edgefocus/config/; fi; "
            f"if test -d {board_release}/rollback/lib; then cp -a {board_release}/rollback/lib/. /opt/edgefocus/lib/; fi; "
            f"cp -a {board_release}/rollback/focus_app.service /etc/systemd/system/focus_app.service; "
            "systemctl daemon-reload; "
            "systemctl start focus_app; exit 1; fi; "
            "systemctl is-active focus_app"
        )
        run(["ssh", "-o", "BatchMode=yes", args.board, deploy_command], dry_run=args.dry_run)
        sync_demo_media(args.board, dry_run=args.dry_run)

    print(f"deployed release {release} to {args.board}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
