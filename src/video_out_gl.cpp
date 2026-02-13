// Copyright (c) 2012, Thomas Goyne <plorkyeran@aegisub.org>
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

/// @file video_out_gl.cpp
/// @brief OpenGL based video renderer
/// @ingroup video
///

#include <algorithm>
#include <utility>

#include <libaegisub/log.h>

#include "video_out_gl.h"
#include "video_frame.h"

namespace {
template<typename Exception>
BOOST_NOINLINE void throw_error(GLenum err, const char *msg) {
	LOG_E("video/out/gl") << msg << " failed with error code " << err;
	throw Exception(msg, err);
}
}

#define DO_CHECK_ERROR(cmd, Exception, msg) \
	do { \
		cmd; \
		GLenum err = glGetError(); \
		if (BOOST_UNLIKELY(err)) \
			throw_error<Exception>(err, msg); \
	} while(0);
#define CHECK_INIT_ERROR(cmd) DO_CHECK_ERROR(cmd, VideoOutInitException, #cmd)
#define CHECK_ERROR(cmd) DO_CHECK_ERROR(cmd, VideoOutRenderException, #cmd)

VideoOutGL::VideoOutGL() { }

VideoOutGL::~VideoOutGL() {
	CleanupGL();
}

void VideoOutGL::InitShaders() {
	const char *vertex_shader_src = R"(
		#version 130
		in vec2 a_position;
		in vec2 a_texcoord;
		out vec2 v_texcoord;
		uniform vec2 u_viewport;

		void main() {
			vec2 ndc = (a_position / u_viewport) * 2.0 - 1.0;
			gl_Position = vec4(ndc, 0.0, 1.0);
			v_texcoord = a_texcoord;
		}
	)";

	const char *fragment_shader_src = R"(
		#version 130
		in vec2 v_texcoord;
		out vec4 fragColor;
		uniform sampler2D u_tex;

		void main() {
			fragColor = texture(u_tex, v_texcoord);
		}
	)";

	// Compile vertex shader
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	CHECK_INIT_ERROR(glShaderSource(vs, 1, &vertex_shader_src, nullptr));
	CHECK_INIT_ERROR(glCompileShader(vs));

	GLint success;
	glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
	if (!success) {
		char info_log[512];
		glGetShaderInfoLog(vs, 512, nullptr, info_log);
		glDeleteShader(vs);
		throw VideoOutInitException("Vertex shader compilation failed: " + std::string(info_log));
	}

	// Compile fragment shader
	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	CHECK_INIT_ERROR(glShaderSource(fs, 1, &fragment_shader_src, nullptr));
	CHECK_INIT_ERROR(glCompileShader(fs));

	glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
	if (!success) {
		char info_log[512];
		glGetShaderInfoLog(fs, 512, nullptr, info_log);
		glDeleteShader(vs);
		glDeleteShader(fs);
		throw VideoOutInitException("Fragment shader compilation failed: " + std::string(info_log));
	}

	// Link program
	shader_program = glCreateProgram();
	CHECK_INIT_ERROR(glAttachShader(shader_program, vs));
	CHECK_INIT_ERROR(glAttachShader(shader_program, fs));
	CHECK_INIT_ERROR(glLinkProgram(shader_program));

	glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
	if (!success) {
		char info_log[512];
		glGetProgramInfoLog(shader_program, 512, nullptr, info_log);
		glDeleteShader(vs);
		glDeleteShader(fs);
		glDeleteProgram(shader_program);
		throw VideoOutInitException("Shader program linking failed: " + std::string(info_log));
	}

	glDeleteShader(vs); // TODO: RAII
	glDeleteShader(fs);

	// Get uniform locations
	u_viewport = glGetUniformLocation(shader_program, "u_viewport");
	u_tex = glGetUniformLocation(shader_program, "u_tex");
}

