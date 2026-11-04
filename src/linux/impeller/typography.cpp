// The typography context text is laid out with: Impeller's own paragraph engine (HarfBuzz and ICU
// inside libimpeller), the system's fonts through fontconfig, and the font files registered here.
#include "state.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>

namespace lm_impeller {
ImpellerTypographyContext typography() {
    static ImpellerTypographyContext context = ImpellerTypographyContextNew(); // Lives as long as the process.
    return context;
}

bool registerFontFile(const char* path) {
    static std::mutex mutex;
    static std::set<std::string> known;
    std::lock_guard lock(mutex);
    if (!known.insert(path).second) return true;
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) { std::fclose(file); return false; }
    // The bytes stay for the process: Impeller may read them whenever a paragraph needs a glyph.
    auto* bytes = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(size)));
    const bool read = bytes && std::fread(bytes, 1, static_cast<size_t>(size), file) == static_cast<size_t>(size);
    std::fclose(file);
    if (!read) { std::free(bytes); return false; }
    const ImpellerMapping mapping{bytes, static_cast<uint64_t>(size), nullptr};
    return ImpellerTypographyContextRegisterFont(typography(), &mapping, nullptr, nullptr);
}

// ---- Paragraphs ---------------------------------------------------------------------------------

namespace {
ImpellerFontWeight nativeWeight(int weight) {
    const int step = std::clamp((weight + 50) / 100, 1, 9);
    return static_cast<ImpellerFontWeight>(kImpellerFontWeight100 + step - 1);
}

using StyleHandle = Handle<ImpellerParagraphStyle, ImpellerParagraphStyleRelease>;
using ParagraphBuilderHandle = Handle<ImpellerParagraphBuilder, ImpellerParagraphBuilderRelease>;

StyleHandle nativeStyle(const TextStyle& style) {
    StyleHandle result(ImpellerParagraphStyleNew());
    if (!result) return {};
    ImpellerParagraphStyleSetFontFamily(result.get(), style.family.c_str());
    ImpellerParagraphStyleSetFontSize(result.get(), style.size);
    ImpellerParagraphStyleSetFontWeight(result.get(), nativeWeight(style.weight));
    ImpellerParagraphStyleSetFontStyle(result.get(), style.italic ? kImpellerFontStyleItalic : kImpellerFontStyleNormal);
    ImpellerParagraphStyleSetTextDirection(result.get(), kImpellerTextDirectionLTR);
    if (!style.locale.empty()) ImpellerParagraphStyleSetLocale(result.get(), style.locale.c_str());
    if (style.underline) {
        const ImpellerTextDecoration decoration{kImpellerTextDecorationTypeUnderline, {style.ink.color.r, style.ink.color.g, style.ink.color.b, style.ink.color.a, kImpellerColorSpaceSRGB},
                                                kImpellerTextDecorationStyleSolid, 1.0f};
        ImpellerParagraphStyleSetTextDecoration(result.get(), &decoration);
    }
    if (auto paint = makeNativePaint(style.ink)) ImpellerParagraphStyleSetForeground(result.get(), paint.get());
    return result;
}
} // namespace

TextParagraph::TextParagraph(const ParagraphSpec& spec) {
    if (spec.runs.empty()) return;
    StyleHandle root = nativeStyle(spec.runs.front().style);
    if (!root) return;
    ImpellerParagraphStyleSetTextAlignment(root.get(), spec.align == TextAlign::right ? kImpellerTextAlignmentRight
                                                       : spec.align == TextAlign::center ? kImpellerTextAlignmentCenter
                                                       : spec.align == TextAlign::justify ? kImpellerTextAlignmentJustify
                                                                                          : kImpellerTextAlignmentLeft);
    if (spec.max_lines) ImpellerParagraphStyleSetMaxLines(root.get(), spec.max_lines);
    if (spec.ellipsis) ImpellerParagraphStyleSetEllipsis(root.get(), "…");
    ParagraphBuilderHandle builder(ImpellerParagraphBuilderNew(typography()));
    if (!builder) return;
    ImpellerParagraphBuilderPushStyle(builder.get(), root.get());
    for (const TextRun& run : spec.runs) {
        StyleHandle style = nativeStyle(run.style);
        if (!style || run.utf8.empty()) continue;
        ImpellerParagraphBuilderPushStyle(builder.get(), style.get());
        ImpellerParagraphBuilderAddText(builder.get(), reinterpret_cast<const uint8_t*>(run.utf8.data()), static_cast<uint32_t>(run.utf8.size()));
        ImpellerParagraphBuilderPopStyle(builder.get());
    }
    paragraph_ = ImpellerParagraphBuilderBuildParagraphNew(builder.get(), spec.width);
}

