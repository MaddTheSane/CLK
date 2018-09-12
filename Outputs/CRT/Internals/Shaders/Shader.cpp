//
//  Shader.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 07/02/2016.
//  Copyright 2016 Thomas Harte. All rights reserved.
//

#include "Shader.hpp"

#include <cstdio>
#include <cstring>

using namespace OpenGL;

namespace {
	// The below is disabled because it isn't context/thread-specific. Which makes it
	// fairly 'unuseful'.
//	Shader *bound_shader = nullptr;
}

GLuint Shader::compile_shader(const std::string &source, GLenum type) {
	GLuint shader = glCreateShader(type);
	const char *c_str = source.c_str();
	glShaderSource(shader, 1, &c_str, NULL);
	glCompileShader(shader);

#ifdef DEBUG
	GLint isCompiled = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
	if(isCompiled == GL_FALSE) {
		GLint logLength;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
		if(logLength > 0) {
			GLchar *log = new GLchar[static_cast<std::size_t>(logLength)];
			glGetShaderInfoLog(shader, logLength, &logLength, log);
			printf("Compile log:\n%s\n", log);
			delete[] log;
		}

		throw (type == GL_VERTEX_SHADER) ? VertexShaderCompilationError : FragmentShaderCompilationError;
	}
#endif

	return shader;
}

Shader::Shader(const std::string &vertex_shader, const std::string &fragment_shader, const std::vector<AttributeBinding> &attribute_bindings) {
	shader_program_ = glCreateProgram();
	GLuint vertex = compile_shader(vertex_shader, GL_VERTEX_SHADER);
	GLuint fragment = compile_shader(fragment_shader, GL_FRAGMENT_SHADER);

	glAttachShader(shader_program_, vertex);
	glAttachShader(shader_program_, fragment);

	for(const auto &binding : attribute_bindings) {
		glBindAttribLocation(shader_program_, binding.index, binding.name.c_str());
	}

	glLinkProgram(shader_program_);

#ifdef DEBUG
	GLint didLink = 0;
	glGetProgramiv(shader_program_, GL_LINK_STATUS, &didLink);
	if(didLink == GL_FALSE) {
		GLint logLength;
		glGetProgramiv(shader_program_, GL_INFO_LOG_LENGTH, &logLength);
		if(logLength > 0) {
			GLchar *log = new GLchar[static_cast<std::size_t>(logLength)];
			glGetProgramInfoLog(shader_program_, logLength, &logLength, log);
			printf("Link log:\n%s\n", log);
			delete[] log;
		}
		throw ProgramLinkageError;
	}
#endif
}

Shader::~Shader() {
//	if(bound_shader == this) Shader::unbind();
	glDeleteProgram(shader_program_);
}

void Shader::bind() {
//	if(bound_shader != this) {
		glUseProgram(shader_program_);
//		bound_shader = this;
//	}
	flush_functions();
}

void Shader::unbind() {
//	bound_shader = nullptr;
	glUseProgram(0);
}

GLint Shader::get_attrib_location(const std::string &name) {
	return glGetAttribLocation(shader_program_, name.c_str());
}

GLint Shader::get_uniform_location(const std::string &name) {
	return glGetUniformLocation(shader_program_, name.c_str());
}

void Shader::enable_vertex_attribute_with_pointer(const std::string &name, GLint size, GLenum type, GLboolean normalised, GLsizei stride, const GLvoid *pointer, GLuint divisor) {
	GLint location = get_attrib_location(name);
	glEnableVertexAttribArray((GLuint)location);
	glVertexAttribPointer((GLuint)location, size, type, normalised, stride, pointer);
	glVertexAttribDivisor((GLuint)location, divisor);
}

// The various set_uniforms...
#define location() glGetUniformLocation(shader_program_, name.c_str())
void Shader::set_uniform(const std::string &name, GLint value) {
	enqueue_function([name, value, this] {
		glUniform1i(location(), value);
	});
}

void Shader::set_uniform(const std::string &name, GLuint value) {
	enqueue_function([name, value, this] {
		glUniform1ui(location(), value);
	});
}

void Shader::set_uniform(const std::string &name, GLfloat value) {
	enqueue_function([name, value, this] {
		glUniform1f(location(), value);
	});
}


void Shader::set_uniform(const std::string &name, GLint value1, GLint value2) {
	enqueue_function([name, value1, value2, this] {
		glUniform2i(location(), value1, value2);
	});
}

void Shader::set_uniform(const std::string &name, GLfloat value1, GLfloat value2) {
	enqueue_function([name, value1, value2, this] {
		GLint location = location();
		glUniform2f(location, value1, value2);
	});
}

void Shader::set_uniform(const std::string &name, GLuint value1, GLuint value2) {
	enqueue_function([name, value1, value2, this] {
		glUniform2ui(location(), value1, value2);
	});
}

void Shader::set_uniform(const std::string &name, GLint value1, GLint value2, GLint value3) {
	enqueue_function([name, value1, value2, value3, this] {
		glUniform3i(location(), value1, value2, value3);
	});
}

