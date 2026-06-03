#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <string>

namespace render {

// Compiled & linked GL program. Construct via fromFiles(); the returned object
// is in a valid (use()-able) state only if isValid() returns true.
class Shader {
public:
  Shader() = default;
  ~Shader();

  Shader(const Shader&)            = delete;
  Shader& operator=(const Shader&) = delete;
  Shader(Shader&&) noexcept;
  Shader& operator=(Shader&&) noexcept;

  // Load + compile + link from two GLSL source files. Returns false on error
  // (logs the compile/link errors to stderr).
  bool fromFiles(const std::filesystem::path& vert, const std::filesystem::path& frag);

  // Load + compile + link a single compute shader (GL 4.3+). Returns false on error.
  bool fromCompute(const std::filesystem::path& comp);

  bool isValid() const { return program_ != 0; }
  GLuint id()    const { return program_; }
  void use()    const { glUseProgram(program_); }

  // Uniform setters
  void setMat4 (const char* name, const glm::mat4& v);
  void setVec4 (const char* name, const glm::vec4& v);
  void setVec3 (const char* name, const glm::vec3& v);
  void setVec2 (const char* name, const glm::vec2& v);
  void setFloat(const char* name, float v);
  void setInt  (const char* name, int v);

private:
  void destroy();
  GLuint program_ = 0;
};

}  // namespace render
