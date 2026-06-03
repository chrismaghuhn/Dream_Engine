#include "engine/Engine.hpp"

#include <cstdlib>

int main() {
    engine::Engine engine;
    if (!engine.startup()) {
        return EXIT_FAILURE;
    }

    engine.run();
    engine.shutdown();
    return EXIT_SUCCESS;
}
