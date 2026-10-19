# Self-contained Danbooru tag dictionary (QtCore only); copy this folder to reuse it.
add_library(danbooru-dictionary STATIC DanbooruDictionary.cpp DanbooruDictionary.h)
target_include_directories(danbooru-dictionary PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(danbooru-dictionary PUBLIC Qt6::Core)
