#ifndef ZELDA3D_MODEL_GEOMETRY_H
#define ZELDA3D_MODEL_GEOMETRY_H

struct LoadedModel;

// Bind-pose local-space height shared by loader diagnostics and public geometry probes.
float Zelda3D_ModelGeometryHeight(const LoadedModel& model);

#endif // ZELDA3D_MODEL_GEOMETRY_H
