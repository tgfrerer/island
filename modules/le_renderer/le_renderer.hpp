#ifndef GUARD_LE_RENDERER_HPP
#define GUARD_LE_RENDERER_HPP

#ifdef __cplusplus
#	include "le_renderer.h"

namespace le {

#	define BUILDER_IMPLEMENT( builder, method_name, param_type, param, default_value ) \
		constexpr builder& method_name( param_type param default_value ) {              \
			self.param = param;                                                         \
			return *this;                                                               \
		}

#	define P_BUILDER_IMPLEMENT( builder, method_name, param_type, param, default_value ) \
		constexpr builder& method_name( param_type param default_value ) {                \
			self->param = param;                                                          \
			return *this;                                                                 \
		}

// ----------------------------------------------------------------------

class SamplerInfoBuilder {
	le_sampler_info_t info{};

	le_sampler_info_t& self = info;

  public:
	SamplerInfoBuilder()  = default;
	~SamplerInfoBuilder() = default;

	SamplerInfoBuilder( le_sampler_info_t const& info_ )
	    : info( info_ ) {
	}

	BUILDER_IMPLEMENT( SamplerInfoBuilder, setMagFilter, le::Filter, magFilter, = le::Filter::eLinear )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setMinFilter, le::Filter, minFilter, = le::Filter::eLinear )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setMipmapMode, le::SamplerMipmapMode, mipmapMode, = le::SamplerMipmapMode::eLinear )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setAddressModeU, le::SamplerAddressMode, addressModeU, = le::SamplerAddressMode::eClampToBorder )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setAddressModeV, le::SamplerAddressMode, addressModeV, = le::SamplerAddressMode::eClampToBorder )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setAddressModeW, le::SamplerAddressMode, addressModeW, = le::SamplerAddressMode::eRepeat )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setMipLodBias, float, mipLodBias, = 0.f )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setAnisotropyEnable, bool, anisotropyEnable, = false )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setMaxAnisotropy, float, maxAnisotropy, = 0.f )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setCompareEnable, bool, compareEnable, = false )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setCompareOp, le::CompareOp, compareOp, = le::CompareOp::eLess )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setMinLod, float, minLod, = 0.f )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setMaxLod, float, maxLod, = 1.f )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setBorderColor, le::BorderColor, borderColor, = le::BorderColor::eFloatTransparentBlack )
	BUILDER_IMPLEMENT( SamplerInfoBuilder, setUnnormalizedCoordinates, bool, unnormalizedCoordinates, = false )

	le_sampler_info_t const& build() {
		return info;
	}
};

class ImageSamplerInfoBuilder {
	le_image_sampler_info_t info{};

	class SamplerInfoBuilder {
		ImageSamplerInfoBuilder& parent;
		le_sampler_info_t&       self = parent.info.sampler;

	  public:
		SamplerInfoBuilder( ImageSamplerInfoBuilder& parent_ )
		    : parent( parent_ ) {
		}

		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMagFilter, le::Filter, magFilter, = le::Filter::eLinear )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMinFilter, le::Filter, minFilter, = le::Filter::eLinear )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMipmapMode, le::SamplerMipmapMode, mipmapMode, = le::SamplerMipmapMode::eLinear )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setAddressModeU, le::SamplerAddressMode, addressModeU, = le::SamplerAddressMode::eClampToBorder )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setAddressModeV, le::SamplerAddressMode, addressModeV, = le::SamplerAddressMode::eClampToBorder )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setAddressModeW, le::SamplerAddressMode, addressModeW, = le::SamplerAddressMode::eRepeat )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMipLodBias, float, mipLodBias, = 0.f )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setAnisotropyEnable, bool, anisotropyEnable, = false )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMaxAnisotropy, float, maxAnisotropy, = 0.f )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setCompareEnable, bool, compareEnable, = false )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setCompareOp, le::CompareOp, compareOp, = le::CompareOp::eLess )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMinLod, float, minLod, = 0.f )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMaxLod, float, maxLod, = 1.f )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setBorderColor, le::BorderColor, borderColor, = le::BorderColor::eFloatTransparentBlack )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setUnnormalizedCoordinates, bool, unnormalizedCoordinates, = false )

		ImageSamplerInfoBuilder& end() {
			return parent;
		}
	};

	class ImageViewInfoBuilder {
		ImageSamplerInfoBuilder&                       parent;
		le_image_view_info_t&                          self = parent.info.imageView;

	  public:
		ImageViewInfoBuilder( ImageSamplerInfoBuilder& parent_ )
		    : parent( parent_ ) {
		}

		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setImage, le_image_resource_handle, imageId, = {} )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setImageViewType, le::ImageViewType, image_view_type, = le::ImageViewType::e2D )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setFormat, le::Format, format, = le::Format::eUndefined )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setBaseArrayLayer, uint32_t, base_array_layer, = 0 )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setLayerCount, uint32_t, layer_count, = 1 )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setSwizzleR, le::ComponentSwizzle, r_swizzle, = {} )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setSwizzleG, le::ComponentSwizzle, g_swizzle, = {} )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setSwizzleB, le::ComponentSwizzle, b_swizzle, = {} )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setSwizzleA, le::ComponentSwizzle, a_swizzle, = {} )

		ImageSamplerInfoBuilder& end() {
			return parent;
		}
	};

	SamplerInfoBuilder   mSamplerInfoBuilder{ *this };
	ImageViewInfoBuilder mImageViewInfoBuilder{ *this };

  public:
	ImageSamplerInfoBuilder()  = default;
	~ImageSamplerInfoBuilder() = default;

	ImageSamplerInfoBuilder( le_image_sampler_info_t const& info_ )
	    : info( info_ ) {
	}

	ImageSamplerInfoBuilder( le_image_resource_handle const& image_resource ) {
		info.imageView.imageId = image_resource;
	}

	ImageViewInfoBuilder& withImageViewInfo() {
		return mImageViewInfoBuilder;
	}

	SamplerInfoBuilder& withSamplerInfo() {
		return mSamplerInfoBuilder;
	}

	le_image_sampler_info_t const& build() {
		return info;
	}
};

