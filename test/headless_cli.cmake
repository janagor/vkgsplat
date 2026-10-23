if(NOT DEFINED EXECUTABLE OR NOT DEFINED OUTPUT_DIR)
  message(FATAL_ERROR "EXECUTABLE and OUTPUT_DIR are required")
endif()

set(ply_path "${OUTPUT_DIR}/headless-cli-placeholder.ply")
file(WRITE "${ply_path}" "ply\n")

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --width 0 --output "${OUTPUT_DIR}/headless-cli-invalid.png"
  RESULT_VARIABLE dimensions_result
  ERROR_VARIABLE dimensions_error)
if(dimensions_result EQUAL 0
   OR NOT
      dimensions_error
      MATCHES
      "positive width and height")
  message(FATAL_ERROR "Zero-width CLI invocation did not report invalid dimensions: ${dimensions_error}")
endif()

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --height 0 --output "${OUTPUT_DIR}/headless-cli-invalid.png"
  RESULT_VARIABLE dimensions_result
  ERROR_VARIABLE dimensions_error)
if(dimensions_result EQUAL 0
   OR NOT
      dimensions_error
      MATCHES
      "positive width and height")
  message(FATAL_ERROR "Zero-height CLI invocation did not report invalid dimensions: ${dimensions_error}")
endif()

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --output "${OUTPUT_DIR}/headless-cli-invalid.png" --camera-position 0 1
          invalid
  RESULT_VARIABLE camera_result
  ERROR_VARIABLE camera_error)
if(camera_result EQUAL 0
   OR NOT
      camera_error
      MATCHES
      "camera-position")
  message(FATAL_ERROR "Malformed camera coordinates were not rejected: ${camera_error}")
endif()

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --target headless-surface --sequence-dir "${OUTPUT_DIR}/invalid-sequence"
          --frames 2
  RESULT_VARIABLE sequence_target_result
  ERROR_VARIABLE sequence_target_error)
if(sequence_target_result EQUAL 0
   OR NOT
      sequence_target_error
      MATCHES
      "Invalid output mode")
  message(FATAL_ERROR "Sequence mode accepted a headless surface: ${sequence_target_error}")
endif()

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --target offscreen --sequence-dir "${OUTPUT_DIR}/invalid-sequence"
          --frames 0
  RESULT_VARIABLE sequence_count_result
  ERROR_VARIABLE sequence_count_error)
if(sequence_count_result EQUAL 0
   OR NOT
      sequence_count_error
      MATCHES
      "frame count")
  message(FATAL_ERROR "Sequence mode accepted zero frames: ${sequence_count_error}")
endif()

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --target headless-surface --output "${OUTPUT_DIR}/invalid-video.mp4"
          --frames 2
  RESULT_VARIABLE video_target_result
  ERROR_VARIABLE video_target_error)
if(video_target_result EQUAL 0
   OR NOT
      video_target_error
      MATCHES
      "Invalid output mode")
  message(FATAL_ERROR "Video mode accepted a headless surface: ${video_target_error}")
endif()

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --target offscreen --output "${OUTPUT_DIR}/invalid-video.h264" --frames 0
  RESULT_VARIABLE video_count_result
  ERROR_VARIABLE video_count_error)
if(video_count_result EQUAL 0
   OR NOT
      video_count_error
      MATCHES
      "frame count")
  message(FATAL_ERROR "Video mode accepted zero frames: ${video_count_error}")
endif()

set(video_output "${OUTPUT_DIR}/headless-cli-invalid.h264")
foreach(
  arguments IN
  ITEMS "--fps;0"
        "--fps-denominator;0"
        "--orbit-degrees;nan"
        "--orbit-degrees;inf")
  execute_process(
    COMMAND "${EXECUTABLE}" --ply "${ply_path}" --target offscreen --output "${video_output}" --frames 2 ${arguments}
    RESULT_VARIABLE invalid_result
    ERROR_VARIABLE invalid_error)
  if(invalid_result EQUAL 0
     OR NOT
        invalid_error
        MATCHES
        "Invalid output mode")
    message(FATAL_ERROR "Video mode did not reject ${arguments} correctly: ${invalid_error}")
  endif()
endforeach()

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --target offscreen --output "${video_output}"
  RESULT_VARIABLE missing_count_result
  ERROR_VARIABLE missing_count_error)
if(missing_count_result EQUAL 0
   OR NOT
      missing_count_error
      MATCHES
      "frame count")
  message(FATAL_ERROR "Video mode accepted a missing frame count: ${missing_count_error}")
endif()

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --target invalid-target --output "${video_output}" --frames 2
  RESULT_VARIABLE invalid_target_result
  ERROR_VARIABLE invalid_target_error)
if(invalid_target_result EQUAL 0
   OR NOT
      invalid_target_error
      MATCHES
      "invalid-target")
  message(FATAL_ERROR "Video mode accepted an invalid target: ${invalid_target_error}")
endif()

execute_process(
  COMMAND "${EXECUTABLE}" --ply "${ply_path}" --target offscreen --output "${video_output}" --sequence-dir
          "${OUTPUT_DIR}/invalid-sequence" --frames 2
  RESULT_VARIABLE conflict_result
  ERROR_VARIABLE conflict_error)
if(conflict_result EQUAL 0
   OR NOT
      conflict_error
      MATCHES
      "--output")
  message(FATAL_ERROR "Video mode accepted conflicting outputs: ${conflict_error}")
endif()

if(EXISTS "${video_output}")
  message(FATAL_ERROR "Invalid video arguments created an output file")
endif()