TextParagraph::~TextParagraph() { ImpellerParagraphRelease(paragraph_); }

float TextParagraph::height() const { return paragraph_ ? ImpellerParagraphGetHeight(paragraph_) : 0; }
float TextParagraph::longestLine() const { return paragraph_ ? ImpellerParagraphGetLongestLineWidth(paragraph_) : 0; }

std::vector<LineInfo> TextParagraph::lines() const {
    std::vector<LineInfo> result;
    if (!paragraph_) return result;
    const uint32_t count = ImpellerParagraphGetLineCount(paragraph_);
    ImpellerLineMetrics metrics = ImpellerParagraphGetLineMetrics(paragraph_);
    if (!metrics) return result;
    for (uint32_t i = 0; i < count; ++i) {
        LineInfo line;
        line.start = ImpellerLineMetricsGetCodeUnitStartIndex(metrics, i);
        line.end = ImpellerLineMetricsGetCodeUnitEndIndex(metrics, i);
        line.end_excluding_whitespace = ImpellerLineMetricsGetCodeUnitEndIndexExcludingWhitespace(metrics, i);
        line.end_including_newline = ImpellerLineMetricsGetCodeUnitEndIndexIncludingNewline(metrics, i);
        line.ascent = static_cast<float>(ImpellerLineMetricsGetAscent(metrics, i));
        line.descent = static_cast<float>(ImpellerLineMetricsGetDescent(metrics, i));
        line.baseline = static_cast<float>(ImpellerLineMetricsGetBaseline(metrics, i));
        line.width = static_cast<float>(ImpellerLineMetricsGetWidth(metrics, i));
        line.height = static_cast<float>(ImpellerLineMetricsGetHeight(metrics, i));
        line.left = static_cast<float>(ImpellerLineMetricsGetLeft(metrics, i));
        line.hard_break = ImpellerLineMetricsIsHardbreak(metrics, i);
        result.push_back(line);
    }
    // The metrics belong to the paragraph (cached with it): not released here.
    return result;
}

bool TextParagraph::glyphAt(size_t unit, GlyphBox& box) const {
    if (!paragraph_) return false;
    ImpellerGlyphInfo info = ImpellerParagraphCreateGlyphInfoAtCodeUnitIndexNew(paragraph_, unit);
    if (!info) return false;
    ImpellerRect bounds{};
    ImpellerGlyphInfoGetGraphemeClusterBounds(info, &bounds);
    box.bounds = Rect::XYWH(static_cast<float>(bounds.x), static_cast<float>(bounds.y), static_cast<float>(bounds.width), static_cast<float>(bounds.height));
    box.begin = ImpellerGlyphInfoGetGraphemeClusterCodeUnitRangeBegin(info);
    box.end = ImpellerGlyphInfoGetGraphemeClusterCodeUnitRangeEnd(info);
    box.ellipsis = ImpellerGlyphInfoIsEllipsis(info);
    ImpellerGlyphInfoRelease(info);
    return true;
}

void TextParagraph::draw(Canvas& canvas, Point origin) const { canvas.drawParagraph(paragraph_, origin); }
} // namespace lm_impeller