// TODO: refactor this -- so that this is not declared as a sub-object of ImageSamplerInfoBuilder anymore

class ImageViewInfoBuilder {
	le_image_view_info_t info{};

	le_image_view_info_t& self = info;

  public:
	ImageViewInfoBuilder()  = default;
	~ImageViewInfoBuilder() = default;

	ImageViewInfoBuilder( le_image_view_info_t const& info_ )
	    : info( info_ ) {
	}

	BUILDER_IMPLEMENT( ImageViewInfoBuilder, setImage, le_image_resource_handle, imageId, = {} )
	BUILDER_IMPLEMENT( ImageViewInfoBuilder, setImageViewType, le::ImageViewType, image_view_type, = le::ImageViewType::e2D )
	BUILDER_IMPLEMENT( ImageViewInfoBuilder, setFormat, le::Format, format, = le::Format::eUndefined )
	BUILDER_IMPLEMENT( ImageViewInfoBuilder, setBaseArrayLayer, uint32_t, base_array_layer, = 0 )
	BUILDER_IMPLEMENT( ImageViewInfoBuilder, setLayerCount, uint32_t, layer_count, = 1 )
	BUILDER_IMPLEMENT( ImageViewInfoBuilder, setSwizzleR, le::ComponentSwizzle, r_swizzle, = {} )
	BUILDER_IMPLEMENT( ImageViewInfoBuilder, setSwizzleG, le::ComponentSwizzle, g_swizzle, = {} )
	BUILDER_IMPLEMENT( ImageViewInfoBuilder, setSwizzleB, le::ComponentSwizzle, b_swizzle, = {} )
	BUILDER_IMPLEMENT( ImageViewInfoBuilder, setSwizzleA, le::ComponentSwizzle, a_swizzle, = {} )

	le_image_view_info_t const& build() {
		return info;
	}
};

class ImageAttachmentInfoBuilder {
	le_image_attachment_info_t self{};

  public:
	BUILDER_IMPLEMENT( ImageAttachmentInfoBuilder, setLoadOp, le::AttachmentLoadOp, loadOp, = le::AttachmentLoadOp::eClear )
	BUILDER_IMPLEMENT( ImageAttachmentInfoBuilder, setStoreOp, le::AttachmentStoreOp, storeOp, = le::AttachmentStoreOp::eStore )
	BUILDER_IMPLEMENT( ImageAttachmentInfoBuilder, setColorClearValue, le::ClearValue, clearValue, = le_image_attachment_info_t::DefaultClearValueColor )
	BUILDER_IMPLEMENT( ImageAttachmentInfoBuilder, setDepthStencilClearValue, le::ClearValue, clearValue, = le_image_attachment_info_t::DefaultClearValueDepthStencil )

	operator le_image_attachment_info_t const&() {
		return self;
	}

	le_image_attachment_info_t const& build() {
		return self;
	}
};

class WriteToImageSettingsBuilder {
	le_write_to_image_settings_t self{};

  public:
	WriteToImageSettingsBuilder() = default;

	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setImageW, uint32_t, image_w, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setImageH, uint32_t, image_h, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setImageD, uint32_t, image_d, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setOffsetX, int32_t, offset_x, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setOffsetY, int32_t, offset_y, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setOffsetZ, int32_t, offset_z, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setArrayLayer, uint32_t, dst_array_layer, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setDstMiplevel, uint32_t, dst_miplevel, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setNumMiplevels, uint32_t, num_miplevels, = 1 )

	constexpr le_write_to_image_settings_t const& build() const {
		return self;
	}
};
#	undef BUILDER_IMPLEMENT
#	undef P_BUILDER_IMPLEMENT

// ---------

class Renderer : NoMove, NoCopy {

	le_renderer_o* self;

  public:
	Renderer()
	    : self( le_renderer::renderer_i.create() ) {
	}

	~Renderer() {
		le_renderer::renderer_i.destroy( self );
	}

	void setup( le_renderer_settings_t const& settings ) {
		le_renderer::renderer_i.setup( self, &settings );
	}

	void setup( le_window_o* window = nullptr ) {
		if ( window ) {
			le_renderer::renderer_i.setup_with_window( self, window );
		} else {
			le_renderer::renderer_i.setup( self, {} );
		}
	}

	/// Call this method exactly once per Frame - this is where rendergraph execution callbacks are triggered.
	void update( le_rendergraph_o* rendergraph ) {
		le_renderer::renderer_i.update( self, rendergraph );
	}

	le_renderer_settings_t const& getSettings() const noexcept {
		return *le_renderer::renderer_i.get_settings( self );
	}

	size_t getCurrentFrameNumber() const noexcept {
		return le_renderer::renderer_i.get_current_frame_number( self );
	}

	/// Note: you can only add Swapchains to the renderer after the renderer has been setup()
	le_swapchain_handle addSwapchain( le_swapchain_settings_t const* swapchain_settings ) noexcept {
		return le_renderer::renderer_i.add_swapchain( self, swapchain_settings );
	}

