import subprocess
import sys


WIDTH = 1280
HEIGHT = 720
CHROMA_DIVISOR = 4
FRAME_COUNT = 60
FIRST_LUMA = 64
EXPECTED_CHROMA = 128
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
    if len(result.stdout) != expected_size * FRAME_COUNT:
        print(f"expected {FRAME_COUNT} YUV420 frames, got {len(result.stdout)} bytes", file=sys.stderr)
        return 1

    observed_luma = []
    for index in range(FRAME_COUNT):
        start = index * expected_size
        planes = (
            result.stdout[start:start + luma_size],
            result.stdout[start + luma_size:start + luma_size + chroma_size],
            result.stdout[start + luma_size + chroma_size:start + expected_size],
        )
        for name, plane, expected in zip(
            ("Y", "U", "V"), planes, (FIRST_LUMA + index, EXPECTED_CHROMA, EXPECTED_CHROMA)
        ):
            errors = [abs(sample - expected) for sample in plane]
            maximum = max(errors)
            mean = sum(errors) / len(errors)
            if maximum > MAX_SAMPLE_ERROR or mean > MAX_MEAN_ERROR:
                print(f"frame {index} {name} differs: max={maximum}, mean={mean:.2f}", file=sys.stderr)
                return 1
        observed_luma.append(sum(planes[0]) / len(planes[0]))
    if observed_luma[-1] - observed_luma[0] < FRAME_COUNT // 2:
        print("first and last encoded frames are not distinguishable", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
