#include "pdf_pages.hpp"

namespace pdf_pages {
bool available() { return false; }
Page render(const std::filesystem::path&, int, uint32_t) { return {}; }
void render(const std::filesystem::path&, const std::vector<int>& indices, uint32_t, const std::function<bool(int, Page)>& deliver) {
    for (const int index : indices) if (!deliver(index, {})) return;
}
}
