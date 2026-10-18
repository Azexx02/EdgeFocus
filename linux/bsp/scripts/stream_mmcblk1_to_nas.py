# -*- coding: utf-8 -*-
import hashlib
import pathlib
import sys
import time

import paramiko


HOST = "192.168.10.210"
OUT_DIR = pathlib.Path(
    r"\\192.168.10.112\团队文件-嵌入式\MP257_backups\ATK_DLMP257_before_flash_20260630_193813"
)
OUT_FILE = OUT_DIR / "mmcblk1_full.img.gz"
SHA_FILE = OUT_DIR / "mmcblk1_full.img.gz.sha256"
META_FILE = OUT_DIR / "backup_metadata.txt"


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(
        HOST,
        username="root",
        password="",
        timeout=10,
        look_for_keys=False,
        allow_agent=False,
    )

    pre_cmd = (
        "lsblk -no NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS "
        "/dev/mmcblk1 /dev/mmcblk1p* 2>/dev/null; "
        "df -h /; uname -a"
    )
    stdin, stdout, stderr = ssh.exec_command(pre_cmd)
    meta = stdout.read().decode(errors="replace")
    meta_err = stderr.read().decode(errors="replace")
    META_FILE.write_text(
        "Source host: 192.168.10.210\n"
        "Source device: /dev/mmcblk1\n"
        "Command: dd if=/dev/mmcblk1 bs=4M conv=noerror,sync | gzip -1\n\n"
        + meta
        + (("\nSTDERR:\n" + meta_err) if meta_err else ""),
        encoding="utf-8",
    )

    cmd = "dd if=/dev/mmcblk1 bs=4M conv=noerror,sync 2>/tmp/mmcblk1-backup-dd.log | gzip -1"
    stdin, stdout, stderr = ssh.exec_command(cmd)
    chan = stdout.channel
    digest = hashlib.sha256()
    written = 0
    last_report = 0
    start = time.time()

    with OUT_FILE.open("wb") as f:
        while True:
            if chan.recv_ready():
                data = chan.recv(1024 * 1024)
                if data:
                    f.write(data)
                    digest.update(data)
                    written += len(data)
                    if written - last_report >= 100 * 1024 * 1024:
                        elapsed = max(time.time() - start, 0.001)
                        mib = written / 1024 / 1024
                        print(f"{mib:.1f} MiB written, {mib / elapsed:.1f} MiB/s")
                        sys.stdout.flush()
                        last_report = written
            elif chan.exit_status_ready():
                while chan.recv_ready():
                    data = chan.recv(1024 * 1024)
                    if data:
                        f.write(data)
                        digest.update(data)
                        written += len(data)
                break
            else:
                time.sleep(0.05)

    code = chan.recv_exit_status()

    sftp = ssh.open_sftp()
    try:
        sftp.get("/tmp/mmcblk1-backup-dd.log", str(OUT_DIR / "mmcblk1-backup-dd.log"))
    except Exception as exc:
        (OUT_DIR / "mmcblk1-backup-dd-log.error.txt").write_text(str(exc), encoding="utf-8")
    sftp.close()
    ssh.close()

    hex_digest = digest.hexdigest()
    SHA_FILE.write_text(f"{hex_digest}  {OUT_FILE.name}\n", encoding="ascii")

    elapsed = time.time() - start
    mib = written / 1024 / 1024
    print(f"Exit={code}")
    print(f"Wrote={written} bytes ({mib:.1f} MiB)")
    print(f"Elapsed={elapsed:.1f}s, avg={mib / max(elapsed, 0.001):.1f} MiB/s")
    print(f"SHA256={hex_digest}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
