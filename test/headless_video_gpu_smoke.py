import json
import subprocess
import sys
import tempfile
from pathlib import Path


WIDTH = 128
HEIGHT = 128
FRAME_COUNT = 30
FPS = 30
ORBIT_DEGREES = 360
EXPECTED_DURATION_SECONDS = FRAME_COUNT / FPS
MAX_DURATION_ERROR_SECONDS = 0.05


def main() -> int:
    executable, ply, ffmpeg, ffprobe, suffix, validation = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="vkgsplat-headless-video-") as directory:
        output = Path(directory) / f"orbit{suffix}"
        arguments = [executable, "--ply", ply, "--target", "offscreen", "--output", str(output),
                     "--width", str(WIDTH), "--height", str(HEIGHT), "--frames", str(FRAME_COUNT),
                     "--fps", str(FPS), "--orbit-degrees", str(ORBIT_DEGREES)]
        if validation.upper() in ("ON", "TRUE", "1"):
            arguments.append("--validation")
        exported = subprocess.run(
            arguments,
            capture_output=True,
            text=True,
            check=False,
        )
        if exported.returncode != 0:
            print(exported.stdout, exported.stderr, file=sys.stderr)
            return 1
        if not output.is_file() or output.stat().st_size == 0:
            print("headless video output is missing or empty", file=sys.stderr)
            return 1
        probed = subprocess.run(
            [ffprobe, "-v", "error", "-count_frames", "-select_streams", "v:0",
             "-show_entries", "stream=codec_name,width,height,nb_read_frames,duration,r_frame_rate",
             "-of", "json", str(output)],
            capture_output=True,
            text=True,
            check=False,
        )
        if probed.returncode != 0:
            print(probed.stderr, file=sys.stderr)
            return 1
        streams = json.loads(probed.stdout).get("streams", [])
        if len(streams) != 1:
            print(f"expected one video stream, got {streams}", file=sys.stderr)
            return 1
        stream = streams[0]
        if (stream.get("codec_name") != "h264" or stream.get("width") != WIDTH
                or stream.get("height") != HEIGHT
                or int(stream.get("nb_read_frames", 0)) != FRAME_COUNT):
            print(f"unexpected video stream: {stream}", file=sys.stderr)
            return 1
        if suffix == ".mp4":
            if (stream.get("r_frame_rate") != f"{FPS}/1"
                    or abs(float(stream.get("duration", 0)) - EXPECTED_DURATION_SECONDS)
                    > MAX_DURATION_ERROR_SECONDS):
                print(f"incorrect MP4 timing: {stream}", file=sys.stderr)
                return 1
        decoded = subprocess.run(
            [ffmpeg, "-v", "error", "-xerror", "-i", str(output), "-f", "framemd5", "-"],
            capture_output=True,
            text=True,
            check=False,
        )
        if decoded.returncode != 0:
            print(decoded.stderr, file=sys.stderr)
            return 1
        hashes = [line.rsplit(",", 1)[-1].strip()
                  for line in decoded.stdout.splitlines() if line and not line.startswith("#")]
        if len(hashes) != FRAME_COUNT or len(set(hashes)) < 2:
            print("decoded video has missing or identical frames", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
