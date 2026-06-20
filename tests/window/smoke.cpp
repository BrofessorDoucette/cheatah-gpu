// smoke.cpp — validate the window renderer in isolation (before the cheatah stage): open the window,
// draw a couple seconds of the Slang plasma, close. Calls the same `cheatah::window` API the .purr
// test does. Built only by scripts/window_test.sh.
#include "window.hpp"

#include <cstdio>

int main() {
    long long w = cheatah::window::open(640, 480);
    if (!w) {
        std::fprintf(stderr, "smoke: window open failed\n");
        return 1;
    }
    int frames = 0;
    for (int i = 0; i < 120; ++i) {
        if (!cheatah::window::draw(w, static_cast<double>(i) * 0.03)) {
            break;
        }
        ++frames;
    }
    cheatah::window::close(w);
    std::printf("smoke: drew %d frames of the plasma\n", frames);
    return frames > 0 ? 0 : 2;
}