void Shader::set_uniform(const std::string &name, GLfloat value1, GLfloat value2, GLfloat value3) {
	enqueue_function([name, value1, value2, value3, this] {
		glUniform3f(location(), value1, value2, value3);
	});
}

void Shader::set_uniform(const std::string &name, GLuint value1, GLuint value2, GLuint value3) {
	enqueue_function([name, value1, value2, value3, this] {
		glUniform3ui(location(), value1, value2, value3);
	});
}

void Shader::set_uniform(const std::string &name, GLint value1, GLint value2, GLint value3, GLint value4) {
	enqueue_function([name, value1, value2, value3, value4, this] {
		glUniform4i(location(), value1, value2, value3, value4);
	});
}

void Shader::set_uniform(const std::string &name, GLfloat value1, GLfloat value2, GLfloat value3, GLfloat value4) {
	enqueue_function([name, value1, value2, value3, value4, this] {
		glUniform4f(location(), value1, value2, value3, value4);
	});
}

void Shader::set_uniform(const std::string &name, GLuint value1, GLuint value2, GLuint value3, GLuint value4) {
	enqueue_function([name, value1, value2, value3, value4, this] {
		glUniform4ui(location(), value1, value2, value3, value4);
	});
}

void Shader::set_uniform(const std::string &name, GLint size, GLsizei count, const GLint *values) {
	std::size_t number_of_values = static_cast<std::size_t>(count) * static_cast<std::size_t>(size);
	GLint *values_copy = new GLint[number_of_values];
	std::memcpy(values_copy, values, sizeof(*values) * static_cast<std::size_t>(number_of_values));

	enqueue_function([name, size, count, values_copy, this] {
		switch(size) {
			case 1: glUniform1iv(location(), count, values_copy);	break;
			case 2: glUniform2iv(location(), count, values_copy);	break;
			case 3: glUniform3iv(location(), count, values_copy);	break;
			case 4: glUniform4iv(location(), count, values_copy);	break;
		}
		delete[] values_copy;
	});
}

void Shader::set_uniform(const std::string &name, GLint size, GLsizei count, const GLfloat *values) {
	std::size_t number_of_values = static_cast<std::size_t>(count) * static_cast<std::size_t>(size);
	GLfloat *values_copy = new GLfloat[number_of_values];
	std::memcpy(values_copy, values, sizeof(*values) * static_cast<std::size_t>(number_of_values));

	enqueue_function([name, size, count, values_copy, this] {
		switch(size) {
			case 1: glUniform1fv(location(), count, values_copy);	break;
			case 2: glUniform2fv(location(), count, values_copy);	break;
			case 3: glUniform3fv(location(), count, values_copy);	break;
			case 4: glUniform4fv(location(), count, values_copy);	break;
		}
		delete[] values_copy;
	});
}

void Shader::set_uniform(const std::string &name, GLint size, GLsizei count, const GLuint *values) {
	std::size_t number_of_values = static_cast<std::size_t>(count) * static_cast<std::size_t>(size);
	GLuint *values_copy = new GLuint[number_of_values];
	std::memcpy(values_copy, values, sizeof(*values) * static_cast<std::size_t>(number_of_values));

	enqueue_function([name, size, count, values_copy, this] {
		switch(size) {
			case 1: glUniform1uiv(location(), count, values_copy);	break;
			case 2: glUniform2uiv(location(), count, values_copy);	break;
			case 3: glUniform3uiv(location(), count, values_copy);	break;
			case 4: glUniform4uiv(location(), count, values_copy);	break;
		}
		delete[] values_copy;
	});
}

void Shader::set_uniform_matrix(const std::string &name, GLint size, bool transpose, const GLfloat *values) {
	set_uniform_matrix(name, size, 1, transpose, values);
}

void Shader::set_uniform_matrix(const std::string &name, GLint size, GLsizei count, bool transpose, const GLfloat *values) {
	std::size_t number_of_values = static_cast<std::size_t>(count) * static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
	std::vector<GLfloat> values_copy(number_of_values);
	std::memcpy(values_copy.data(), values, sizeof(*values) * number_of_values);

	enqueue_function([name, size, count, transpose, values_copy, this] {
		GLboolean glTranspose = transpose ? GL_TRUE : GL_FALSE;
		switch(size) {
			case 2: glUniformMatrix2fv(location(), count, glTranspose, values_copy.data());	break;
			case 3: glUniformMatrix3fv(location(), count, glTranspose, values_copy.data());	break;
			case 4: glUniformMatrix4fv(location(), count, glTranspose, values_copy.data());	break;
		}
	});
}

void Shader::enqueue_function(std::function<void(void)> function) {
	std::lock_guard<std::mutex> function_guard(function_mutex_);
	enqueued_functions_.push_back(function);
}

void Shader::flush_functions() {
	std::lock_guard<std::mutex> function_guard(function_mutex_);
	for(std::function<void(void)> function : enqueued_functions_) {
		function();
	}
	enqueued_functions_.clear();
}
