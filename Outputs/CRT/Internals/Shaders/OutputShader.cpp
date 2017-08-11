//
//  OutputShader.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 27/04/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#include "OutputShader.hpp"

#include <stdlib.h>
#include <math.h>

using namespace OpenGL;

namespace {
	const OpenGL::Shader::AttributeBinding bindings[] = {
		{"position", 0},
		{"srcCoordinates", 1},
		{nullptr}
	};
}

std::unique_ptr<OutputShader> OutputShader::make_shader(const char *fragment_methods, const char *colour_expression, bool use_usampler) {
	const char *sampler_type = use_usampler ? "usampler2D" : "sampler2D";

	char *vertex_shader;
	asprintf(&vertex_shader,
		"#version 150\n"

		"in vec2 horizontal;"
		"in vec2 vertical;"

		"uniform vec2 boundsOrigin;"
		"uniform vec2 boundsSize;"
		"uniform vec2 positionConversion;"
		"uniform vec2 scanNormal;"
		"uniform %s texID;"
		"uniform float inputScaler;"
		"uniform int textureHeightDivisor;"

		"out float lateralVarying;"
		"out vec2 srcCoordinatesVarying;"
		"out vec2 iSrcCoordinatesVarying;"

		"void main(void)"
		"{"
			"float lateral = float(gl_VertexID & 1);"
			"float longitudinal = float((gl_VertexID & 2) >> 1);"
			"float x = mix(horizontal.x, horizontal.y, longitudinal);"

			"lateralVarying = lateral - 0.5;"

			"vec2 vSrcCoordinates = vec2(x, vertical.y);"
			"ivec2 textureSize = textureSize(texID, 0) * ivec2(1, textureHeightDivisor);"
			"iSrcCoordinatesVarying = vSrcCoordinates;"
			"srcCoordinatesVarying = vec2(inputScaler * vSrcCoordinates.x / textureSize.x, (vSrcCoordinates.y + 0.5) / textureSize.y);"
			"srcCoordinatesVarying.x = srcCoordinatesVarying.x - mod(srcCoordinatesVarying.x, 1.0 / textureSize.x);"

			"vec2 vPosition = vec2(x, vertical.x);"
			"vec2 floatingPosition = (vPosition / positionConversion) + lateral * scanNormal;"
			"vec2 mappedPosition = (floatingPosition - boundsOrigin) / boundsSize;"
			"gl_Position = vec4(mappedPosition.x * 2.0 - 1.0, 1.0 - mappedPosition.y * 2.0, 0.0, 1.0);"
		"}", sampler_type);

	char *fragment_shader;
	asprintf(&fragment_shader,
		"#version 150\n"

		"in float lateralVarying;"
		"in vec2 srcCoordinatesVarying;"
		"in vec2 iSrcCoordinatesVarying;"

		"out vec4 fragColour;"

		"uniform %s texID;"
		"uniform float gamma;"
		"uniform float alphaMultiplier;"

		"\n%s\n"

		"void main(void)"
		"{"
			"float alpha = 0.5*clamp(alphaMultiplier * cos(lateralVarying), 0.0, 1.0);"
			"fragColour = vec4(pow(%s, vec3(gamma)), alpha);"
		"}",
	sampler_type, fragment_methods, colour_expression);

	std::unique_ptr<OutputShader> result(new OutputShader(vertex_shader, fragment_shader, bindings));
	free(vertex_shader);
	free(fragment_shader);

	return result;
}

void OutputShader::set_output_size(unsigned int output_width, unsigned int output_height, Outputs::CRT::Rect visible_area) {
	GLfloat outputAspectRatioMultiplier = ((float)output_width / (float)output_height) / (4.0f / 3.0f);

	GLfloat bonusWidth = (outputAspectRatioMultiplier - 1.0f) * visible_area.size.width;
	visible_area.origin.x -= bonusWidth * 0.5f * visible_area.size.width;
	visible_area.size.width *= outputAspectRatioMultiplier;

	set_uniform("boundsOrigin", (GLfloat)visible_area.origin.x, (GLfloat)visible_area.origin.y);
	set_uniform("boundsSize", (GLfloat)visible_area.size.width, (GLfloat)visible_area.size.height);

	// This is a very broad low-pass filter: disable the (very subtle, but justified*) scanline effect if output is
	// below 700px.
	//
	// * justification is partly based on the very slightly overlaps between adjacent lines that rounding errors bring,
	// partly on the real physics of an electron gun. But here it's portrayed using very close to ideal electronics.
	// Most people won't be aware that an effect is being applied, but the visible double-hits of not applying slight
	// fading can be very obvious.
	set_uniform("alphaMultiplier", (output_height > 700) ? 1.0f : 256.0f);
}

void OutputShader::set_source_texture_unit(GLenum unit) {
	set_uniform("texID", (GLint)(unit - GL_TEXTURE0));
}

void OutputShader::set_timing(unsigned int height_of_display, unsigned int cycles_per_line, unsigned int horizontal_scan_period, unsigned int vertical_scan_period, unsigned int vertical_period_divider) {
	GLfloat scan_angle = atan2f(1.0f / (float)height_of_display, 1.0f);
	GLfloat scan_normal[] = { -sinf(scan_angle), cosf(scan_angle)};
	GLfloat multiplier = (float)cycles_per_line / ((float)height_of_display * (float)horizontal_scan_period);
	scan_normal[0] *= multiplier;
	scan_normal[1] *= multiplier;

	set_uniform("scanNormal", scan_normal[0], scan_normal[1]);
	set_uniform("positionConversion", (GLfloat)horizontal_scan_period, (GLfloat)vertical_scan_period / (GLfloat)vertical_period_divider);
}

void OutputShader::set_gamma_ratio(float ratio) {
	set_uniform("gamma", ratio);
}

void OutputShader::set_input_width_scaler(float input_scaler) {
	set_uniform("inputScaler", input_scaler);
}

void OutputShader::set_origin_is_double_height(bool is_double_height) {
	set_uniform("textureHeightDivisor", is_double_height ? 2 : 1);
}
