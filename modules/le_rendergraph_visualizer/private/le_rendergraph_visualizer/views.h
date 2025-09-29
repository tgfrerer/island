#pragma once

#include "le_renderer.hpp"
#include "le_rendergraph_visualizer.h"
#include "le_2d.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace le {
class Font;
}

class RenderPassView {

	std::string   name  = {};
	le::Font*     pFont = nullptr;    // weak, owned by rendergraph_visualizer
	le::Encoder2D encoder_cache;      // owning, cache over all draw calls done for this view

	const bool              is_visible             = false; // this can change
	const bool              is_contributing        = false;
	const bool              is_root                = false;
	const le::QueueFlagBits renderpass_queue_flags = {}; // what "type" of renderpass we have here

	float right_most_x    = 0;  // leftmost point drawn by font renderer
	float required_height = 30; // height, calculated based on content

	std::unordered_map<le_resource_handle, float> ports; // y-position of ports

	std::vector<glm::vec2> in_ports; // we cache ports positions so that we can draw them on top of the clipping region.
	std::vector<glm::vec2> explicit_out_ports;
	std::vector<glm::vec2> implicit_out_ports;

	void draw_into_cache( le::Encoder2D& font_encoder );

  public:
	uint8_t epoch = 0; // last time this was updated (cache control)

	RenderPassView( le::Font* const font, le_renderpass_o const* rp, uint32_t epoch );


	inline float get_leftmost_x() {
		return right_most_x;
	}

	inline float get_required_height() {
		return required_height;
	}

	inline bool get_is_contributing() {
		return is_contributing;
	}

	inline bool get_is_visible() {
		return is_visible;
	}

	// we need to list all positions at which a port is drawn for a particular resource
	glm::vec2 getPortForResource( le_resource_handle const& resource, bool in_out );

	void draw( le::Encoder2D& encoder, LeTransform2D const& transform );
};