	bool resizeSwapchain( uint32_t width, uint32_t height, le_swapchain_handle swapchain = nullptr ) noexcept {
		return le_renderer::renderer_i.resize_swapchain( self, swapchain, width, height );
	}

	bool removeSwapchain( le_swapchain_handle swapchain ) noexcept {
		return le_renderer::renderer_i.remove_swapchain( self, swapchain );
	}

	bool getSwapchains( size_t* num_swapchains, le_swapchain_handle* p_swapchain_handles ) {
		return le_renderer::renderer_i.get_swapchains( self, num_swapchains, p_swapchain_handles );
	}

	/// Returns the image resource for the given swapchain, if this swapchain is available in the backend.
	le_image_resource_handle getSwapchainResource( le_swapchain_handle swapchain ) const {
		return le_renderer::renderer_i.get_swapchain_resource( self, swapchain );
	}

	/// Returns the default swapchain image resource, if there is a swapchain available in the backend
	le_image_resource_handle getSwapchainResource() const {
		return le_renderer::renderer_i.get_swapchain_resource_default( self );
	}

	bool getSwapchainExtent( uint32_t* pWidth, uint32_t* pHeight, le_swapchain_handle swapchain = nullptr ) const {
		return le_renderer::renderer_i.get_swapchain_extent( self, swapchain, pWidth, pHeight );
	}

	le_image_resource_handle createImageResourceHandle() {
		// return le_renderer::renderer_i.create_img_resource_handle(self, name, num_samples,)
	}

	le_bindless_texture_handle allocateBindlessTexture( le_image_sampler_info_t const& image_sampler ) {
		return le_renderer::renderer_i.allocate_bindless_texture( self, &image_sampler );
	}

	le_bindless_sampler_handle allocateBindlessSampler( le_sampler_info_t const& sampler ) {
		return le_renderer::renderer_i.allocate_bindless_sampler( self, &sampler );
	}

	le_bindless_storage_image_handle allocateBindlessStorageImage( le_image_view_info_t const& storage_image ) {
		return le_renderer::renderer_i.allocate_bindless_storage_image( self, &storage_image );
	}

	void resolveResourcesForBindlessResources( le_bindless_resource_handle const* p_bindless_resources, uint32_t num_bindless_resources, le_resource_handle* p_resource_handles ) {
		le_renderer::renderer_i.get_resources_for_bindless_resources( self, p_bindless_resources, num_bindless_resources, p_resource_handles );
	}

	const le::Extent2D getSwapchainExtent( le_swapchain_handle swapchain = nullptr ) const {
		le::Extent2D result{};
		le_renderer::renderer_i.get_swapchain_extent( self, swapchain, &result.width, &result.height );
		return result;
	}

	le_pipeline_manager_o* getPipelineManager() const {
		return le_renderer::renderer_i.get_pipeline_manager( self );
	}

	operator auto() {
		return self;
	}
};

class RenderPass {

	le_renderpass_o* self;
	using resource_usage_flags = le_renderer_api::renderpass_interface_t::resource_usage_flags;

  public:
	// We must allow for this constructor to be called with no name, so that it can be used with initializer lists
	// and perfect forwarding (this is necessary if you want to throw a RenderPass into a std::unordered_map, for
	// example.
	RenderPass( const char* name_ = "", const le::QueueFlagBits& type_ = le::QueueFlagBits::eGraphics )
	    : self( le_renderer::renderpass_i.create( name_, type_ ) ) {
	}

	RenderPass( const char* name_, const le::QueueFlagBits& type_, le_renderer_api::pfn_renderpass_setup_t fun_setup, le_renderer_api::pfn_renderpass_execute_t fun_exec, void* user_data )
	    : self( le_renderer::renderpass_i.create( name_, type_ ) ) {
		le_renderer::renderpass_i.set_setup_callback( self, user_data, fun_setup );
		le_renderer::renderpass_i.set_execute_callback( self, user_data, fun_exec );
	}

	operator auto() {
		return self;
	}

	operator const auto() const {
		return self;
	}

	static RenderPass clone( le_renderpass_o const* original ) {
		RenderPass rp( le_renderer::renderpass_i.clone( original ) );
		// we must decrease ref count as creating a Renderpass
		// facade on the clone has increased its ref count to 2
		le_renderer::renderpass_i.ref_dec( rp );
		return rp;
	}

	RenderPass clone() {
		return RenderPass::clone( self );
	}

	// Create facade from pointer
	explicit RenderPass( le_renderpass_o* self_ )
	    : self( self_ ) {
		le_renderer::renderpass_i.ref_inc( self );
	}

	// Destructor
	~RenderPass() {
		// We check for validity of self, as move assignment/constructor
		// set the moved-from to null after completion.
		if ( self ) {
			le_renderer::renderpass_i.ref_dec( self );
		}
	}

	// Copy constructor
	RenderPass( RenderPass const& rhs )
	    : self( rhs.self ) {
		le_renderer::renderpass_i.ref_inc( self );
	}

	// Copy assignment
	RenderPass& operator=( RenderPass const& rhs ) {
		self = rhs.self;
		le_renderer::renderpass_i.ref_inc( self );
		return *this;
	}

	// Move constructor
	RenderPass( RenderPass&& rhs ) noexcept
	    : self( rhs.self ) {
		rhs.self = nullptr;
	}

