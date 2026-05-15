#pragma once

#include <glad/glad.h>

#include <cstdint>
#include <span>

namespace render {

// Owns a position VBO + a color VBO + two index buffers:
//   - triangle indices, drawn via draw()      (GL_TRIANGLES)
//   - optional line indices, drawn via drawLines() (GL_LINES)
//
// Two separate VAOs share the same underlying VBOs, so toggling between
// filled and wireframe rendering touches zero CPU-side memory and avoids
// any "swap the EBO bound to a VAO" side effects.
class Mesh {
public:
  Mesh() = default;
  ~Mesh();

  Mesh(const Mesh&)            = delete;
  Mesh& operator=(const Mesh&) = delete;
  Mesh(Mesh&&) noexcept;
  Mesh& operator=(Mesh&&) noexcept;

  // Upload geometry to GPU. positions: 3 floats per vertex; colors: 4 floats.
  // triangleIndices: uint32 triangle list (required).
  // lineIndices:     uint32 line list (optional; empty span = no wireframe).
  void upload(std::span<const float>    positions,
              std::span<const float>    colors,
              std::span<const uint32_t> triangleIndices,
              std::span<const uint32_t> lineIndices = {});

  void draw()      const;  // GL_TRIANGLES
  void drawLines() const;  // GL_LINES (no-op if upload had no line indices)

  GLsizei triangleCount() const { return triCount_;  }
  GLsizei lineCount()     const { return lineCount_; }
  bool    isValid()       const { return triVao_ != 0; }
  bool    hasLines()      const { return lineVao_ != 0 && lineCount_ > 0; }

private:
  void destroy();

  // Shared vertex buffers
  GLuint  vboPos_      = 0;
  GLuint  vboColor_    = 0;

  // Triangle path
  GLuint  triVao_      = 0;
  GLuint  triEbo_      = 0;
  GLsizei triCount_    = 0;

  // Line path (optional)
  GLuint  lineVao_     = 0;
  GLuint  lineEbo_     = 0;
  GLsizei lineCount_   = 0;
};

}  // namespace render
