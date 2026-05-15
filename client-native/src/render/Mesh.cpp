#include "render/Mesh.hpp"

namespace render {

Mesh::~Mesh() { destroy(); }

Mesh::Mesh(Mesh&& other) noexcept
    : vboPos_(other.vboPos_), vboColor_(other.vboColor_),
      triVao_(other.triVao_), triEbo_(other.triEbo_), triCount_(other.triCount_),
      lineVao_(other.lineVao_), lineEbo_(other.lineEbo_), lineCount_(other.lineCount_) {
  other.vboPos_ = other.vboColor_ = 0;
  other.triVao_ = other.triEbo_ = 0;
  other.lineVao_ = other.lineEbo_ = 0;
  other.triCount_ = other.lineCount_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
  if (this != &other) {
    destroy();
    vboPos_   = other.vboPos_;   vboColor_ = other.vboColor_;
    triVao_   = other.triVao_;   triEbo_   = other.triEbo_;    triCount_  = other.triCount_;
    lineVao_  = other.lineVao_;  lineEbo_  = other.lineEbo_;   lineCount_ = other.lineCount_;
    other.vboPos_ = other.vboColor_ = 0;
    other.triVao_ = other.triEbo_ = 0;
    other.lineVao_ = other.lineEbo_ = 0;
    other.triCount_ = other.lineCount_ = 0;
  }
  return *this;
}

void Mesh::destroy() {
  if (lineEbo_)  glDeleteBuffers(1, &lineEbo_);
  if (lineVao_)  glDeleteVertexArrays(1, &lineVao_);
  if (triEbo_)   glDeleteBuffers(1, &triEbo_);
  if (triVao_)   glDeleteVertexArrays(1, &triVao_);
  if (vboColor_) glDeleteBuffers(1, &vboColor_);
  if (vboPos_)   glDeleteBuffers(1, &vboPos_);
  vboPos_ = vboColor_ = 0;
  triVao_ = triEbo_   = 0;
  lineVao_ = lineEbo_ = 0;
  triCount_ = lineCount_ = 0;
}

void Mesh::upload(std::span<const float>    positions,
                  std::span<const float>    colors,
                  std::span<const uint32_t> triangleIndices,
                  std::span<const uint32_t> lineIndices) {
  destroy();

  // ---- Shared vertex buffers ---------------------------------------------
  glCreateBuffers(1, &vboPos_);
  glCreateBuffers(1, &vboColor_);
  glNamedBufferStorage(vboPos_, static_cast<GLsizeiptr>(positions.size_bytes()),
                       positions.data(), 0);
  glNamedBufferStorage(vboColor_, static_cast<GLsizeiptr>(colors.size_bytes()),
                       colors.data(), 0);

  // ---- Triangle VAO + EBO -------------------------------------------------
  glCreateVertexArrays(1, &triVao_);
  glCreateBuffers(1, &triEbo_);
  glNamedBufferStorage(triEbo_,
                       static_cast<GLsizeiptr>(triangleIndices.size_bytes()),
                       triangleIndices.data(), 0);

  // Attribute 0 = position (vec3)
  glVertexArrayVertexBuffer(triVao_, 0, vboPos_, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(triVao_, 0);
  glVertexArrayAttribFormat(triVao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(triVao_, 0, 0);
  // Attribute 1 = color (vec4)
  glVertexArrayVertexBuffer(triVao_, 1, vboColor_, 0, sizeof(float) * 4);
  glEnableVertexArrayAttrib(triVao_, 1);
  glVertexArrayAttribFormat(triVao_, 1, 4, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(triVao_, 1, 1);

  glVertexArrayElementBuffer(triVao_, triEbo_);
  triCount_ = static_cast<GLsizei>(triangleIndices.size());

  // ---- Optional line VAO + EBO -------------------------------------------
  // Shares vboPos_ — no vertex data duplicated. Only position is bound.
  if (!lineIndices.empty()) {
    glCreateVertexArrays(1, &lineVao_);
    glCreateBuffers(1, &lineEbo_);
    glNamedBufferStorage(lineEbo_,
                         static_cast<GLsizeiptr>(lineIndices.size_bytes()),
                         lineIndices.data(), 0);

    glVertexArrayVertexBuffer(lineVao_, 0, vboPos_, 0, sizeof(float) * 3);
    glEnableVertexArrayAttrib(lineVao_, 0);
    glVertexArrayAttribFormat(lineVao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(lineVao_, 0, 0);
    glVertexArrayElementBuffer(lineVao_, lineEbo_);

    lineCount_ = static_cast<GLsizei>(lineIndices.size());
  }
}

void Mesh::draw() const {
  if (!triVao_ || triCount_ == 0) return;
  glBindVertexArray(triVao_);
  glDrawElements(GL_TRIANGLES, triCount_, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void Mesh::drawLines() const {
  if (!lineVao_ || lineCount_ == 0) return;
  glBindVertexArray(lineVao_);
  glDrawElements(GL_LINES, lineCount_, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

}  // namespace render
