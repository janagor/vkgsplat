import json
import subprocess
import sys
import tempfile
from fractions import Fraction
from pathlib import Path


WIDTH = 128
HEIGHT = 128
FRAME_COUNT = 30
ORBIT_DEGREES = 360
FRAME_RATES = ((30, 1), (30000, 1001))
REFERENCE_COUNT = 3
REFERENCE_INDICES = (0, 10, 20)
REFERENCE_NOISE_FACTOR = 2

# Production video capture routes through RenderThread::CaptureVideoFrame,
# GpuColorConverter::Convert, and VideoEncoder::EncodeImage. The RGBA CPU
# readback in ReadFrameOnWorker is used by screenshot/PNG capture instead.


def run(arguments: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise AssertionError(f"command failed: {arguments}\n{result.stdout}\n{result.stderr}")
    return result


def probe(ffprobe: str, output: Path, packets: bool = False) -> dict:
    entries = "stream=codec_name,width,height,nb_read_frames,duration_ts,r_frame_rate,time_base"
    arguments = [ffprobe, "-v", "error", "-count_frames", "-select_streams", "v:0"]
    if packets:
        arguments += ["-show_packets", "-show_entries", f"{entries}:packet=pts,dts,duration,flags"]
    else:
        arguments += ["-show_entries", entries]
    return json.loads(run(arguments + ["-of", "json", str(output)]).stdout)


def decode_gray(ffmpeg: str, output: Path) -> list[bytes]:
    decoded = subprocess.run(
        [ffmpeg, "-v", "error", "-xerror", "-i", str(output), "-f", "rawvideo",
         "-pix_fmt", "gray", "-vsync", "0", "-"],
        capture_output=True, check=False,
    )
    if decoded.returncode != 0:
        raise AssertionError(f"decoding {output} failed: {decoded.stderr.decode(errors='replace')}")
    frame_size = WIDTH * HEIGHT
    if len(decoded.stdout) % frame_size:
        raise AssertionError(f"decoded {output} has a partial frame")
    return [decoded.stdout[index:index + frame_size]
            for index in range(0, len(decoded.stdout), frame_size)]


def check_mp4_timing(probed: dict, frame_count: int, fps_numerator: int,
                     fps_denominator: int) -> None:
    stream = probed["streams"][0]
    packets = probed.get("packets", [])
    time_base = Fraction(stream["time_base"])
    frame_interval = Fraction(fps_denominator, fps_numerator)
    ticks_per_frame = frame_interval / time_base
    if ticks_per_frame.denominator != 1:
        raise AssertionError(f"MP4 time base cannot express the frame interval: {stream}")
    if len(packets) != frame_count:
        raise AssertionError(f"expected {frame_count} MP4 packets, got {len(packets)}")
    for index, packet in enumerate(packets):
        expected_pts = index * ticks_per_frame.numerator
        if (int(packet["pts"]) != expected_pts or int(packet["dts"]) != expected_pts
                or int(packet["duration"]) != ticks_per_frame.numerator
                or "K" not in packet.get("flags", "")):
            raise AssertionError(f"incorrect MP4 packet {index}: {packet}")
    expected_duration = frame_count * frame_interval
    if (int(stream["duration_ts"]) * time_base != expected_duration
            or Fraction(stream["r_frame_rate"]) != Fraction(fps_numerator, fps_denominator)):
        raise AssertionError(f"incorrect MP4 stream timing: {stream}")


def check_keyframes(ffprobe: str, output: Path, frame_count: int) -> None:
    result = run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_frames",
                  "-show_entries", "frame=key_frame,pict_type", "-of", "json", str(output)])
    frames = json.loads(result.stdout).get("frames", [])
    if (len(frames) != frame_count
            or any(frame.get("key_frame") != 1 or frame.get("pict_type") != "I" for frame in frames)):
        raise AssertionError(f"expected {frame_count} independently decodable keyframes: {frames}")


