# Add hand-written UI extension sources here; LVGL Editor preserves this file.
list(
  APPEND
  PROJECT_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/app/bz_app.c
  ${CMAKE_CURRENT_LIST_DIR}/app/bz_assets_data.c
  ${CMAKE_CURRENT_LIST_DIR}/app/bz_boot.c
  ${CMAKE_CURRENT_LIST_DIR}/app/bz_gfx.c
  ${CMAKE_CURRENT_LIST_DIR}/app/bz_icons.c
  ${CMAKE_CURRENT_LIST_DIR}/app/bz_menu.c
  ${CMAKE_CURRENT_LIST_DIR}/app/bz_mining.c
  ${CMAKE_CURRENT_LIST_DIR}/app/bz_model.c)
