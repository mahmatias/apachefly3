# Data library
add_library(c3_data STATIC
  data/iso9660.cpp
  data/resource2.cpp
  data/lzp1.cpp
  data/pcx.cpp
  data/model3do.cpp
  data/midi.cpp
  data/wav.cpp
)
target_include_directories(c3_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Platform library (placeholder)
add_library(c3_platform STATIC
  platform/window.cpp
  platform/d3d11_device.cpp
)
target_include_directories(c3_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Audio library (placeholder)
add_library(c3_audio STATIC
  audio/audio.cpp
  audio/mixer.cpp
)
target_include_directories(c3_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Present/render library (placeholder)
add_library(c3_present STATIC
  present/entity_renderer.cpp
  present/entity_model.cpp
)
target_include_directories(c3_present PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# App library
add_library(c3_app STATIC
  app/application.cpp
)
target_include_directories(c3_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(c3_app PUBLIC c3_data)

# Extract tool
add_executable(c3_extract
  tools/extract.cpp
)
target_link_libraries(c3_extract PRIVATE c3_data)

# Decompress tool
add_executable(c3_decompress
  tools/decompress.cpp
)
target_link_libraries(c3_decompress PRIVATE c3_data)

# LZP1 test tool
add_executable(c3_test_lzp1
  tools/test_lzp1_lib.cpp
)
target_link_libraries(c3_test_lzp1 PRIVATE c3_data)

# Bitstream analysis tool
add_executable(c3_analyze_bitstream
  tools/analyze_bitstream.cpp
)