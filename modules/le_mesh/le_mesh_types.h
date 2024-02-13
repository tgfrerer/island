#ifndef GUARD_le_mesh_types_H
#define GUARD_le_mesh_types_H

#include <stdint.h>
#include <vector>
#include "glm/glm.hpp"

struct le_mesh_o {

	typedef glm::vec3 vertex_t;
	typedef glm::vec3 normals_t;
	typedef glm::vec4 colours_t;
	typedef glm::vec2 uvs_t;
	typedef glm::vec3 tangents_t;

	std::vector<uint16_t>   indices;  // list of indices
	std::vector<vertex_t>   vertices; // 3d position in model space
	std::vector<normals_t>  normals;  // normalised normal, per-vertex
	std::vector<colours_t>  colours;  // rgba colour, per-vertex
	std::vector<uvs_t>      uvs;      // uv coordintates    , per-vertex
	std::vector<tangents_t> tangents; // normalised tangents, per-vertex
};

#endif
