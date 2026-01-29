#ifndef GUARD_le_ffmpeg_pipe_H
#define GUARD_le_ffmpeg_pipe_H

#include "le_core.h"

/*

# Summary

Write raw images to an external (ffmpeg) video encoder via a pipe.

Implements `le_image_encoder_interface`.

> [!Note]
> By default, this module depends on ffmpeg being installed on your system,
> and available to call directly (which means it must be found in your `$PATH`)

# Linux

    # Ubuntu, Debian
    sudo apt-get install ffmpeg

    # Fedora
    sudo dnf install ffmpeg

# Windows

I recommend `winget`, use the following incantation:

    winget install ffmpeg

You will have to reboot your machine so that ffmpeg is automatically found
in your `$PATH`.

# How To Use This Module

You may use this module as if it was a regular image encoder, as it implements
the image encoder interface. If you want to record multiple frames into a video
you want to write to the encoder once for each frame. Keep the encoder alive
for  the full duration of recording the video.

This is particularly useful with screen recorders. You can  screen record to a
video file like this:

    le_ffmpeg_pipe_encoder_parameters_t encoder_params{
        // .command_line = "ffmpeg -r 60 -f rawvideo -pix_fmt ${pix_fmt} -s ${w}x${h} -i ${input} -filter_complex \"[0:v] fps=30,split [a][b];[a] palettegen [p];[b][p] paletteuse\" ${output}",
        .command_line = "ffmpeg -r 60 -f rawvideo -pix_fmt ${pix_fmt} -s ${w}x${h} -i ${input} -threads 0 -vcodec h264_nvenc -preset llhq -rc:v vbr_minqp -qmin:v 19 -qmax:v 21 -b:v 2500k -maxrate:v 5000k -r 30 -profile:v high ${output}",
    };

    le_swapchain_img_settings_t settings_ffmpeg_pipe{
        .width_hint  = 0, // 0 means to take the width of the renderer's first swapchain
        .height_hint = 0, // 0 means to take the height of the renderer's first swapchain

        .format_hint              = le::Format::eR8G8B8A8Unorm,
        .image_encoder_i          = le_ffmpeg_pipe::api->le_ffmpeg_pipe_encoder_i,
        .image_encoder_parameters = &encoder_params,
        .image_filename_template  = "./capture/recording_%04d.mp4",
    };

    le_screenshot::le_screenshot_i.record( self->screen_recorder, rendergraph, self->swapchain_image, &self->should_take_screenshots, &settings_ffmpeg_pipe );

`le_ffmpeg_pipe` will substitute the following f-string like parameters in the command line string:

    ${pix_fmt} | pixel format
    ${w}       | input image width in pixels
    ${h}       | input image height in pixels
    ${input}   | input into ffmpeg (either "-", or named pipe for windows)
    ${output}  | output sink for ffmpeg (either a filename, or any other output that ffmpeg supports)


Note that the filename must match the video codec parameters -- this means if
 your encoder parameters suggest an animated GIF, the filename must have a
 `.gif` file ending.

 Note that you can fine-tune the command line - as long as it creates a pipe
 this encoder will happily write to it.

 In case you don't specify a command line, the default command line is chosen.

*/

// The encoder interface is declared in:
//
// #include "shared/interfaces/le_image_encoder_interface.h"

struct le_image_encoder_interface_t;

struct le_ffmpeg_pipe_encoder_parameters_t {
	char const* command_line; // non-owning
};

struct le_ffmpeg_pipe_api {
	le_image_encoder_interface_t * le_ffmpeg_pipe_encoder_i = nullptr; // abstract image encoder interface
};

LE_MODULE( le_ffmpeg_pipe );
LE_MODULE_LOAD_DEFAULT( le_ffmpeg_pipe );

#ifdef __cplusplus

namespace le_ffmpeg_pipe {
	static const auto &api = le_ffmpeg_pipe_api_i;
} // namespace

#endif // __cplusplus
#endif
