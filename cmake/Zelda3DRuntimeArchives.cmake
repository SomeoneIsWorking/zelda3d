# OoT's custom runtime archive is regenerated with the main build's GAME_OOT ZAPD. MM's `mm.o2r`
# and `2ship.o2r` are one atomic output of the independent GAME_MM extraction owner in
# `launcher_bootstrap/mm_assets.py`; generating `2ship.o2r` here with the wrong exporter mode would
# create two authorities for the same runtime input.

add_custom_target(
    GenerateSohOtr
    COMMAND ${CMAKE_COMMAND} -E rm -f soh.o2r

    # SDL3 GPU generates its shaders at runtime; this archive owns only the port's real runtime
    # assets (fonts, objects, scenes, textures, languages, and presets).
    COMMAND ${Python3_EXECUTABLE} ${ZELDA3D_SHIPWRIGHT_DIR}/OTRExporter/extract_assets.py -z "$<TARGET_FILE:ZAPD>" --norom --custom-otr-file soh.o2r "--custom-assets-path" ${ZELDA3D_OOT_DIR}/assets/custom --port-ver "${CMAKE_PROJECT_VERSION}"
    COMMAND ${CMAKE_COMMAND} -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME} -DTARGET_DIR="$<TARGET_FILE_DIR:ZAPD>" -DSOURCE_DIR=${ZELDA3D_SHIPWRIGHT_DIR} -DBINARY_DIR=${CMAKE_BINARY_DIR} -DONLYSOHOTR=On -P ${ZELDA3D_SHIPWRIGHT_DIR}/copy-existing-otrs.cmake
    WORKING_DIRECTORY ${ZELDA3D_OOT_DIR}
    COMMENT "Generating current soh.o2r..."
    DEPENDS ZAPD
)

add_custom_target(zelda3d_runtime_archives)
add_dependencies(zelda3d_runtime_archives GenerateSohOtr)
