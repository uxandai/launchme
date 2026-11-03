#include <algorithm>
#include "../src/model/game_fps.hpp"
#include "../src/ui/quick_menu.hpp"
#include "../src/ui/guide_motion.hpp"
#include <iostream>
#include <limits>

int main() {
    using namespace game_fps;
    const auto near = [](double a, double b) { return std::abs(a - b) < 0.01; };
    History history;
    if (history.read(1).status != Status::collecting) return 1;
    for (int i = 0; i <= 600; ++i) history.present(1, 10 + i / 60.0);
    auto result = history.read(20);
    if (result.status != Status::ready || !near(result.fps, 60) || !near(result.low, 60)) return 2;
    // A second swapchain must not double the application's FPS.
    for (int i = 0; i <= 150; ++i) history.present(2, 10 + i / 15.0);
    result = history.read(20);
    if (!near(result.fps, 60)) return 3;
    if (history.read(24).status != Status::collecting) return 4;
    history.clear();
    // 199 x 10 ms plus a 100 ms hitch: 1% low averages the TWO slowest
    // intervals (100 and 10 ms), so 18.18 FPS, not 10 FPS or a percentile.
    double time = 1;
    history.present(7, time);
    for (int i = 0; i < 200; ++i) { time += i == 50 ? 0.1 : 0.01; history.present(7, time); }
    result = history.read(time);
    if (!near(result.fps, 100) || !near(result.low, 2.0 / 0.11)) return 5;
    // The curve: 96 slices over six seconds; the hitch lifts its slice (a mean of 62 ms of frames) and the steady parts read 10 ms.
    if (result.graph.size() != 96) return 20;
    const auto peak = *std::max_element(result.graph.begin(), result.graph.end());
    if (peak < 20 || peak > 101 || result.graph.back() < 9.9f || result.graph.back() > 10.1f) return 21;
    history.present(7, time); history.present(7, time - 1);
    history.present(7, std::numeric_limits<double>::quiet_NaN());
    if (history.read(time) != result) return 6;
    // The hitch ages out; the history and 1% low are bounded to 60 seconds.
    for (int i = 0; i < 6500; ++i) { time += 0.01; history.present(7, time); }
    result = history.read(time);
    if (!near(result.fps, 100) || !near(result.low, 100)) return 7;
    history.clear();
    if (history.read(time).fps || history.read(time).low) return 8;
    for (int i = 0; i < 10; ++i) history.present(7, time + i / 60.0);
    if (history.read(time + 0.15).low) return 9;
    // Over a game the bar is Start + Close (a group of their own), then Kontrolery, Dźwięk, Widgety, Ustawienia and
    // Zasilanie: no search, library, store or downloads. The launcher's own bar keeps its order.
    for (int plugins = 0; plugins < 5; ++plugins) {
        using A = quick_menu::Action;
        const A expected[] = {A::dashboard, A::close_game, A::controllers, A::audio, A::widgets, A::settings, A::power};
        if (quick_menu::count(plugins, true) != 7) return 10;
        for (int i = 0; i < 7; ++i)
            if (quick_menu::action(i, plugins, true) != expected[i]) return 11;
        const auto home = quick_menu::center(1600, 0, plugins, true), close = quick_menu::center(1600, 1, plugins, true), next = quick_menu::center(1600, 2, plugins, true);
        if (!near(close - home, 116) || !near(next - close, 148) ||
            quick_menu::hit(1600, 900, (close + next) * 0.5f, 835, plugins, true) != -1 ||
            quick_menu::hit(1600, 900, close, 835, plugins, true) != 1) return 12;
    }
    GuideMotion motion;
    motion.open(400);
    if (!motion.hidden()) return 13;
    motion.step(0.016f, false, false, 400);
    if (motion.bar <= 0 || motion.bar >= 1 || motion.drawer != 0) return 14;
    for (int i = 0; i < 30; ++i) motion.step(0.016f, false, false, 400);
    if (motion.bar != 1 || motion.opacity != 1) return 15;
    for (int i = 0; i < 30; ++i) motion.step(0.016f, false, true, 516);
    if (motion.drawer != 1 || motion.bar != 0 || !motion.circle.settled(516)) return 16;
    for (int i = 0; i < 20; ++i) motion.step(0.016f, true, false, 516);
    if (!motion.hidden()) return 17;
    std::cout << "Game FPS cadence, swapchain isolation, 1% low, stale samples and overlay bar passed\n";
}