	// Move assignment
	RenderPass& operator=( RenderPass&& rhs ) {
		self     = rhs.self;
		rhs.self = nullptr;
		return *this;
	}


	RenderPass& setSetupCallback( void* user_data, le_renderer_api::pfn_renderpass_setup_t fun ) {
		le_renderer::renderpass_i.set_setup_callback( self, user_data, fun );
		return *this;
	}

	RenderPass& setExecuteCallback( void* user_data, le_renderer_api::pfn_renderpass_execute_t fun ) {
		le_renderer::renderpass_i.set_execute_callback( self, user_data, fun );
		return *this;
	}

	// ----

	/// \brief Adds a resource as an image attachment to the renderpass.
	/// \details resource is used for ColorAttachment and Write access, unless otherwise specified.
	///          Use an le_image_attachment_info_t struct to specialise parameters, such as LOAD_OP, CLEAR_OP, and Clear/Load Color.
	RenderPass& addColorAttachment( const le_image_resource_handle&   resource_id,
	                                const le_image_attachment_info_t& imageAttachmentInfo = le_image_attachment_info_t() ) {
		le_renderer::renderpass_i.add_color_attachment( self, resource_id, &imageAttachmentInfo );
		return *this;
	}

	RenderPass& addDepthStencilAttachment( const le_image_resource_handle&   resource_id,
	                                       const le_image_attachment_info_t& depthAttachmentInfo = LeDepthAttachmentInfo() ) {
		le_renderer::renderpass_i.add_depth_stencil_attachment( self, resource_id, &depthAttachmentInfo );
		return *this;
	}

	// TODO: not super happy with this -- it feels a bit cumbersome,
	//
	// The intent is to have a way to signal to the renderer that we are using an image resource in a bindless manner
	// and that the renderer does not need to create a transient view for this image for this frame.
	//
	// It would be better if we could directly declare the resource via the bindless handle and would
	// not have go go through the parent resource.
	//
	// Furthermore, it is not much use to just declare that the renderer must create a transient view without specifying
	// the view required for this image (you can't assume images are all 2d, for example) -- this is much better solved
	// with bindless resources where there is a 1:1 correspondence of a bindless handle to an image+view.
	//
	RenderPass& useImageResourceNoTransient( le_image_resource_handle resource_id, le::AccessFlagBits2 const& first_read_access = le::AccessFlagBits2::eShaderSampledRead, le::AccessFlagBits2 const& last_write_access = le::AccessFlagBits2::eNone ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, first_read_access | last_write_access, resource_usage_flags::eNone );
		return *this;
	}

	RenderPass& useImageResource( le_image_resource_handle resource_id, le::AccessFlagBits2 const& first_read_access = le::AccessFlagBits2::eShaderSampledRead, le::AccessFlagBits2 const& last_write_access = le::AccessFlagBits2::eNone ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, first_read_access | last_write_access, resource_usage_flags::eRequiresTransient );
		return *this;
	}

	RenderPass& useBufferResource( le_buffer_resource_handle resource_id, le::AccessFlagBits2 const& first_read_access = le::AccessFlagBits2::eVertexAttributeRead, le::AccessFlagBits2 const& last_write_access = le::AccessFlagBits2::eNone ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, first_read_access | last_write_access, resource_usage_flags::eNone );
		return *this;
	}

	RenderPass& useRtxBlasResource( le_resource_handle resource_id, le::AccessFlags2 const& access_flags = le::AccessFlags2( le::AccessFlagBits2::eAccelerationStructureReadBitKhr ) ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, access_flags, resource_usage_flags::eNone );
		return *this;
	}

	RenderPass& useRtxTlasResource( le_resource_handle resource_id, le::AccessFlags2 const& access_flags = le::AccessFlags2( le::AccessFlagBits2::eAccelerationStructureReadBitKhr ) ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, access_flags, resource_usage_flags::eNone );
		return *this;
	}

	RenderPass& setIsRoot( bool isRoot = true ) {
		le_renderer::renderpass_i.set_is_root( self, isRoot );
		return *this;
	}

	// the important thing here is that we mark the linked image resource as being used with this renderpass-
	// we don't really care about the resource being used bindless; and the lookup will be faster if we can
	// just pass an array of image resources

	RenderPass& sampleTexture( le_texture_handle textureName, const le_image_sampler_info_t& imageSamplerInfo ) {
		le_renderer::renderpass_i.sample_texture( self, textureName, &imageSamplerInfo );
		return *this;
	}

	RenderPass& sampleTexture( le_texture_handle textureName, le_image_resource_handle img_handle ) {
		return sampleTexture( textureName, le::ImageSamplerInfoBuilder( img_handle ).build() );
	}

	RenderPass& setWidth( uint32_t width ) {
		le_renderer::renderpass_i.set_width( self, width );
		return *this;
	}

	RenderPass& setHeight( uint32_t height ) {
		le_renderer::renderpass_i.set_height( self, height );
		return *this;
	}

	RenderPass& setSampleCount( le::SampleCountFlagBits const& sampleCount ) {
		le_renderer::renderpass_i.set_sample_count( self, sampleCount );
		return *this;
	}
};

// ----------------------------------------------------------------------

class RenderGraph : NoCopy, NoMove {

	le_rendergraph_o* self;

	const bool is_reference = false;

  public:
	RenderGraph()
	    : self( le_renderer::rendergraph_i.create() ) {
	}

	RenderGraph( le_rendergraph_o* self_ )
	    : self( self_ )
	    , is_reference( true ) {
	}