void VideoOutGL::InitGL() {
	// TODO: proper cleanup in case of error (just call CleanupGL on exception)
	if (initialized) return;

	// Create texture
	CHECK_INIT_ERROR(glGenTextures(1, &texture));
	CHECK_INIT_ERROR(glBindTexture(GL_TEXTURE_2D, texture));

	// Set texture parameters
	CHECK_INIT_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	CHECK_INIT_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	CHECK_INIT_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	CHECK_INIT_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

	// Create VAO
	CHECK_INIT_ERROR(glGenVertexArrays(1, &vao));
	CHECK_INIT_ERROR(glBindVertexArray(vao));

	// Create VBO
	CHECK_INIT_ERROR(glGenBuffers(1, &vbo));
	CHECK_INIT_ERROR(glBindBuffer(GL_ARRAY_BUFFER, vbo));

	// Vertex format: position (x, y), texcoord (u, v)
	CHECK_INIT_ERROR(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0));
	CHECK_INIT_ERROR(glEnableVertexAttribArray(0));

	CHECK_INIT_ERROR(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float))));
	CHECK_INIT_ERROR(glEnableVertexAttribArray(1));

	// Initialize shaders
	InitShaders();

	// Cleanup
	CHECK_INIT_ERROR(glBindTexture(GL_TEXTURE_2D, 0));
	CHECK_INIT_ERROR(glBindVertexArray(0));
	CHECK_INIT_ERROR(glBindBuffer(GL_ARRAY_BUFFER, 0));

	initialized = true;
}

void VideoOutGL::CleanupGL() {
	if (texture) {
		glDeleteTextures(1, &texture);
		texture = 0;
	}
	if (vbo) {
		glDeleteBuffers(1, &vbo);
		vbo = 0;
	}
	if (vao) {
		glDeleteVertexArrays(1, &vao);
		vao = 0;
	}
	if (shader_program) {
		glDeleteProgram(shader_program);
		shader_program = 0;
	}
	initialized = false;
}

void VideoOutGL::UploadFrameData(VideoFrame const& frame) {
	if (!initialized) {
		InitGL();
	}

	CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, texture));

	// Reallocate texture if size changed
	if (tex_width != frame.width || tex_height != frame.height) {
		CHECK_ERROR(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height,
			0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr));
		tex_width = frame.width;
		tex_height = frame.height;
	}

	// Upload frame data
	// Handle pitch properly - the frame might have padding at the end of each row
	CHECK_ERROR(glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.pitch / 4));
	CHECK_ERROR(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height,
		GL_BGRA, GL_UNSIGNED_BYTE, frame.data.data()));
	CHECK_ERROR(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));

	CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, 0));

	frameFlipped = frame.flipped;
}

void VideoOutGL::Render(int vp_width, int vp_height, int x, int y, int width, int height) {
	if (!initialized || tex_width == 0 || tex_height == 0) {
		return; // Nothing to render
	}

	// Get current viewport
	// GLint viewport[4];
	// glGetIntegerv(GL_VIEWPORT, viewport);
	// int vp_width = viewport[2];
	// int vp_height = viewport[3];

	float left = x;
	float right = x + width;
	float bottom = y;
	float top = y + height;

	if (frameFlipped)
		std::swap(top, bottom);

	// Texture coordinates
	float vertices[] = {
		// Position      // TexCoord
		left,  bottom,   0.0f, 1.0f,  // Bottom-left
		right, bottom,   1.0f, 1.0f,  // Bottom-right
		right, top,      1.0f, 0.0f,  // Top-right

		right, top,      1.0f, 0.0f,  // Top-right
		left,  top,      0.0f, 0.0f,  // Top-left
		left,  bottom,   0.0f, 1.0f   // Bottom-left
	};

	// Use shader program
	CHECK_ERROR(glUseProgram(shader_program));

	// Set uniforms
	CHECK_ERROR(glUniform2f(u_viewport, vp_width, vp_height));
	CHECK_ERROR(glUniform1i(u_tex, 0));

	// Bind texture
	CHECK_ERROR(glActiveTexture(GL_TEXTURE0));
	CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, texture));

	// Upload vertex data and draw
	CHECK_ERROR(glBindVertexArray(vao));
	CHECK_ERROR(glBindBuffer(GL_ARRAY_BUFFER, vbo));
	CHECK_ERROR(glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW));

	CHECK_ERROR(glDrawArrays(GL_TRIANGLES, 0, 6));

	// Cleanup
	CHECK_ERROR(glUseProgram(0));
	CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, 0));
	CHECK_ERROR(glBindVertexArray(0));
	CHECK_ERROR(glBindBuffer(GL_ARRAY_BUFFER, 0));
}
