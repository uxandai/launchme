#include "../src/library/hardware_rating.hpp"
#include "../src/library/system_requirements.hpp"

#include <cmath>
#include <iostream>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAILED: " << what << '\n'; ++failures; }
}
using sysreq::Level;

// Cyberpunk 2077 as store.steampowered.com/api/appdetails answers (shortened).
constexpr const char* cyberpunk = R"({"1091500":{"success":true,"data":{"name":"Cyberpunk 2077","pc_requirements":{
"minimum":"<strong>Minimum:</strong><br><ul class=\"bb_ul\"><li>Requires a 64-bit processor and operating system<br></li><li><strong>OS:</strong> 64-bit Windows 10<br></li><li><strong>Processor:</strong> Core i7-6700 or Ryzen 5 1600<br></li><li><strong>Memory:</strong> 12 GB RAM<br></li><li><strong>Graphics:</strong> GeForce GTX 1060 6GB or Radeon RX 580 8GB or Arc A380<br></li><li><strong>DirectX:</strong> Version 12<br></li><li><strong>Storage:</strong> 70 GB available space<br></li><li><strong>Additional Notes:</strong> SSD required.</li></ul>",
"recommended":"<strong>Recommended:</strong><br><ul class=\"bb_ul\"><li><strong>OS:</strong> 64-bit Windows 10<br></li><li><strong>Processor:</strong> Core i7-12700 or Ryzen 7 7800X3D<br></li><li><strong>Memory:</strong> 16 GB RAM<br></li><li><strong>Graphics:</strong> GeForce RTX 2060 SUPER or Radeon RX 5700 XT or Arc A770<br></li><li><strong>Storage:</strong> 70 GB available space</li></ul>"}}}})";
// Counter-Strike 2: a card "with 1 GB" and no model, a minimum only.
constexpr const char* counter_strike = R"({"730":{"success":true,"data":{"pc_requirements":{"minimum":"<strong>Minimum:</strong><br><ul class=\"bb_ul\"><li><strong>OS:</strong> Windows&reg; 10<br></li><li><strong>Processor:</strong> 4 hardware CPU threads - Intel&reg; Core&trade; i5 750 or higher<br></li><li><strong>Memory:</strong> 8 GB RAM<br></li><li><strong>Graphics:</strong> Video card must be 1 GB or more and should be a DirectX 11-compatible with support for Shader Model 5.0<br></li><li><strong>Storage:</strong> 85 GB available space</li></ul>"}}}})";
// Stardew Valley: only a clock speed and video memory.
constexpr const char* stardew = R"({"413150":{"success":true,"data":{"pc_requirements":{"minimum":"<strong>Minimum:</strong><br><ul class=\"bb_ul\"><li><strong>OS *:</strong> Windows Vista or greater<br></li><li><strong>Processor:</strong> 2 Ghz<br></li><li><strong>Memory:</strong> 2 GB RAM<br></li><li><strong>Graphics:</strong> 256 mb video memory, shader model 3.0+<br></li><li><strong>Storage:</strong> 500 MB available space</li></ul>"}}}})";
// Terraria: the labels sit inside the <strong>.
constexpr const char* terraria = R"({"105600":{"success":true,"data":{"pc_requirements":{"minimum":"<h2 class=\"bb_tag\" ><strong>REQUIRED</strong></h2><ul class=\"bb_ul\"><li><strong>OS: Windows Xp, Vista, 7</strong> <br> </li><li><strong>Processor: 2.0 Ghz</strong> <br> </li><li><strong>Memory: 2.5GB</strong><br> </li><li><strong>Hard Disk Space: 200MB </strong> <br> </li><li><strong>Video Card: 128mb Video Memory, capable of Shader Model 2.0+</strong> <br> </li></ul>","recommended":"<strong>Memory: 4GB</strong><br>"}}}})";
constexpr const char* polish = R"({"292030":{"success":true,"data":{"pc_requirements":{"minimum":"<strong>Minimalne:</strong><br><ul class=\"bb_ul\"><li><strong>System operacyjny:</strong> 64-bit Windows 11<br></li><li><strong>Procesor:</strong> Core i5-8400 / Ryzen 5 2600<br></li><li><strong>Pamięć:</strong> 12 GB RAM<br></li><li><strong>Grafika:</strong> GeForce GTX 1660 / Radeon RX 5500 XT 8GB / Arc A580<br></li><li><strong>Miejsce na dysku:</strong> 60 SSD GB available space</li></ul>"}}}})";
constexpr const char* none = R"({"1":{"success":true,"data":{"pc_requirements":[]}}})";
// GOG's v2 answer for the Windows build.
constexpr const char* gog = R"({"_embedded":{"supportedOperatingSystems":[
{"operatingSystem":{"name":"linux"},"systemRequirements":[{"type":"minimum","description":"Ubuntu","requirements":[{"id":"memory","name":"Memory:","description":"4 GB RAM"}]}]},
{"operatingSystem":{"name":"windows"},"systemRequirements":[
{"type":"minimum","description":"x","requirements":[{"id":"system","name":"System:","description":"Windows 10"},{"id":"processor","name":"Processor:","description":"Intel Core 2 Duo 2.2 GHz or similar AMD"},{"id":"memory","name":"Memory:","description":"2 GB RAM"},{"id":"graphics","name":"Graphics:","description":"Nvidia GF 8800 512 VR or ATI Radeon HD 3850 512 MB"},{"id":"storage","name":"Storage:","description":"23 GB HDD space"}]},
{"type":"recommended","description":"Processor: Core i5-2500<br>\nMemory: 4 GB RAM<br>\nGraphics: GeForce GTX 560 Ti","requirements":[{"id":"processor","name":"Processor:","description":""}]}]}]}})";
constexpr const char* gog_empty = R"({"_embedded":{"supportedOperatingSystems":[{"operatingSystem":{"name":"windows"},"systemRequirements":[]}]}})";

