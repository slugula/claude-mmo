#include "render/GlDebug.hpp"

#include <glad/glad.h>

#include <cstdio>

namespace render {

namespace {

const char* sourceStr(GLenum source) {
  switch (source) {
    case GL_DEBUG_SOURCE_API:             return "API";
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   return "WINDOW";
    case GL_DEBUG_SOURCE_SHADER_COMPILER: return "SHADER";
    case GL_DEBUG_SOURCE_THIRD_PARTY:     return "THIRD_PARTY";
    case GL_DEBUG_SOURCE_APPLICATION:     return "APP";
    case GL_DEBUG_SOURCE_OTHER:           return "OTHER";
    default:                              return "?";
  }
}

const char* typeStr(GLenum type) {
  switch (type) {
    case GL_DEBUG_TYPE_ERROR:               return "ERROR";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "DEPRECATED";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  return "UNDEFINED";
    case GL_DEBUG_TYPE_PORTABILITY:         return "PORTABILITY";
    case GL_DEBUG_TYPE_PERFORMANCE:         return "PERF";
    case GL_DEBUG_TYPE_MARKER:              return "MARKER";
    case GL_DEBUG_TYPE_PUSH_GROUP:          return "PUSH";
    case GL_DEBUG_TYPE_POP_GROUP:           return "POP";
    case GL_DEBUG_TYPE_OTHER:               return "OTHER";
    default:                                return "?";
  }
}

const char* severityStr(GLenum severity) {
  switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:         return "HIGH";
    case GL_DEBUG_SEVERITY_MEDIUM:       return "MED";
    case GL_DEBUG_SEVERITY_LOW:          return "LOW";
    case GL_DEBUG_SEVERITY_NOTIFICATION: return "INFO";
    default:                             return "?";
  }
}

void GLAPIENTRY debugCallback(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei /*length*/, const GLchar* message, const void* /*userParam*/) {
  // Filter out the spammy "buffer info" notifications
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
  std::fprintf(stderr, "[GL %s/%s/%s id=%u] %s\n",
               sourceStr(source), typeStr(type), severityStr(severity), id, message);
}

}  // namespace

void installGlDebugCallback() {
  // KHR_debug is core in OpenGL 4.3+. We request a 4.6 Core context, so this
  // is always available. Guard once in case the context downgraded silently.
  if (!GLAD_GL_VERSION_4_3) return;
  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(debugCallback, nullptr);
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
}

}  // namespace render
