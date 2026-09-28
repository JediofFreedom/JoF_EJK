set(effects destruction destruction_explode destruction_explode_enhanced2)
foreach(effect IN LISTS effects)
  set(path "${asset_dir}/effects/forcedestruction/${effect}.efx")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing Destruction effect: ${path}")
  endif()
  file(READ "${path}" source)
  if(source MATCHES "(^|\n)[ \t]*Sound[ \t\r\n]*\\{")
    message(FATAL_ERROR "Impact audio must be selected by cgame, not doubled by EFX: ${effect}")
  endif()
  string(REGEX MATCHALL "\\{" opens "${source}")
  string(REGEX MATCHALL "\\}" closes "${source}")
  list(LENGTH opens open_count)
  list(LENGTH closes close_count)
  if(NOT open_count EQUAL close_count)
    message(FATAL_ERROR "Unbalanced effect: ${effect}")
  endif()
endforeach()
foreach(path IN ITEMS
    gfx/forcedestruction/force_destruction.tga
    sound/forcedestruction/destruction.mp3
    sound/forcedestruction/forcedestruct01.wav
    sound/forcedestruction/forcedestruct02.wav
    credits/force-destruction.txt)
  if(NOT EXISTS "${asset_dir}/${path}")
    message(FATAL_ERROR "Missing supplied asset or credit: ${path}")
  endif()
endforeach()
foreach(name IN ITEMS forcedestruct01 forcedestruct02)
  file(READ "${asset_dir}/sound/forcedestruction/${name}.wav" format OFFSET 20 LIMIT 16 HEX)
  if(NOT format STREQUAL "010001002256000044ac000002001000")
    message(FATAL_ERROR "JKA sound effects require mono PCM16/22050 Hz: ${name}")
  endif()
endforeach()
foreach(path IN ITEMS
    effects/destruction/projectile.efx effects/destruction/impact.efx
    effects/jof/destruction/projectile.efx effects/jof/destruction/impact.efx
    shaders/force_destruction.shader
    shaders/firstmovingball.shader shaders/newmovingball.shader
    models/players/movingball/model.glm ext_data/npcs/zzforcedestruction.npc)
  if(EXISTS "${asset_dir}/${path}")
    message(FATAL_ERROR "Retired or excluded Destruction asset: ${path}")
  endif()
endforeach()
message(STATUS "Supplied Destruction effects, icon, sounds and credit are present; retired/excluded assets are absent.")
