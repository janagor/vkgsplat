import subprocess
import sys
import tempfile
from pathlib import Path


SKIPPED = 77


def main() -> int:
    encoder, cmake, verifier, ffmpeg, ffprobe = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="vkgsplat-video-") as directory:
        output = Path(directory) / "synthetic.h264"
        encoded = subprocess.run([encoder, str(output)], check=False)
        if encoded.returncode == SKIPPED:
            return SKIPPED
        if encoded.returncode != 0:
            print(f"synthetic H.264 encoding failed with exit code {encoded.returncode}", file=sys.stderr)
            return 1

        verified = subprocess.run(
            [cmake, f"-DINPUT={output}", f"-DFFMPEG={ffmpeg}",
             f"-DFFPROBE={ffprobe}", f"-DPYTHON={sys.executable}",
             "-P", verifier],
            check=False,
        )
        if verified.returncode != 0:
            print("synthetic H.264 verification failed", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
