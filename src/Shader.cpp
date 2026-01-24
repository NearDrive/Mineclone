#include "Shader.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {
std::string readFile(const std::string& path, std::string& errorOut) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        errorOut = "Failed to open shader file: " + path;
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool compileShader(GLuint shader, const std::string& source, std::string& errorOut) {
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        errorOut = log;
        return false;
    }
    return true;
}

bool linkProgram(GLuint program, std::string& errorOut) {
    glLinkProgram(program);
    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        errorOut = log;
        return false;
    }
    return true;
}
} // namespace

Shader::Shader() = default;

Shader::~Shader() {
    Destroy();
}

Shader::Shader(Shader&& other) noexcept : programId_(other.programId_) {
    other.programId_ = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        Destroy();
        programId_ = other.programId_;
        other.programId_ = 0;
    }
    return *this;
}

void Shader::Destroy() {
    if (programId_ != 0) {
        glDeleteProgram(programId_);
        programId_ = 0;
    }
}

bool Shader::loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath, std::string& errorOut) {
    std::string vertError;
    std::string fragError;
    const std::string vertexSource = readFile(vertexPath, vertError);
    if (vertexSource.empty()) {
        errorOut = vertError;
        return false;
    }

    const std::string fragmentSource = readFile(fragmentPath, fragError);
    if (fragmentSource.empty()) {
        errorOut = fragError;
        return false;
    }

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    std::string compileError;

    if (!compileShader(vertexShader, vertexSource, compileError)) {
        errorOut = "Vertex shader compilation failed: " + compileError;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    if (!compileShader(fragmentShader, fragmentSource, compileError)) {
        errorOut = "Fragment shader compilation failed: " + compileError;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);

    std::string linkError;
    if (!linkProgram(program, linkError)) {
        errorOut = "Shader program link failed: " + linkError;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(program);
        return false;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (programId_ != 0) {
        glDeleteProgram(programId_);
    }

    programId_ = program;
    return true;
}

void Shader::use() const {
    glUseProgram(programId_);
}

void Shader::setMat4(const std::string& name, const glm::mat4& value) const {
    GLint location = glGetUniformLocation(programId_, name.c_str());
    if (location >= 0) {
        glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
    }
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
    GLint location = glGetUniformLocation(programId_, name.c_str());
    if (location >= 0) {
        glad_glUniform3fv(location, 1, &value[0]);
    }
}