	~RenderGraph() {
		if ( !is_reference ) {
			le_renderer::rendergraph_i.destroy( self );
		}
	}

	operator auto() {
		return self;
	}

	RenderGraph& addRenderPass( le_renderpass_o* renderpass ) {
		le_renderer::rendergraph_i.add_renderpass( self, renderpass );
		return *this;
	}

	RenderGraph& declareResource( le_resource_handle const& resource_id, le_resource_info_t const& info ) {
		le_renderer::rendergraph_i.declare_resource( self, resource_id, info );
		return *this;
	}
};

// ----------------------------------------------------------------------

class ImageInfoBuilder : NoCopy, NoMove {
	le_resource_info_t             res = le_renderer::helpers_i.get_default_resource_info_for_image();
	le_resource_info_t::ImageInfo& img = res.image;

  public:
	// FIXME: This method does not check that the resource_info type is actually image!
	ImageInfoBuilder( const le_resource_info_t& info )
	    : res( info ) {
	}
	ImageInfoBuilder()  = default;
	~ImageInfoBuilder() = default;

	ImageInfoBuilder& setFormat( Format format ) {
		img.format = format;
		return *this;
	}

	ImageInfoBuilder& setCreateFlags( le::ImageCreateFlags flags = ImageCreateFlagBits() ) {
		img.flags = flags;
		return *this;
	}

	ImageInfoBuilder& setArrayLayers( uint32_t arrayLayers = 1 ) {
		img.arrayLayers = arrayLayers;
		return *this;
	}

	ImageInfoBuilder& setExtent( uint32_t width, uint32_t height, uint32_t depth = 1 ) {
		img.extent.width  = width;
		img.extent.height = height;
		img.extent.depth  = depth;
		return *this;
	}

	ImageInfoBuilder& setUsageFlags( le::ImageUsageFlags const& usageFlagBits ) {
		img.usage = usageFlagBits;
		return *this;
	}

	ImageInfoBuilder& addUsageFlags( le::ImageUsageFlags const& usageFlags ) {
		img.usage = img.usage | usageFlags;
		return *this;
	}

	ImageInfoBuilder& setMipLevels( uint32_t mipLevels = 1 ) {
		img.mipLevels = mipLevels;
		return *this;
	}

	ImageInfoBuilder& setImageType( le::ImageType const& imageType = le::ImageType::e2D ) {
		img.imageType = imageType;
		return *this;
	}

	ImageInfoBuilder& setImageTiling( le::ImageTiling const& imageTiling = le::ImageTiling::eOptimal ) {
		img.tiling = imageTiling;
		return *this;
	}

	const le_resource_info_t& build() {
		return res;
	}
};

using ImageResourceInfoBuilder = ImageInfoBuilder;

// ----------------------------------------------------------------------

class BufferInfoBuilder : NoCopy, NoMove {
	le_resource_info_t              res = le_renderer::helpers_i.get_default_resource_info_for_buffer();
	le_resource_info_t::BufferInfo& buf = res.buffer;

  public:
	// FIXME: This method does not check that the resource_info type is actually buffer!
	BufferInfoBuilder( const le_resource_info_t& info )
	    : res( info ) {
	}

	BufferInfoBuilder()  = default;
	~BufferInfoBuilder() = default;

	BufferInfoBuilder& setSize( uint32_t size ) {
		buf.size = size;
		return *this;
	}

	BufferInfoBuilder& setUsageFlags( le::BufferUsageFlags const& usageFlagBits ) {
		buf.usage = usageFlagBits;
		return *this;
	}

	BufferInfoBuilder& addUsageFlags( le::BufferUsageFlags const& usageFlags ) {
		buf.usage |= usageFlags;
		return *this;
	}

	const le_resource_info_t& build() {
		return res;
	}
};
using BufferResourceInfoBuilder = BufferInfoBuilder;

// ----------------------------------------------------------------------

class GraphicsEncoder {
	le_command_buffer_encoder_o* self = nullptr;

  public:
	GraphicsEncoder()  = delete;
	~GraphicsEncoder() = default;

	GraphicsEncoder( le_command_buffer_encoder_o* self_ )
	    : self( self_ ) {
	}

	le_pipeline_manager_o* getPipelineManager() {
		return le_renderer::encoder_graphics_i.get_pipeline_manager( self );
	}

	GraphicsEncoder& setPushConstantData( void const* data, uint64_t const& numBytes ) {
		le_renderer::encoder_graphics_i.set_push_constant_data( self, data, numBytes );
		return *this;
	}

	GraphicsEncoder& bindArgumentBuffer( uint64_t const& argumentName, le_buffer_resource_handle const& bufferId, uint64_t const& offset = 0, uint64_t const& range = ( ~0ULL ) ) {
		le_renderer::encoder_graphics_i.bind_argument_buffer( self, bufferId, argumentName, offset, range );
		return *this;
	}

	GraphicsEncoder& bufferMemoryBarrier(
	    le::PipelineStageFlags2 const&   srcStageMask,
	    le::PipelineStageFlags2 const&   dstStageMask,
	    le::AccessFlags2 const&          srcAccessMask,
	    le::AccessFlags2 const&          dstAccessMask,
	    le_buffer_resource_handle const& buffer,
	    uint64_t const&                  offset = 0,
	    uint64_t const&                  range  = ~( 0ull ) ) {
		le_renderer::encoder_graphics_i.buffer_memory_barrier( self, srcStageMask, dstStageMask, srcAccessMask, dstAccessMask, buffer, offset, range );
		return *this;
	}

