#include "render/Shader.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace render {

namespace {

std::string readFile(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "[Shader] cannot open %s\n", p.string().c_str());
    return {};
  }
  std::stringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

GLuint compileStage(GLenum stage, const std::string& src, const char* name) {
  GLuint sh = glCreateShader(stage);
  const char* csrc = src.c_str();
  glShaderSource(sh, 1, &csrc, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
    std::string log(static_cast<size_t>(len), '\0');
    glGetShaderInfoLog(sh, len, nullptr, log.data());
    std::fprintf(stderr, "[Shader] %s compile failed:\n%s\n", name, log.c_str());
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

}  // namespace

Shader::~Shader() { destroy(); }

Shader::Shader(Shader&& other) noexcept : program_(other.program_) {
  other.program_ = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
  if (this != &other) {
    destroy();
    program_ = other.program_;
    other.program_ = 0;
  }
  return *this;
}

void Shader::destroy() {
  if (program_) {
    glDeleteProgram(program_);
    program_ = 0;
  }
}

bool Shader::fromFiles(const std::filesystem::path& vert, const std::filesystem::path& frag) {
  destroy();

  const std::string vsrc = readFile(vert);
  const std::string fsrc = readFile(frag);
  if (vsrc.empty() || fsrc.empty()) return false;

  GLuint vs = compileStage(GL_VERTEX_SHADER,   vsrc, vert.filename().string().c_str());
  if (!vs) return false;
  GLuint fs = compileStage(GL_FRAGMENT_SHADER, fsrc, frag.filename().string().c_str());
  if (!fs) { glDeleteShader(vs); return false; }

  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
    std::string log(static_cast<size_t>(len), '\0');
    glGetProgramInfoLog(program_, len, nullptr, log.data());
    std::fprintf(stderr, "[Shader] link failed:\n%s\n", log.c_str());
    destroy();
    return false;
  }
  return true;
}

void Shader::setMat4(const char* name, const glm::mat4& v) {
  GLint loc = glGetUniformLocation(program_, name);
  if (loc < 0) return;
  glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(v));
}

void Shader::setVec3(const char* name, const glm::vec3& v) {
  GLint loc = glGetUniformLocation(program_, name);
  if (loc < 0) return;
  glUniform3fv(loc, 1, glm::value_ptr(v));
}

void Shader::setVec2(const char* name, const glm::vec2& v) {
  GLint loc = glGetUniformLocation(program_, name);
  if (loc < 0) return;
  glUniform2fv(loc, 1, glm::value_ptr(v));
}

void Shader::setFloat(const char* name, float v) {
  GLint loc = glGetUniformLocation(program_, name);
  if (loc < 0) return;
  glUniform1f(loc, v);
}

void Shader::setInt(const char* name, int v) {
  GLint loc = glGetUniformLocation(program_, name);
  if (loc < 0) return;
  glUniform1i(loc, v);
}

}  // namespace render