def mean_difference(left: bytes, right: bytes) -> float:
    return sum(abs(a - b) for a, b in zip(left, right)) / len(left)


def check_viewpoints(frames: list[bytes], references: list[bytes], indices: tuple[int, ...]) -> None:
    for reference_index, video_index in enumerate(indices):
        expected_error = mean_difference(frames[video_index], references[reference_index])
        incorrect_errors = [mean_difference(frames[video_index], reference)
                            for index, reference in enumerate(references) if index != reference_index]
        nearest_incorrect_error = min(incorrect_errors)
        margin = nearest_incorrect_error - expected_error
        reference_separation = min(
            mean_difference(references[reference_index], reference)
            for index, reference in enumerate(references) if index != reference_index)
        if (margin <= 0
                or reference_separation <= REFERENCE_NOISE_FACTOR * expected_error):
            raise AssertionError(
                f"video frame {video_index} does not distinguish camera {reference_index}: "
                f"expected error={expected_error:.3f}, nearest incorrect error="
                f"{nearest_incorrect_error:.3f}, margin={margin:.3f}, "
                f"reference separation={reference_separation:.3f}")


def main() -> int:
    executable, ply, ffmpeg, ffprobe, suffix, validation = sys.argv[1:]
    validation_arguments = ["--validation"] if validation.upper() in ("ON", "TRUE", "1") else []
    with tempfile.TemporaryDirectory(prefix="vkgsplat-headless-video-") as directory:
        root = Path(directory)
        common = [executable, "--ply", ply, "--target", "offscreen", "--width", str(WIDTH),
                  "--height", str(HEIGHT), "--orbit-degrees", str(ORBIT_DEGREES)]
        reference_dir = root / "reference"
        run(common + ["--sequence-dir", str(reference_dir), "--frames", str(REFERENCE_COUNT)]
            + validation_arguments)
        reference_files = sorted(reference_dir.glob("*.png"))
        if len(reference_files) != REFERENCE_COUNT:
            raise AssertionError(f"expected {REFERENCE_COUNT} PNG references, got {reference_files}")
        references = [decode_gray(ffmpeg, path)[0] for path in reference_files]
        cases = [(1, *FRAME_RATES[0]), (FRAME_COUNT, *FRAME_RATES[0])]
        if suffix == ".mp4":
            cases.append((FRAME_COUNT, *FRAME_RATES[1]))
        for frame_count, numerator, denominator in cases:
            output = root / f"orbit-{frame_count}-{numerator}-{denominator}{suffix}"
            run(common + ["--output", str(output), "--frames", str(frame_count),
                          "--fps", str(numerator), "--fps-denominator", str(denominator)]
                + validation_arguments)
            if not output.is_file() or output.stat().st_size == 0:
                raise AssertionError(f"video output is missing or empty: {output}")
            probed = probe(ffprobe, output, suffix == ".mp4")
            streams = probed.get("streams", [])
            if len(streams) != 1:
                raise AssertionError(f"expected one video stream, got {streams}")
            stream = streams[0]
            if (stream.get("codec_name") != "h264" or stream.get("width") != WIDTH
                    or stream.get("height") != HEIGHT
                    or int(stream.get("nb_read_frames", 0)) != frame_count):
                raise AssertionError(f"unexpected video stream: {stream}")
            if suffix == ".mp4":
                check_mp4_timing(probed, frame_count, numerator, denominator)
            check_keyframes(ffprobe, output, frame_count)
            frames = decode_gray(ffmpeg, output)
            if len(frames) != frame_count:
                raise AssertionError(f"expected {frame_count} decoded frames, got {len(frames)}")
            if frame_count == 1:
                check_viewpoints(frames, references, (REFERENCE_INDICES[0],))
            if frame_count == FRAME_COUNT:
                if len(set(frames)) < REFERENCE_COUNT:
                    raise AssertionError("video repeats frames across camera viewpoints")
                check_viewpoints(frames, references, REFERENCE_INDICES)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
