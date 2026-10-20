if(NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "Synthetic H.264 output is missing: ${INPUT}")
endif()

execute_process(COMMAND "${FFMPEG}" -v error -xerror -f h264 -i "${INPUT}" -f null -
                RESULT_VARIABLE decode_result OUTPUT_VARIABLE decode_output ERROR_VARIABLE decode_error)
if(NOT decode_result EQUAL 0)
  message(FATAL_ERROR "FFmpeg could not decode synthetic H.264: ${decode_error}")
endif()

execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/video_encode_verify_pixels.py" "${FFMPEG}" "${INPUT}"
                RESULT_VARIABLE pixels_result OUTPUT_VARIABLE pixels_output ERROR_VARIABLE pixels_error)
if(NOT pixels_result EQUAL 0)
  message(FATAL_ERROR "Unexpected synthetic H.264 pixels: ${pixels_output} ${pixels_error}")
endif()

execute_process(COMMAND "${FFPROBE}" -v error -f h264 -count_frames -select_streams v:0
                        -show_entries stream=width,height,nb_read_frames -of default=noprint_wrappers=1 "${INPUT}"
                RESULT_VARIABLE probe_result OUTPUT_VARIABLE probe_output ERROR_VARIABLE probe_error)
if(NOT probe_result EQUAL 0 OR NOT probe_output MATCHES "width=1280"
   OR NOT probe_output MATCHES "height=720" OR NOT probe_output MATCHES "nb_read_frames=1")
  message(FATAL_ERROR "Unexpected synthetic H.264 dimensions or frame count: ${probe_output} ${probe_error}")
endif()
