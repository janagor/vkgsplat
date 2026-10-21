import subprocess
import sys
import tempfile
from pathlib import Path


SKIPPED = 77
WIDTH = 1280
HEIGHT = 720
CHROMA_DIVISOR = 2
SAMPLE_RADIUS = 4
MAX_LUMA_ERROR = 24
MAX_CHROMA_ERROR = 32
MAX_NV12_ERROR = 2
MISSING_CR_TOLERANCE = 4
FRAME_SOURCES = ("GPU image", "CPU upload")
AFFECTED_COLORS = ("red", "green")
QUARTER_DIVISOR = 4
THREE_QUARTERS = 3
LUMA_OFFSET = 16.0
CHROMA_OFFSET = 128.0
LUMA_RANGE = 219.0
CHROMA_RANGE = 224.0
BT709_LUMA = (0.2126, 0.7152, 0.0722)
BT709_CB = (-0.114572, -0.385428, 0.5)
BT709_CR = (0.5, -0.454153, -0.045847)
COLORS = (
    (1.0, 0.0, 0.0),
    (0.0, 1.0, 0.0),
    (0.0, 0.0, 1.0),
    (1.0, 1.0, 1.0),
)


def expected_yuv(rgb: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        LUMA_OFFSET + LUMA_RANGE * sum(value * coefficient for value, coefficient in zip(rgb, BT709_LUMA)),
        CHROMA_OFFSET + CHROMA_RANGE * sum(value * coefficient for value, coefficient in zip(rgb, BT709_CB)),
        CHROMA_OFFSET + CHROMA_RANGE * sum(value * coefficient for value, coefficient in zip(rgb, BT709_CR)),
    )


def sample_mean(plane: bytes, width: int, x: int, y: int) -> float:
    samples = (
        plane[row * width + column]
        for row in range(y - SAMPLE_RADIUS, y + SAMPLE_RADIUS)
        for column in range(x - SAMPLE_RADIUS, x + SAMPLE_RADIUS)
    )
    values = list(samples)
    return sum(values) / len(values)


def check_nv12(data: bytes) -> bool:
    y_size = WIDTH * HEIGHT
    if len(data) != y_size + y_size // CHROMA_DIVISOR:
        print(f"unexpected NV12 size: {len(data)}", file=sys.stderr)
        return False
    reference = tuple(expected_yuv(color) for color in COLORS)
    for y in range(HEIGHT):
        row = y * WIDTH
        top_or_bottom = CHROMA_DIVISOR if y >= HEIGHT // CHROMA_DIVISOR else 0
        for x in range(WIDTH):
            color = top_or_bottom + (1 if x >= WIDTH // CHROMA_DIVISOR else 0)
            if abs(data[row + x] - reference[color][0]) > MAX_NV12_ERROR:
                print(f"NV12 Y mismatch at ({x}, {y})", file=sys.stderr)
                return False
    for y in range(0, HEIGHT, CHROMA_DIVISOR):
        row = y_size + y // CHROMA_DIVISOR * WIDTH
        top_or_bottom = CHROMA_DIVISOR if y >= HEIGHT // CHROMA_DIVISOR else 0
        for x in range(0, WIDTH, CHROMA_DIVISOR):
            color = top_or_bottom + (1 if x >= WIDTH // CHROMA_DIVISOR else 0)
            if (abs(data[row + x] - reference[color][1]) > MAX_NV12_ERROR
                    or abs(data[row + x + 1] - reference[color][2]) > MAX_NV12_ERROR):
                print(f"NV12 UV mismatch at ({x}, {y})", file=sys.stderr)
                return False
    return True


def main() -> int:
    encoder, ffmpeg = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="vkgsplat-color-") as directory:
        stream = Path(directory) / "converted.h264"
        encoded = subprocess.run([encoder, str(stream), "--color-conversion"], check=False)
        if encoded.returncode == SKIPPED:
            return SKIPPED
        if encoded.returncode != 0:
            print("GPU color conversion encode failed", file=sys.stderr)
            return 1
        if not check_nv12(Path(f"{stream}.nv12").read_bytes()):
            return 1
        decoded = subprocess.run(
            [ffmpeg, "-v", "error", "-xerror", "-f", "h264", "-i", str(stream),
             "-f", "rawvideo", "-pix_fmt", "yuv420p", "-"],
            capture_output=True,
            check=False,
        )
        if decoded.returncode != 0:
            print(decoded.stderr.decode(errors="replace"), file=sys.stderr)
            return 1
        y_size = WIDTH * HEIGHT
        uv_size = y_size // (CHROMA_DIVISOR * CHROMA_DIVISOR)
        expected_size = y_size + CHROMA_DIVISOR * uv_size
        if len(decoded.stdout) != expected_size * len(FRAME_SOURCES):
            print(f"expected {len(FRAME_SOURCES)} YUV420 frames, got {len(decoded.stdout)} bytes", file=sys.stderr)
            return 1
        locations = (
            (WIDTH // QUARTER_DIVISOR, HEIGHT // QUARTER_DIVISOR),
            (WIDTH * THREE_QUARTERS // QUARTER_DIVISOR, HEIGHT // QUARTER_DIVISOR),
            (WIDTH // QUARTER_DIVISOR, HEIGHT * THREE_QUARTERS // QUARTER_DIVISOR),
            (WIDTH * THREE_QUARTERS // QUARTER_DIVISOR,
             HEIGHT * THREE_QUARTERS // QUARTER_DIVISOR),
        )
        failures = []
        neutral_cr_samples = set()
        for frame_index, source in enumerate(FRAME_SOURCES):
            frame = decoded.stdout[frame_index * expected_size:(frame_index + 1) * expected_size]
            planes = (
                frame[:y_size],
                frame[y_size:y_size + uv_size],
                frame[y_size + uv_size:],
            )
            for name, rgb, (x, y) in zip(("red", "green", "blue", "white"), COLORS, locations):
                reference = expected_yuv(rgb)
                observed = (
                    sample_mean(planes[0], WIDTH, x, y),
                    sample_mean(planes[1], WIDTH // CHROMA_DIVISOR,
                                x // CHROMA_DIVISOR, y // CHROMA_DIVISOR),
                    sample_mean(planes[2], WIDTH // CHROMA_DIVISOR,
                                x // CHROMA_DIVISOR, y // CHROMA_DIVISOR),
                )
                for component, actual, expected, tolerance in zip(
                    ("Y", "U", "V"), observed, reference,
                    (MAX_LUMA_ERROR, MAX_CHROMA_ERROR, MAX_CHROMA_ERROR),
                ):
                    if abs(actual - expected) > tolerance:
                        failures.append(
                            f"{source} {name} {component}: got {actual:.1f}, expected {expected:.1f}")
                        if (component == "V" and name in AFFECTED_COLORS
                                and abs(actual - CHROMA_OFFSET) <= MISSING_CR_TOLERANCE):
                            neutral_cr_samples.add((source, name))
        if failures:
            print("\n".join(failures), file=sys.stderr)
            if len(neutral_cr_samples) == len(FRAME_SOURCES) * len(AFFECTED_COLORS):
                print(
                    "This matches the Mesa ANV missing-Cr symptom on Intel: the NV12 input passed "
                    "the CPU reference check, "
                    "but H.264 output has neutral Cr (V near 128) for red and green in both GPU-image "
                    "and CPU-upload paths. The shared Vulkan Video encode path needs investigation; "
                    "the test remains failed so color corruption is visible.",
                    file=sys.stderr,
                )
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
