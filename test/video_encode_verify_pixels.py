import subprocess
import sys


WIDTH = 1280
HEIGHT = 720
CHROMA_DIVISOR = 4
EXPECTED_SAMPLE = 128
MAX_SAMPLE_ERROR = 24
MAX_MEAN_ERROR = 8


def main() -> int:
    ffmpeg, stream = sys.argv[1:]
    result = subprocess.run(
        [ffmpeg, "-v", "error", "-xerror", "-f", "h264", "-i", stream,
         "-f", "rawvideo", "-pix_fmt", "yuv420p", "-"],
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        print(result.stderr.decode(errors="replace"), file=sys.stderr)
        return 1

    luma_size = WIDTH * HEIGHT
    chroma_size = luma_size // CHROMA_DIVISOR
    expected_size = luma_size + 2 * chroma_size
    if len(result.stdout) != expected_size:
        print(f"expected one YUV420 frame ({expected_size} bytes), got {len(result.stdout)}", file=sys.stderr)
        return 1

    planes = (
        result.stdout[:luma_size],
        result.stdout[luma_size:luma_size + chroma_size],
        result.stdout[luma_size + chroma_size:],
    )
    for name, plane in zip(("Y", "U", "V"), planes):
        errors = [abs(sample - EXPECTED_SAMPLE) for sample in plane]
        maximum = max(errors)
        mean = sum(errors) / len(errors)
        if maximum > MAX_SAMPLE_ERROR or mean > MAX_MEAN_ERROR:
            print(f"{name} plane differs from neutral gray: max={maximum}, mean={mean:.2f}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
