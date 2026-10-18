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