	GraphicsEncoder& bindArgumentBufferExplicit( uint32_t const& set_idx, uint32_t const& binding_idx, le_buffer_resource_handle const& bufferId, uint64_t const& offset = 0, uint64_t const& range = ( ~0ULL ) ) {
		le_renderer::encoder_compute_i.bind_argument_buffer_explicit( self, bufferId, set_idx, binding_idx, offset, range );
		return *this;
	}

	GraphicsEncoder& setArgumentDataExplicit( uint32_t const& set_idx, uint32_t const& binding_idx, void const* data, size_t const& numBytes ) {
		le_renderer::encoder_compute_i.set_argument_data_explicit( self, set_idx, binding_idx, data, numBytes );
		return *this;
	}

	GraphicsEncoder& setArgumentTextureExplicit( uint32_t const& set_idx, uint32_t const& binding_idx, le_texture_handle const& textureId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_compute_i.set_argument_texture_explicit( self, textureId, set_idx, binding_idx, arrayIndex );
		return *this;
	}

	GraphicsEncoder& setArgumentImageExplicit( uint32_t const& set_idx, uint32_t const& binding_idx, le_image_resource_handle const& imageId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_compute_i.set_argument_image_explicit( self, imageId, set_idx, binding_idx, arrayIndex );
		return *this;
	}

	GraphicsEncoder& setArgumentData( uint64_t const& argumentNameId, void const* data, size_t const& numBytes ) {
		le_renderer::encoder_graphics_i.set_argument_data( self, argumentNameId, data, numBytes );
		return *this;
	}

	GraphicsEncoder& setArgumentTexture( uint64_t const& argumentName, le_texture_handle const& textureId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_graphics_i.set_argument_texture( self, textureId, argumentName, arrayIndex );
		return *this;
	}

	GraphicsEncoder& setArgumentImage( uint64_t const& argumentName, le_image_resource_handle const& imageId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_graphics_i.set_argument_image( self, imageId, argumentName, arrayIndex );
		return *this;
	}

	GraphicsEncoder& draw( const uint32_t& vertexCount, const uint32_t& instanceCount = 1, const uint32_t& firstVertex = 0, const uint32_t& firstInstance = 0 ) {
		le_renderer::encoder_graphics_i.draw( self, vertexCount, instanceCount, firstVertex, firstInstance );
		return *this;
	}

	GraphicsEncoder& drawIndexed( uint32_t const& indexCount, uint32_t const& instanceCount = 1, uint32_t const& firstIndex = 0, int32_t const& vertexOffset = 0, uint32_t const& firstInstance = 0 ) {
		le_renderer::encoder_graphics_i.draw_indexed( self, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance );
		return *this;
	}

	GraphicsEncoder& drawMeshTasks( const uint32_t& x_count = 1, const uint32_t& y_count = 1, const uint32_t z_count = 1 ) {
		le_renderer::encoder_graphics_i.draw_mesh_tasks( self, x_count, y_count, z_count );
		return *this;
	}

	GraphicsEncoder& drawMeshTasksNV( const uint32_t& taskCount, const uint32_t& firstTask = 0 ) {
		le_renderer::encoder_graphics_i.draw_mesh_tasks_nv( self, taskCount, firstTask );
		return *this;
	}

	GraphicsEncoder& bindGraphicsPipeline( le_gpso_handle pipelineHandle ) {
		le_renderer::encoder_graphics_i.bind_graphics_pipeline( self, pipelineHandle );
		return *this;
	}

	GraphicsEncoder& setLineWidth( float const& lineWidth ) {
		le_renderer::encoder_graphics_i.set_line_width( self, lineWidth );
		return *this;
	}

	GraphicsEncoder& setViewports( uint32_t firstViewport, const uint32_t& viewportCount, const le::Viewport* pViewports ) {
		le_renderer::encoder_graphics_i.set_viewport( self, firstViewport, viewportCount, pViewports );
		return *this;
	}

	GraphicsEncoder& setScissors( uint32_t firstScissor, const uint32_t scissorCount, const le::Rect2D* pScissors ) {
		le_renderer::encoder_graphics_i.set_scissor( self, firstScissor, scissorCount, pScissors );
		return *this;
	}

	GraphicsEncoder& bindIndexBuffer( le_buffer_resource_handle const& bufferId, uint64_t const& offset, IndexType const& indexType = IndexType::eUint16 ) {
		le_renderer::encoder_graphics_i.bind_index_buffer( self, bufferId, offset, indexType );
		return *this;
	}

	/// \param firstBinding: first binding index
	/// \param pOffsets: byte offset per-binding. consider initialising this with a stack-allocated array as in `uint64_t offsets[] = {0};`
	GraphicsEncoder& bindVertexBuffers( uint32_t const& firstBinding, uint32_t const& bindingCount, le_buffer_resource_handle const* pBufferId, uint64_t const* pOffsets ) {
		le_renderer::encoder_graphics_i.bind_vertex_buffers( self, firstBinding, bindingCount, pBufferId, pOffsets );
		return *this;
	}

	/// \brief Set index data directly by uploading data via GPU scratch buffer
	/// \note if either `data == nullptr`, or numBytes == 0, this method call has no effect.
	GraphicsEncoder& setIndexData( void const* data, uint64_t const& numBytes, IndexType const& indexType = IndexType::eUint16, le_renderer_api::command_buffer_encoder_interface_t::buffer_binding_info_o* transient_buffer_info_readback = nullptr ) {
		le_renderer::encoder_graphics_i.set_index_data( self, data, numBytes, indexType, transient_buffer_info_readback );
		return *this;
	}

