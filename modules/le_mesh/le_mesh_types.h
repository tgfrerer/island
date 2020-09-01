#ifndef GUARD_le_mesh_types_H
#define GUARD_le_mesh_types_H

#include <stdint.h>
#include <vector>
#include "glm/glm.hpp"

struct le_mesh_o {
	std::vector<uint16_t>  indices;  // list of indices
	std::vector<glm::vec3> vertices; // 3d position in model space
	std::vector<glm::vec3> normals;  // normalised normal, per-vertex
	std::vector<glm::vec4> colours;  // rgba colour, per-vertex
	std::vector<glm::vec2> uvs;      // uv coordintates    , per-vertex
	std::vector<glm::vec3> tangents; // normalised tangents, per-vertex
};

#endif
