#include "pal_api_loader/ApiRegistry.hpp"
#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"

// include this in application, rather.
#include "le_renderer/le_rendergraph.h"
#include "vulkan/vulkan.hpp"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

#ifdef PLUGIN_PAL_WINDOW_STATIC
	Registry::addApiStatic<pal_window_api>();
#else
	Registry::addApiDynamic<pal_window_api>( true );
#endif

#ifdef PLUGIN_LE_BACKEND_VK_STATIC
	Registry::addApiStatic<le_backend_vk_api>();
#else
	Registry::addApiDynamic<le_backend_vk_api>( true );
#endif

#ifdef PLUGIN_LE_SWAPCHAIN_VK_STATIC
	Registry::addApiStatic<le_swapchain_vk_api>();
#else
	Registry::addApiDynamic<le_swapchain_vk_api>( true );
#endif

#ifdef PLUGIN_LE_RENDERER_STATIC
	Registry::addApiStatic<le_renderer_api>();
#else
	Registry::addApiDynamic<le_renderer_api>( true );
#endif

	{

		// NOTE: we need a way to easily add enabled device extensions
		// and to add easily to requestedExtensions.

		uint32_t numRequestedExtensions = 0;

		pal::Window::init();
		auto requestedExtensions = pal::Window::getRequiredVkExtensions( &numRequestedExtensions );

		pal::Window::Settings settings;
		settings
		    .setWidth ( 640 )
		    .setHeight( 480 )
		    .setTitle ( "Hello world" )
		    ;

		pal::Window window{settings};

		le::Instance instance{requestedExtensions, numRequestedExtensions};

		window.createSurface( instance.getVkInstance() );

		le::Device device{instance};

		le::Swapchain::Settings swapchainSettings;
		swapchainSettings
		    .setImageCountHint          ( 3 )
		    .setPresentModeHint         ( le::Swapchain::Presentmode::eFifo )
		    .setWidthHint               ( window.getSurfaceWidth() )
		    .setHeightHint              ( window.getSurfaceHeight() )
		    .setVkDevice                ( device.getVkDevice() )
		    .setVkPhysicalDevice        ( device.getVkPhysicalDevice() )
		    .setGraphicsQueueFamilyIndex( device.getDefaultGraphicsQueueFamilyIndex() )
		    .setVkSurfaceKHR            ( window.getVkSurfaceKHR() )
		    ;

		{
			// create swapchain, and attach it to window via the window's VkSurface
			le::Swapchain swapchain{swapchainSettings};

			le::Renderer renderer{device, swapchain};

			renderer.setup();

			// TODO: `swapchain.reset()` needs to run when surface has been lost -
			// Swapchain will report as such.
			//
			// Window will then have to re-acquire surface.
			// then swapchain must be reset.
			// swapchain.reset(swapchainSettings);

			for ( ; window.shouldClose() == false; ) {

				Registry::pollForDynamicReload();

				pal::Window::pollEvents();

				// app.update
				// app.draw

				{

					le::RenderPass renderPassEarlyZ( "earlyZ" );
					renderPassEarlyZ.setSetupCallback( []( auto pRp, auto pDevice ) {
						auto rp     = le::RenderPassRef{pRp};
						auto device = le::Device{pDevice};

						le::ImageAttachmentInfo depthAttachmentInfo;
						depthAttachmentInfo.format  = device.getDefaultDepthStencilFormat();
//						depthAttachmentInfo.onClear = []( void *clearVal ) {
//							auto clear = reinterpret_cast<vk::ClearValue *>( clearVal );
//							clear->setDepthStencil( vk::ClearDepthStencilValue( 1.f, 0 ) );
//						};

						rp.addOutputAttachment( "depth", &depthAttachmentInfo );

						return true;
					} );


					le::RenderPass renderPassForward( "forward" );
					renderPassForward.setSetupCallback( []( auto pRp, auto pDevice ) {
						auto                    rp     = le::RenderPassRef{pRp};
						auto                    device = le::Device{pDevice};

						le::ImageAttachmentInfo colorAttachmentInfo;
						colorAttachmentInfo.format  = vk::Format::eR8G8B8A8Unorm;
//						colorAttachmentInfo.onClear = []( void *clearVal ) {
//							auto clear = reinterpret_cast<vk::ClearValue *>( clearVal );
//							clear->setColor( vk::ClearColorValue( std::array<float, 4>{{1.f, 0.f, 0.f, 1.f}} ) );
//						};

						le::ImageAttachmentInfo depthAttachmentInfo;
						depthAttachmentInfo.format = device.getDefaultDepthStencilFormat();

						//rp.addInputAttachment( "depth", &depthAttachmentInfo );

						rp.addOutputAttachment( "backbuffer", &colorAttachmentInfo );
						return true;
					} );

					le::RenderPass renderPassFinal( "final" );
					renderPassFinal.setSetupCallback( []( auto pRp, auto pDevice ) {
						auto rp     = le::RenderPassRef{pRp};
						auto device = le::Device{pDevice};

						le::ImageAttachmentInfo colorAttachmentInfo;
						colorAttachmentInfo.format = vk::Format::eR8G8B8A8Unorm;

						le::ImageAttachmentInfo depthAttachmentInfo;
						depthAttachmentInfo.format = device.getDefaultDepthStencilFormat();

						rp.addInputAttachment( "depth", &depthAttachmentInfo );
						rp.addInputAttachment( "backbuffer", &colorAttachmentInfo );

						rp.addOutputAttachment( "backbuffer", &colorAttachmentInfo );
						rp.addOutputAttachment( "depth", &depthAttachmentInfo );
						return true;
					} );

					// TODO: add setExecuteFun to renderpass - this is the method which actually
					// does specify the draw calls, and which pipelines to use.
					le::RenderModule renderModule;
					renderModule.addRenderPass( renderPassEarlyZ );
					renderModule.addRenderPass( renderPassForward );
					renderModule.addRenderPass( renderPassFinal );

					renderer.update( renderModule );

				}
			}
		}
		window.destroySurface( instance.getVkInstance() );

		pal::Window::terminate();
	}

	return 0;
}
