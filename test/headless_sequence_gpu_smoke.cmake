if(NOT DEFINED EXECUTABLE
   OR NOT DEFINED VERIFY_EXECUTABLE
   OR NOT DEFINED PLY
   OR NOT DEFINED OUTPUT_DIR)
  message(FATAL_ERROR "EXECUTABLE, VERIFY_EXECUTABLE, PLY, and OUTPUT_DIR are required")
endif()

set(sequence_width 128)
set(sequence_height 128)
set(sequence_frames 4)
file(REMOVE_RECURSE "${OUTPUT_DIR}")
set(render_arguments
    --ply
    "${PLY}"
    --target
    offscreen
    --sequence-dir
    "${OUTPUT_DIR}"
    --width
    ${sequence_width}
    --height
    ${sequence_height}
    --frames
    ${sequence_frames}
    --fps
    60
    --orbit-degrees
    360)
if(VALIDATION)
  list(APPEND render_arguments --validation)
endif()
execute_process(COMMAND "${EXECUTABLE}" ${render_arguments} RESULT_VARIABLE render_result)
if(NOT
   render_result
   EQUAL
   0)
  message(FATAL_ERROR "Headless sequence export failed: ${render_result}")
endif()
execute_process(COMMAND "${VERIFY_EXECUTABLE}" "${OUTPUT_DIR}" ${sequence_width} ${sequence_height} ${sequence_frames}
                RESULT_VARIABLE verify_result)
if(NOT
   verify_result
   EQUAL
   0)
  message(FATAL_ERROR "Headless sequence PNG verification failed: ${verify_result}")
endif()
