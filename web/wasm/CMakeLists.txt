# Standalone wasm with no JS glue: an AudioWorklet has no fetch or importScripts,
# so the page passes these bytes in and the worklet instantiates them directly.
add_executable(jam_wasm bindings.cpp)
target_link_libraries(jam_wasm PRIVATE jam_engine)
target_include_directories(jam_wasm PRIVATE
  ${PROJECT_SOURCE_DIR}/engine/src
  ${PROJECT_SOURCE_DIR}/engine/tools)
target_compile_options(jam_wasm PRIVATE -fno-exceptions -fno-rtti)
target_link_options(jam_wasm PRIVATE
  --no-entry
  -sSTANDALONE_WASM
  -sFILESYSTEM=0
  -sINITIAL_MEMORY=32MB
  # Growth would detach the views the worklet holds, mid-render.
  -sALLOW_MEMORY_GROWTH=0
  -sSTACK_SIZE=256KB)
set_target_properties(jam_wasm PROPERTIES
  OUTPUT_NAME jam
  SUFFIX .wasm
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/web/public)
