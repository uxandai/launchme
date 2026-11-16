// Tryb handhelda's first-start detection (src/platform/handheld.cpp): the firmware names of known
// handhelds, and ordinary PCs left alone.
#include "../src/platform/handheld.hpp"
#include <cstdio>

int main() {
    struct Case { const char* vendor; const char* product; const char* family; bool handheld; };
    const Case cases[] = {
        {"Valve", "Jupiter", "Aerith", true},                       // Steam Deck LCD
        {"Valve", "Galileo", "Sephiroth", true},                    // Steam Deck OLED
        {"ASUSTeK COMPUTER INC.", "ROG Ally RC71L_RC71L", "ROG Ally", true},
        {"ASUSTeK COMPUTER INC.", "ROG Ally X RC72LA_RC72LA", "ROG Ally X", true},
        {"LENOVO", "83E1", "Legion Go 8APU1", true},
        {"Micro-Star International Co., Ltd.", "Claw A1M", "Claw", true},
        {"AYANEO", "AYANEO 2S", "", true},
        {"GPD", "G1618-04", "", true},
        {"ONE-NETBOOK", "ONEXPLAYER X1 i", "", true},
        {"Valve", "Steam Machine", "", false},
        {"ASUSTeK COMPUTER INC.", "ROG STRIX G614JV", "ROG Strix", false},
        {"LENOVO", "82RB", "Legion 5 15ARH7H", false},
        {"Micro-Star International Co., Ltd.", "MS-7D75", "Default string", false},
        {"", "", "", false},
    };
    int failed = 0;
    for (const auto& c : cases)
        if (handheld::knownModel(c.vendor, c.product, c.family) != c.handheld) {
            std::fprintf(stderr, "wrong: %s / %s / %s\n", c.vendor, c.product, c.family);
            ++failed;
        }
    return failed;
}