sysreq::Machine machine(const wchar_t* cpu, const wchar_t* gpu, uint32_t ram_mb, uint32_t vram_mb, uint64_t free_mb = 500 * 1024) {
    sysreq::Machine value;
    value.cpu = cpu; value.gpu = gpu; value.ram_mb = ram_mb; value.vram_mb = vram_mb; value.free_mb = free_mb;
    return value;
}
}

int main() {
    // Amounts.
    check(sysreq::megabytesIn(L"12 GB RAM") == 12288 && sysreq::megabytesIn(L"512 MB RAM") == 512 && sysreq::megabytesIn(L"2.5GB") == 2560, "amounts");
    check(sysreq::megabytesIn(L"60 SSD GB available space") == 61440 && sysreq::megabytesIn(L"1,5 GB") == 1536 && sysreq::megabytesIn(L"1,024 MB") == 1024, "odd spellings");
    check(!sysreq::megabytesIn(L"4 hardware CPU threads") && !sysreq::megabytesIn(L"2 Ghz") && !sysreq::megabytesIn(L""), "no amount");

    // Steam's HTML.
    const auto needs = sysreq::fromSteam(1091500, cyberpunk);
    check(needs.minimum && needs.recommended, "both columns");
    check(needs.minimum->ram_mb == 12288 && needs.recommended->ram_mb == 16384 && needs.minimum->storage_mb == 70 * 1024, "RAM and disk read");
    check(needs.minimum->cpu == L"Core i7-6700 or Ryzen 5 1600" && needs.recommended->gpu.find(L"Arc A770") != std::wstring::npos, "CPU and GPU text");
    check(!sysreq::fromSteam(1, none).any() && !sysreq::fromSteam(2, cyberpunk).any() && !sysreq::fromSteam(1, "not json").any(), "nothing to read");
    const auto tera = sysreq::fromSteam(105600, terraria);
    check(tera.minimum && tera.minimum->ram_mb == 2560 && tera.minimum->storage_mb == 200 && tera.minimum->gpu.find(L"128mb") != std::wstring::npos, "labels inside the bold tag");
    check(tera.recommended && tera.recommended->ram_mb == 4096 && tera.recommended->cpu.empty(), "a partial recommended column");
    const auto pl = sysreq::fromSteam(292030, polish);
    check(pl.minimum && pl.minimum->ram_mb == 12288 && pl.minimum->storage_mb == 60 * 1024 && pl.minimum->cpu.find(L"Ryzen 5 2600") != std::wstring::npos &&
          pl.minimum->gpu.find(L"Arc A580") != std::wstring::npos, "Polish labels");

    // GOG.
    const auto old = sysreq::fromGog(gog);
    check(old.minimum && old.minimum->ram_mb == 2048 && old.minimum->storage_mb == 23 * 1024, "GOG minimum comes from the Windows entry");
    check(old.recommended && old.recommended->ram_mb == 4096 && old.recommended->cpu == L"Core i5-2500", "GOG recommended from its text");
    check(!sysreq::fromGog(gog_empty).any() && !sysreq::fromGog("{}").any(), "GOG without requirements");

    // Ratings: the order matters, not the numbers.
    const auto g = [](const wchar_t* name) { return hardware::gpuScore(name); };
    const auto c = [](const wchar_t* name) { return hardware::cpuScore(name); };
    check(g(L"NVIDIA GeForce GTX 1060 6GB") == 100.0f && g(L"GeForce GTX 1060 3 GB") == 90.0f, "GTX 1060 variants");
    check(g(L"NVIDIA GeForce RTX 3070") > g(L"AMD Radeon RX 6600 XT") && g(L"Radeon RX 580 8GB") > g(L"GeForce GTX 960") &&
          g(L"GeForce GTX 960") > g(L"Radeon HD 7770") && g(L"GeForce GTX 1050 Ti") > g(L"GeForce GTX 1050"), "GPU order");
    check(g(L"GeForce RTX 3060 Laptop GPU") < g(L"GeForce RTX 3060") && g(L"GeForce GTX1060") == 100.0f && g(L"RTX 3060Ti") == 245.0f, "laptop parts and glued names");
    check(!g(L"AMD Radeon(TM) Graphics") && !g(L"GeForce GTX 970M") && !g(L"Microsoft Basic Render Driver") && !g(L"1 GB or more"), "unknown GPUs stay unknown");
    check(c(L"AMD Ryzen 5 3600 6-Core Processor") > c(L"Intel(R) Core(TM) i7-6700 CPU @ 3.40GHz") && c(L"13th Gen Intel(R) Core(TM) i7-13700K") > c(L"AMD Ryzen 7 5800X3D") &&
          c(L"Intel Core i5-12400") > c(L"Core i5-8400") && c(L"Ryzen 7 7800X3D") > c(L"Ryzen 5 1600") && c(L"Intel Core 2 Duo") < c(L"Core i5 750"), "CPU order");
    check(c(L"Core i7-1185G7") > c(L"Core i3-3240") && !c(L"2 Ghz") && !c(L"Intel Core i5") && !c(L"Dual Core") && c(L"AMD FX-8350") > c(L"AMD FX-4300"), "CPU edge cases");

    // The verdict.
    const auto strong = sysreq::assess(machine(L"AMD Ryzen 9 7950X3D", L"NVIDIA GeForce RTX 4080", 32768, 16384), needs, false);
    check(strong.level == Level::good && strong.rows.size() == 4, "a strong PC runs Cyberpunk");
    const auto middle = sysreq::assess(machine(L"AMD Ryzen 5 3600 6-Core Processor", L"NVIDIA GeForce RTX 3070", 32768, 8192), needs, false);
    check(middle.level == Level::ok && middle.rows[0].level == Level::ok && middle.rows[1].level == Level::good, "a slower CPU makes it yellow");
    const auto weak = sysreq::assess(machine(L"Intel(R) Core(TM) i3-3240 CPU", L"NVIDIA GeForce GTX 750 Ti", 8192, 2048), needs, false);
    check(weak.level == Level::poor && weak.rows[2].level == Level::poor, "an old PC cannot");
    check(sysreq::assess(machine(L"AMD Ryzen 9 7950X3D", L"NVIDIA GeForce RTX 4080", 32768, 16384, 10 * 1024), needs, false).level == Level::poor, "no room for it on disk");
    check(sysreq::assess(machine(L"AMD Ryzen 9 7950X3D", L"NVIDIA GeForce RTX 4080", 32768, 16384, 10 * 1024), needs, true).level == Level::good, "an installed game needs no room");
    check(sysreq::assess(machine(L"AMD Ryzen 9 7950X3D", L"NVIDIA GeForce RTX 4080", 15900, 16384), needs, true).rows[2].yours == L"16 GB", "RAM rounds to the fitted amount");

    const auto cs = sysreq::fromSteam(730, counter_strike);
    const auto cs_verdict = sysreq::assess(machine(L"AMD Ryzen 5 3600", L"NVIDIA GeForce GTX 1660", 16384, 6144), cs, false);
    check(cs_verdict.level == Level::good && cs_verdict.rows[1].level == Level::good, "a card named only by its memory");
    check(sysreq::assess(machine(L"AMD Ryzen 5 3600", L"AMD Radeon(TM) Graphics", 16384, 512), cs, false).level == Level::poor, "512 MB is below 1 GB");

    // What cannot be read is grey, never a guess.
    const auto sdv = sysreq::fromSteam(413150, stardew);
    const auto sdv_verdict = sysreq::assess(machine(L"AMD Ryzen 5 3600", L"NVIDIA GeForce GTX 1660", 16384, 6144), sdv, false);
    check(sdv_verdict.level == Level::unknown && sdv_verdict.rows[0].level == Level::unknown && sdv_verdict.rows[2].level == Level::good, "a clock speed says nothing");
    check(sysreq::assess(machine(L"AMD Ryzen 5 3600", L"NVIDIA GeForce GTX 1660", 1024, 6144), sdv, false).level == Level::poor, "but too little RAM is certain");
    check(sysreq::assess(machine(L"", L"", 0, 0), needs, false).level == Level::unknown, "an unknown PC");
    check(sysreq::assess(machine(L"AMD Ryzen 5 3600", L"NVIDIA GeForce RTX 3070", 16384, 8192), {}, false).level == Level::unknown, "no requirements");
    const auto gog_verdict = sysreq::assess(machine(L"AMD Ryzen 5 3600", L"NVIDIA GeForce RTX 3070", 16384, 8192), old, false);
    check(gog_verdict.level == Level::good, "an old GOG game on a modern PC");

    if (failures) return 1;
    std::cout << "system requirements tests passed\n";
    return 0;
}
