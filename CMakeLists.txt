project(obs-move-to-sound VERSION 0.3.0)

set(obs-move-to-sound_SOURCES
  src/main.c)

add_library(obs-move-to-sound MODULE ${obs-move-to-sound_SOURCES})

target_link_libraries(obs-move-to-sound libobs)
set_target_properties(obs-move-to-sound PROPERTIES FOLDER "plugins")

install_obs_plugin_with_data(obs-move-to-sound data)
