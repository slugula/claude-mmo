#include "app/App.hpp"

#include <cstdio>

int main(int /*argc*/, char** /*argv*/) {
  app::App app;
  if (!app.init()) {
    std::fprintf(stderr, "App init failed\n");
    return 1;
  }
  return app.run();
}
