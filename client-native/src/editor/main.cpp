#include "editor/EditorApp.hpp"

int main() {
  editor::EditorApp app;
  if (!app.init()) return 1;
  return app.run();
}
