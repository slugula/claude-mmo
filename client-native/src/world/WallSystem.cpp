#include "world/WallSystem.hpp"

#include <cmath>
#include <cstddef>

namespace world {

namespace {
constexpr float kPi4 = 0.78539816339f;   // 45° in radians
constexpr float kWallH = 1.5f;           // ~1.5× player height (tunable)

// Average of a tile's 4 corner vertex heights → tile-centre world Y.
float tileCenterY(const std::vector<float>& vh, int W, int H, int tx, int ty) {
  const float SW = vh[(H - ty)     * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float SE = vh[(H - ty)     * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  const float NW = vh[(H - ty - 1) * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float NE = vh[(H - ty - 1) * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  return (SW + SE + NW + NE) * 0.25f;
}

// Axis-aligned box (min..max) as 6 quads with outward normals.
void makeBox(glm::vec3 lo, glm::vec3 hi,
             std::vector<float>& pos, std::vector<float>& nrm,
             std::vector<uint32_t>& idx) {
  pos.clear(); nrm.clear(); idx.clear();
  auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
    const uint32_t base = static_cast<uint32_t>(pos.size() / 3);
    for (const glm::vec3& p : {a, b, c, d}) {
      pos.insert(pos.end(), { p.x, p.y, p.z });
      nrm.insert(nrm.end(), { n.x, n.y, n.z });
    }
    idx.insert(idx.end(), { base, base+1, base+2, base, base+2, base+3 });
  };
  face({lo.x,lo.y,lo.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,hi.z},{lo.x,hi.y,lo.z},{-1,0,0});
  face({hi.x,lo.y,hi.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{ 1,0,0});
  face({hi.x,lo.y,lo.z},{lo.x,lo.y,lo.z},{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{ 0,0,-1});
  face({lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z},{ 0,0, 1});
  face({lo.x,hi.y,hi.z},{hi.x,hi.y,hi.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z},{ 0,1,0});
  face({lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{ 0,-1,0});
}
}  // namespace

WallSystem::~WallSystem() { destroy(); }

void WallSystem::initGL() {
  std::vector<float> pos, nrm; std::vector<uint32_t> idx;
  // Cardinal wall: hugs the +Z edge, ~0.2 thick, full tile width, tall.
  makeBox({-0.5f, 0.f, 0.30f}, {0.5f, kWallH, 0.50f}, pos, nrm, idx);
  buildKit(cardinal_, pos, nrm, idx, glm::vec3(0.62f, 0.62f, 0.64f));
  // Diagonal wall: centred, spans corner-to-corner (length √2), ~0.2 thick.
  makeBox({-0.7071f, 0.f, -0.10f}, {0.7071f, kWallH, 0.10f}, pos, nrm, idx);
  buildKit(diagonal_, pos, nrm, idx, glm::vec3(0.62f, 0.62f, 0.64f));
  // Pillar: a column at the +X+Z corner.
  makeBox({0.34f, 0.f, 0.34f}, {0.50f, kWallH, 0.50f}, pos, nrm, idx);
  buildKit(pillar_, pos, nrm, idx, glm::vec3(0.56f, 0.55f, 0.50f));
}

void WallSystem::buildKit(Kit& k, const std::vector<float>& pos,
                          const std::vector<float>& nrm,
                          const std::vector<uint32_t>& idx, glm::vec3 color) {
  k.color      = color;
  k.indexCount = static_cast<GLsizei>(idx.size());

  const std::size_t vcount = pos.size() / 3;
  std::vector<float> col(vcount * 4, 1.0f);   // white per-vertex; u_color tints

  glCreateBuffers(1, &k.vboPos);
  glCreateBuffers(1, &k.vboNrm);
  glCreateBuffers(1, &k.vboCol);
  glCreateBuffers(1, &k.ebo);
  glNamedBufferStorage(k.vboPos, (GLsizeiptr)(pos.size()*sizeof(float)), pos.data(), 0);
  glNamedBufferStorage(k.vboNrm, (GLsizeiptr)(nrm.size()*sizeof(float)), nrm.data(), 0);
  glNamedBufferStorage(k.vboCol, (GLsizeiptr)(col.size()*sizeof(float)), col.data(), 0);
  glNamedBufferStorage(k.ebo,    (GLsizeiptr)(idx.size()*sizeof(uint32_t)), idx.data(), 0);

  glCreateBuffers(1, &k.instVbo);   // grown on demand in uploadInstances

  glCreateVertexArrays(1, &k.vao);
  glVertexArrayVertexBuffer (k.vao, 0, k.vboPos, 0, sizeof(float)*3);
  glEnableVertexArrayAttrib (k.vao, 0);
  glVertexArrayAttribFormat (k.vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(k.vao, 0, 0);
  glVertexArrayVertexBuffer (k.vao, 1, k.vboNrm, 0, sizeof(float)*3);
  glEnableVertexArrayAttrib (k.vao, 1);
  glVertexArrayAttribFormat (k.vao, 1, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(k.vao, 1, 1);
  glVertexArrayVertexBuffer (k.vao, 4, k.vboCol, 0, sizeof(float)*4);
  glEnableVertexArrayAttrib (k.vao, 4);
  glVertexArrayAttribFormat (k.vao, 4, 4, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(k.vao, 4, 4);
  glVertexArrayElementBuffer(k.vao, k.ebo);
}

void WallSystem::uploadInstances(Kit& k) {
  if (k.insts.empty() || !k.instVbo) return;
  const GLsizei n = static_cast<GLsizei>(k.insts.size());
  if (n > k.instCap) {
    // Grow: recreate the immutable-less buffer with DYNAMIC data store.
    glNamedBufferData(k.instVbo, (GLsizeiptr)(n * sizeof(Instance)),
                      k.insts.data(), GL_DYNAMIC_DRAW);
    k.instCap = n;
    // (Re)point the per-instance attributes at the (possibly new) store.
    glVertexArrayVertexBuffer  (k.vao, 2, k.instVbo, 0, sizeof(Instance));
    glVertexArrayBindingDivisor(k.vao, 2, 1);
    glEnableVertexArrayAttrib  (k.vao, 2);
    glVertexArrayAttribFormat  (k.vao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(Instance, x));
    glVertexArrayAttribBinding (k.vao, 2, 2);
    glEnableVertexArrayAttrib  (k.vao, 3);
    glVertexArrayAttribFormat  (k.vao, 3, 1, GL_FLOAT, GL_FALSE, offsetof(Instance, rotY));
    glVertexArrayAttribBinding (k.vao, 3, 2);
  } else {
    glNamedBufferSubData(k.instVbo, 0, (GLsizeiptr)(n * sizeof(Instance)), k.insts.data());
  }
}

void WallSystem::rebuildFromMap(const shared::WorldMapFile& map) {
  cardinal_.insts.clear();
  diagonal_.insts.clear();
  pillar_.insts.clear();

  const int W = map.width, H = map.height;
  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return;

  for (const auto& w : map.walls) {
    if (w.tileX < 0 || w.tileX >= W || w.tileY < 0 || w.tileY >= H) continue;
    const float cy   = tileCenterY(vh, W, H, w.tileX, w.tileY);
    const float rotY = static_cast<float>(w.orient & 7) * kPi4;
    const Instance inst{ static_cast<float>(w.tileX), cy,
                         static_cast<float>(w.tileY), rotY };
    if      (w.pillar)          pillar_.insts.push_back(inst);
    else if ((w.orient & 1)==0) cardinal_.insts.push_back(inst);  // cardinal edge
    else                        diagonal_.insts.push_back(inst);  // diagonal span
  }

  uploadInstances(cardinal_);
  uploadInstances(diagonal_);
  uploadInstances(pillar_);
}

void WallSystem::drawKit(render::Shader& shader, Kit& k) {
  if (k.insts.empty()) return;
  shader.setVec3("u_color", k.color);
  glBindVertexArray(k.vao);
  glDrawElementsInstanced(GL_TRIANGLES, k.indexCount, GL_UNSIGNED_INT, nullptr,
                          static_cast<GLsizei>(k.insts.size()));
  glBindVertexArray(0);
}

void WallSystem::render(render::Shader& obstacleShader) {
  drawKit(obstacleShader, cardinal_);
  drawKit(obstacleShader, diagonal_);
  drawKit(obstacleShader, pillar_);
}

void WallSystem::destroyKit(Kit& k) {
  if (k.vao)     glDeleteVertexArrays(1, &k.vao);
  if (k.instVbo) glDeleteBuffers(1, &k.instVbo);
  if (k.ebo)     glDeleteBuffers(1, &k.ebo);
  if (k.vboCol)  glDeleteBuffers(1, &k.vboCol);
  if (k.vboNrm)  glDeleteBuffers(1, &k.vboNrm);
  if (k.vboPos)  glDeleteBuffers(1, &k.vboPos);
  k = {};
}

void WallSystem::destroy() {
  destroyKit(cardinal_);
  destroyKit(diagonal_);
  destroyKit(pillar_);
}

}  // namespace world
