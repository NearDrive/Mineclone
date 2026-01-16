#ifndef MINECLONE_SHADER_H
#define MINECLONE_SHADER_H

#include <string>

#include <glad/glad.h>
#include <glm/mat4x4.hpp>

class Shader {
public:
    Shader();
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath, std::string& errorOut);
    void use() const;
    void setMat4(const std::string& name, const glm::mat4& value) const;
    GLuint id() const { return programId_; }

private:
    GLuint programId_ = 0;
};

#endif
