// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file video_out_gl.h
/// @brief OpenGL based video renderer
/// @ingroup video
///

#include <libaegisub/exception.h>

struct VideoFrame;

/// @class VideoOutGL
/// @brief OpenGL based video renderer
class VideoOutGL {
private:
	GLuint texture = 0;
	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint shader_program = 0;
	GLint u_viewport = -1;
	GLint u_tex = -1;

	int tex_width = 0;
	int tex_height = 0;
	bool initialized = false;
	bool frameFlipped = false;

	void InitGL();
	void InitShaders();
	void CleanupGL();

	VideoOutGL(const VideoOutGL &) = delete;
	VideoOutGL& operator=(const VideoOutGL&) = delete;
public:
	/// @brief Set the frame to be displayed when Render() is called
	/// @param frame The frame to be displayed
	void UploadFrameData(VideoFrame const& frame);

	/// @brief Render a frame
	/// @param client_width Width in physical pixels of client window
	/// @param client_height Height in physical pixels of client window
	/// @param x Bottom left x coordinate of the target area
	/// @param y Bottom left y coordinate of the target area
	/// @param width Width in pixels of the target area
	/// @param height Height in pixels of the target area
	/// Assumes origin at bottom-left with Y increasing upward
	void Render(int client_width, int client_height, int x, int y, int width, int height);

	VideoOutGL();
	~VideoOutGL();
};

/// Base class for all exceptions thrown by VideoOutGL
DEFINE_EXCEPTION(VideoOutException, agi::Exception);

/// An OpenGL error occurred while uploading or displaying a frame
class VideoOutRenderException final : public VideoOutException {
public:
	VideoOutRenderException(const char *func, int err)
	: VideoOutException(std::string(func) + " failed with error code " + std::to_string(err))
	{ }
};

/// An OpenGL error occurred while setting up the video display
class VideoOutInitException final : public VideoOutException {
public:
	VideoOutInitException(const char *func, int err)
	: VideoOutException(std::string(func) + " failed with error code " + std::to_string(err))
	{ }
	VideoOutInitException(std::string&& msg) : VideoOutException(std::move(msg)) { }
};
