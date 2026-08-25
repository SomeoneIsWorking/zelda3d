// Public room-model lookup and render-mesh floor queries.
#ifndef ZELDA3D_RENDER_ROOM_GEOMETRY_QUERIES_H
#define ZELDA3D_RENDER_ROOM_GEOMETRY_QUERIES_H

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_RoomModelId(const char* sceneName, int roomNum);
int Zelda3D_RoomMeshFloorAt(int modelId, float x, float z, float* outY);
int Zelda3D_RoomOoT3DFloorAt(int modelId, float x, float z, float referenceY, float* outY);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_ROOM_GEOMETRY_QUERIES_H
