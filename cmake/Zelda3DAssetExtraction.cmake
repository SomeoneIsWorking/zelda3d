# Developer workflows for extracting ROM assets and generated headers.

add_custom_target(
    ExtractAssets
    COMMAND ${CMAKE_COMMAND} -E rm -f oot.o2r oot-mq.o2r soh.o2r
    COMMAND ${Python3_EXECUTABLE} ${ZELDA3D_SHIPWRIGHT_DIR}/OTRExporter/extract_assets.py -z "$<TARGET_FILE:ZAPD>" --non-interactive --xml-root assets/xml --custom-otr-file soh.o2r "--custom-assets-path" ${ZELDA3D_OOT_DIR}/assets/custom --port-ver "${CMAKE_PROJECT_VERSION}"
    COMMAND ${CMAKE_COMMAND} -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME} -DTARGET_DIR="$<TARGET_FILE_DIR:ZAPD>" -DSOURCE_DIR=${ZELDA3D_SHIPWRIGHT_DIR} -DBINARY_DIR=${CMAKE_BINARY_DIR} -P ${ZELDA3D_SHIPWRIGHT_DIR}/copy-existing-otrs.cmake
    WORKING_DIRECTORY ${ZELDA3D_OOT_DIR}
    COMMENT "Running asset extraction..."
    DEPENDS ZAPD
    BYPRODUCTS oot.o2r ${ZELDA3D_SHIPWRIGHT_DIR}/oot.o2r oot-mq.o2r ${ZELDA3D_SHIPWRIGHT_DIR}/oot-mq.o2r ${ZELDA3D_SHIPWRIGHT_DIR}/soh.o2r
)

add_custom_target(
    ExtractAssetHeaders
    COMMAND ${Python3_EXECUTABLE} ${ZELDA3D_SHIPWRIGHT_DIR}/OTRExporter/extract_assets.py -z "$<TARGET_FILE:ZAPD>" --non-interactive --xml-root assets/xml --gen-headers
    WORKING_DIRECTORY ${ZELDA3D_OOT_DIR}
    COMMENT "Generating asset headers..."
    DEPENDS ZAPD
)
