#include "le_renderer/private/le_renderpass.h"
#include "le_renderer/private/hash_util.h"
#include "vulkan/vulkan.hpp"

// ----------------------------------------------------------------------

static le_renderpass_o *renderpass_create( const char *renderpass_name, const LeRenderPassType &type_ ) {
	auto self  = new le_renderpass_o();
	self->id   = const_char_hash64( renderpass_name );
	self->type = type_;
	strncpy( self->debugName, renderpass_name, sizeof( self->debugName ) );
	return self;
}

// ----------------------------------------------------------------------

static void renderpass_destroy( le_renderpass_o *self ) {

	if ( self->encoder ) {
		static auto &rendererApiI = *Registry::getApi<le_renderer_api>();
		static auto &encoderApi   = rendererApiI.le_command_buffer_encoder_i;
		encoderApi.destroy( self->encoder );
	}

	delete self;
}

// ----------------------------------------------------------------------

static void renderpass_set_setup_fun( le_renderpass_o *self, le_renderer_api::pfn_renderpass_setup_t fun ) {
	self->callbackSetup = fun;
}

// ----------------------------------------------------------------------

static void renderpass_set_execute_callback( le_renderpass_o *self, le_renderer_api::pfn_renderpass_execute_t callback_, void *user_data_ ) {
	self->execute_callback_user_data = user_data_;
	self->callbackExecute            = callback_;
}

// ----------------------------------------------------------------------

static void renderpass_use_resource( le_renderpass_o *self, uint64_t resource_id, uint32_t accessFlags ) {

	assert( self->readResourceCount < 32 );  // todo: fix this, as soon as we have potential for growth here.
	assert( self->writeResourceCount < 32 ); // todo: fix this, as soon as we have potential for growth here.

	if ( accessFlags & le::AccessFlagBits::eRead ) {
		self->readResources[ self->readResourceCount++ ] = resource_id;
	}

	if ( accessFlags & le::AccessFlagBits::eWrite ) {
		self->writeResources[ self->writeResourceCount++ ] = resource_id;
	}
}

// ----------------------------------------------------------------------

static void renderpass_add_image_attachment( le_renderpass_o *self, uint64_t resource_id, le_renderer_api::image_attachment_info_o *info_ ) {

	self->imageAttachments[ self->imageAttachmentCount ] = *info_;
	auto &info                                           = self->imageAttachments[ self->imageAttachmentCount ];
	self->imageAttachmentCount++;

	// By default, flag attachment source as being external, if attachment was previously written in this pass,
	// source will be substituted by id of pass which writes to attachment, otherwise the flag will persist and
	// tell us that this attachment must be externally resolved.
	info.source_id   = const_char_hash64( LE_RENDERPASS_MARKER_EXTERNAL );
	info.resource_id = resource_id;

	if ( info.access_flags == le::AccessFlagBits::eReadWrite ) {
		info.loadOp  = vk::AttachmentLoadOp::eLoad;
		info.storeOp = vk::AttachmentStoreOp::eStore;
	} else if ( info.access_flags & le::AccessFlagBits::eWrite ) {
		// Write-only means we may be seen as the creator of this resource
		info.source_id = self->id;
	} else if ( info.access_flags & le::AccessFlagBits::eRead ) {
		// TODO: we need to make sure to distinguish between image attachments and texture attachments
		info.loadOp  = vk::AttachmentLoadOp::eLoad;
		info.storeOp = vk::AttachmentStoreOp::eDontCare;
	} else {
		info.loadOp  = vk::AttachmentLoadOp::eDontCare;
		info.storeOp = vk::AttachmentStoreOp::eDontCare;
	}

	renderpass_use_resource( self, resource_id, info.access_flags );
	//strncpy( info.debugName, name_, sizeof(info.debugName));
}

// ----------------------------------------------------------------------

static void renderpass_declare_resource( le_renderpass_o *self, uint64_t resource_id, const le_renderer_api::ResourceInfo &info ) {

	assert( self->createResourceCount < 32 ); // todo: set this to max_create_resource_count

	self->createResources[ self->createResourceCount ]     = resource_id;
	self->createResourceInfos[ self->createResourceCount ] = info;
	self->createResourceCount++;

	// additionally, we introduce this resource to the write resource table, so that it will be considered
	// when building the graph based on dependencies.
	renderpass_use_resource( self, resource_id, le::AccessFlagBits::eWrite );
}

// ----------------------------------------------------------------------

static void renderpass_set_is_root( le_renderpass_o *self, bool isRoot ) {
	self->isRoot = isRoot;
}

void register_le_renderpass_api( void *api_ ) {

	auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
	auto &le_renderpass_i   = le_renderer_api_i->le_renderpass_i;

	le_renderpass_i.create               = renderpass_create;
	le_renderpass_i.destroy              = renderpass_destroy;
	le_renderpass_i.add_image_attachment = renderpass_add_image_attachment;
	le_renderpass_i.set_setup_fun        = renderpass_set_setup_fun;
	le_renderpass_i.set_execute_callback = renderpass_set_execute_callback;
	le_renderpass_i.use_resource         = renderpass_use_resource;
	le_renderpass_i.set_is_root          = renderpass_set_is_root;
	le_renderpass_i.declare_resource     = renderpass_declare_resource;
}