	/// \brief Set vertex data directly by uploading data via GPU scratch buffer
	/// \note if either `data == nullptr`, or numBytes == 0, this method call has no effect.
	GraphicsEncoder& setVertexData( void const* data, uint64_t const& numBytes, uint32_t const& bindingIndex, le_renderer_api::command_buffer_encoder_interface_t::buffer_binding_info_o* transient_buffer_info_readback = nullptr ) {
		le_renderer::encoder_graphics_i.set_vertex_data( self, data, numBytes, bindingIndex, transient_buffer_info_readback );
		return *this;
	}

	GraphicsEncoder& getRenderpassExtent( le::Extent2D* extent ) {
		le_renderer::encoder_graphics_i.get_extent( self, extent );
		return *this;
	}

	le::Extent2D getRenderpassExtent() {
		le::Extent2D result;
		le_renderer::encoder_graphics_i.get_extent( self, &result );
		return result;
	}

	operator auto() {
		return self;
	}
};

class ComputeEncoder {
	le_command_buffer_encoder_o* self = nullptr;

  public:
	ComputeEncoder()  = delete;
	~ComputeEncoder() = default;

	ComputeEncoder( le_command_buffer_encoder_o* self_ )
	    : self( self_ ) {
	}

	operator auto() {
		return self;
	}

	le_pipeline_manager_o* getPipelineManager() {
		return le_renderer::encoder_compute_i.get_pipeline_manager( self );
	}

	ComputeEncoder& bindComputePipeline( le_cpso_handle pipelineHandle ) {
		le_renderer::encoder_compute_i.bind_compute_pipeline( self, pipelineHandle );
		return *this;
	}

	ComputeEncoder& setPushConstantData( void const* data, uint64_t const& numBytes ) {
		le_renderer::encoder_compute_i.set_push_constant_data( self, data, numBytes );
		return *this;
	}

	ComputeEncoder& bindArgumentBufferExplicit( uint32_t const& set_idx, uint32_t const& binding_idx, le_buffer_resource_handle const& bufferId, uint64_t const& offset = 0, uint64_t const& range = ( ~0ULL ) ) {
		le_renderer::encoder_compute_i.bind_argument_buffer_explicit( self, bufferId, set_idx, binding_idx, offset, range );
		return *this;
	}

	ComputeEncoder& setArgumentDataExplicit( uint32_t const& set_idx, uint32_t const& binding_idx, void const* data, size_t const& numBytes ) {
		le_renderer::encoder_compute_i.set_argument_data_explicit( self, set_idx, binding_idx, data, numBytes );
		return *this;
	}

	ComputeEncoder& setArgumentTextureExplicit( uint32_t const& set_idx, uint32_t const& binding_idx, le_texture_handle const& textureId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_compute_i.set_argument_texture_explicit( self, textureId, set_idx, binding_idx, arrayIndex );
		return *this;
	}

	ComputeEncoder& setArgumentImageExplicit( uint32_t const& set_idx, uint32_t const& binding_idx, le_image_resource_handle const& imageId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_compute_i.set_argument_image_explicit( self, imageId, set_idx, binding_idx, arrayIndex );
		return *this;
	}

	ComputeEncoder& bindArgumentBuffer( uint64_t const& argumentName, le_buffer_resource_handle const& bufferId, uint64_t const& offset = 0, uint64_t const& range = ( ~0ULL ) ) {
		le_renderer::encoder_compute_i.bind_argument_buffer( self, bufferId, argumentName, offset, range );
		return *this;
	}

	ComputeEncoder& setArgumentData( uint64_t const& argumentNameId, void const* data, size_t const& numBytes ) {
		le_renderer::encoder_compute_i.set_argument_data( self, argumentNameId, data, numBytes );
		return *this;
	}

	ComputeEncoder& setArgumentTexture( uint64_t const& argumentName, le_texture_handle const& textureId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_compute_i.set_argument_texture( self, textureId, argumentName, arrayIndex );
		return *this;
	}

	ComputeEncoder& setArgumentImage( uint64_t const& argumentName, le_image_resource_handle const& imageId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_compute_i.set_argument_image( self, imageId, argumentName, arrayIndex );
		return *this;
	}

	ComputeEncoder& fillBuffer( le_buffer_resource_handle const& dstBuffer, size_t const& byteOffsetDst, size_t const& numBytes, uint32_t const& data ) {
		le_renderer::encoder_compute_i.fill_buffer( self, dstBuffer, byteOffsetDst, numBytes, data );
		return *this;
	}

	ComputeEncoder& dispatch( const uint32_t& groupCountX = 1, const uint32_t& groupCountY = 1, const uint32_t& groupCountZ = 1 ) {
		le_renderer::encoder_compute_i.dispatch( self, groupCountX, groupCountY, groupCountZ );
		return *this;
	}
	
	ComputeEncoder& dispatchIndirect(le_buffer_resource_handle buf, const uint64_t& offset=0) {
		le_renderer::encoder_compute_i.dispatch_indirect(self, buf, offset);
		return *this;
	}

