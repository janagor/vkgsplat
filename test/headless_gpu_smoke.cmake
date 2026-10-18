if(NOT DEFINED EXECUTABLE
   OR NOT DEFINED PLY
   OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "EXECUTABLE, PLY, and OUTPUT are required")
endif()

file(REMOVE "${OUTPUT}")
set(render_arguments
    --ply
    "${PLY}"
    --width
    1280
    --height
    720
    --output
    "${OUTPUT}")
if(VALIDATION)
  list(APPEND render_arguments --validation)
endif()
execute_process(COMMAND "${EXECUTABLE}" ${render_arguments} RESULT_VARIABLE render_result)
if(NOT
   render_result
   EQUAL
   0)
  message(FATAL_ERROR "Headless render failed: ${render_result}")
endif()

if(NOT EXISTS "${OUTPUT}")
  message(FATAL_ERROR "Headless render produced no PNG")
endif()
file(
  READ "${OUTPUT}" png_header
  LIMIT 24
  HEX)
string(
  SUBSTRING "${png_header}"
            0
            16
            signature)
if(NOT
   signature
   STREQUAL
   "89504e470d0a1a0a")
  message(FATAL_ERROR "Headless output is not a PNG")
endif()
string(
  SUBSTRING "${png_header}"
            32
            8
            width_hex)
string(
  SUBSTRING "${png_header}"
            40
            8
            height_hex)
math(EXPR png_width "0x${width_hex}")
math(EXPR png_height "0x${height_hex}")
if(NOT
   png_width
   EQUAL
   1280
   OR NOT
      png_height
      EQUAL
      720)
  message(FATAL_ERROR "Headless PNG has unexpected dimensions: ${png_width} x ${png_height}")
endif()