	ComputeEncoder& bufferMemoryBarrier(
	    le::PipelineStageFlags2 const&   srcStageMask,
	    le::PipelineStageFlags2 const&   dstStageMask,
	    le::AccessFlags2 const&          srcAccessMask,
	    le::AccessFlags2 const&          dstAccessMask,
	    le_buffer_resource_handle const& buffer,
	    uint64_t const&                  offset = 0,
	    uint64_t const&                  range  = ~( 0ull ) ) {
		le_renderer::encoder_compute_i.buffer_memory_barrier( self, srcStageMask, dstStageMask, srcAccessMask, dstAccessMask, buffer, offset, range );
		return *this;
	}
};
class TransferEncoder {

	le_command_buffer_encoder_o* self = nullptr;

  public:
	TransferEncoder()  = delete;
	~TransferEncoder() = default;

	TransferEncoder( le_command_buffer_encoder_o* self_ )
	    : self( self_ ) {
	}

	operator auto() {
		return self;
	}

	TransferEncoder& writeToBuffer( le_buffer_resource_handle const& dstBuffer, size_t const& byteOffsetDst, void const* data, size_t const& numBytes ) {
		le_renderer::encoder_transfer_i.write_to_buffer( self, dstBuffer, byteOffsetDst, data, numBytes );
		return *this;
	}

	TransferEncoder& fillBuffer( le_buffer_resource_handle const& dstBuffer, size_t const& byteOffsetDst, size_t const& numBytes, uint32_t const& data ) {
		le_renderer::encoder_transfer_i.fill_buffer( self, dstBuffer, byteOffsetDst, numBytes, data );
		return *this;
	}

	TransferEncoder& writeToImage( le_image_resource_handle const& dstImg, le_write_to_image_settings_t const& writeInfo, void const* data, size_t const& numBytes ) {
		le_renderer::encoder_transfer_i.write_to_image( self, dstImg, writeInfo, data, numBytes );
		return *this;
	}

	TransferEncoder& mapBufferMemory( le_buffer_resource_handle const& dstBuffer, size_t const& byteOffsetDst, size_t const& numBytes, void** p_mem_addr ) {
		le_renderer::encoder_transfer_i.map_buffer_memory( self, dstBuffer, byteOffsetDst, numBytes, p_mem_addr );
		return *this;
	}

	TransferEncoder& mapImageMemory( le_image_resource_handle const& dstImg, le_write_to_image_settings_t const& writeInfo, size_t const& numBytes, void** p_mem_addr ) {
		le_renderer::encoder_transfer_i.map_image_memory( self, dstImg, writeInfo, numBytes, p_mem_addr );
		return *this;
	}

	TransferEncoder& bufferMemoryBarrier(
	    le::PipelineStageFlags2 const&   srcStageMask,
	    le::PipelineStageFlags2 const&   dstStageMask,
	    le::AccessFlags2 const&          srcAccessMask,
	    le::AccessFlags2 const&          dstAccessMask,
	    le_buffer_resource_handle const& buffer,
	    uint64_t const&                  offset = 0,
	    uint64_t const&                  range  = ~( 0ull ) ) {
		le_renderer::encoder_transfer_i.buffer_memory_barrier( self, srcStageMask, dstStageMask, srcAccessMask, dstAccessMask, buffer, offset, range );
		return *this;
	}
};

class RtxEncoder {
	le_command_buffer_encoder_o* self = nullptr;

  public:
	RtxEncoder()  = delete;
	~RtxEncoder() = default;

	RtxEncoder( le_command_buffer_encoder_o* self_ )
	    : self( self_ ) {
	}

	operator auto() {
		return self;
	}

	RtxEncoder& setArgumentTlas( uint64_t const& argumentName, le_tlas_resource_handle const& tlasId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_rtx_i.set_argument_tlas( self, tlasId, argumentName, arrayIndex );
		return *this;
	}

	class ShaderBindingTableBuilder {
		RtxEncoder const&          parent;
		le_shader_binding_table_o* sbt = nullptr;

	  public:
		ShaderBindingTableBuilder( RtxEncoder const& parent_, le_rtxpso_handle pso )
		    : parent( parent_ )
		    , sbt( le_renderer::encoder_rtx_i.build_sbt( parent.self, pso ) ) {
		}

		ShaderBindingTableBuilder& setRayGenIdx( uint32_t idx ) {
			le_renderer::encoder_rtx_i.sbt_set_ray_gen( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder& addHitIdx( uint32_t idx ) {
			le_renderer::encoder_rtx_i.sbt_add_hit( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder& addCallableIdx( uint32_t idx ) {
			le_renderer::encoder_rtx_i.sbt_add_callable( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder& addMissIdx( uint32_t idx ) {
			le_renderer::encoder_rtx_i.sbt_add_miss( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder& addParameterValueU32( uint32_t val ) {
			le_renderer::encoder_rtx_i.sbt_add_u32_param( sbt, val );
			return *this;
		}

		ShaderBindingTableBuilder& addParameterValueF32( float val ) {
			le_renderer::encoder_rtx_i.sbt_add_f32_param( sbt, val );
			return *this;
		}

		le_shader_binding_table_o* build() {
			return le_renderer::encoder_rtx_i.sbt_validate( sbt );
		}
	};

	RtxEncoder& bindRtxPipeline( le_shader_binding_table_o* sbt ) {
		le_renderer::encoder_rtx_i.bind_rtx_pipeline( self, sbt );
		return *this;
	}

	RtxEncoder& traceRays( uint32_t const& width, uint32_t const& height, uint32_t const& depth = 1 ) {
		le_renderer::encoder_rtx_i.trace_rays( self, width, height, depth );
		return *this;
	}
};

// ----------------------------------------------------------------------

} // namespace le
#endif // __cplusplus
#endif // GUARD_LE_RENDERER_H
