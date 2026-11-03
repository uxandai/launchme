// DirectWrite text layouts on Impeller's paragraphs. The paragraph engine shapes, picks fonts,
// breaks lines and signs trimmed text with "…"; this file adds what DirectWrite does differently:
// alignment within the maximum width (also without wrapping), trailing whitespace outside the
// aligned width, line spacing, paragraph alignment, trimming of text below the box at its last
// visible line, and positions counted in wchar_t units. Character spacing is not supported by
// Impeller's typography and is ignored.
#include "dwrite_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lm_dwrite {
namespace {
constexpr float unbounded_width = 100000;

bool space(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == 0x2028 || c == 0x2029 || c == 0x00A0 || c == 0x3000; }
} // namespace

TextLayout::TextLayout(const FormatState& format, const WCHAR* text, UINT32 length, float maxWidth, float maxHeight)
    : FormatBase(format.font(), format.paragraph()), text_(text, length), max_width_(maxWidth), max_height_(maxHeight) {
    utf8_.reserve(length);
    offsets_.reserve(length + 1);
    utf16_.reserve(length + 1);
    uint32_t units = 0;
    for (UINT32 i = 0; i < length; ++i) {
        offsets_.push_back(static_cast<uint32_t>(utf8_.size()));
        utf16_.push_back(units);
        char32_t c = static_cast<char32_t>(text_[i]);
        // A UTF-16 pair (some of the launcher's strings carry them): one code point; its low half
        // is an empty character at the code point's start.
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < length && text_[i + 1] >= 0xDC00 && text_[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(text_[i + 1]) - 0xDC00);
            offsets_.push_back(offsets_.back());
            utf16_.push_back(units);
            ++i;
        }
        if (c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) c = 0xFFFD;
        units += c >= 0x10000 ? 2 : 1;
        appendUtf8(utf8_, c ? c : 0x2060); // A NUL would end the text: an invisible word joiner instead.
    }
    offsets_.push_back(static_cast<uint32_t>(utf8_.size()));
    utf16_.push_back(units);
}

HRESULT TextLayout::create(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, FLOAT maxWidth, FLOAT maxHeight,
                           ComPtr<TextLayout>& layout) {
    const FormatState* state = FormatState::from(format);
    if (!state || (!text && length) || !(maxWidth >= 0) || !(maxHeight >= 0)) return E_INVALIDARG;
    layout.Attach(new TextLayout(*state, text ? text : L"", length, maxWidth, maxHeight));
    return S_OK;
}

TextLayout* TextLayout::from(IDWriteTextLayout* layout) { return dynamic_cast<TextLayout*>(layout); }

// ---- Formatting ---------------------------------------------------------------------------

template <class Apply>
HRESULT TextLayout::style(DWRITE_TEXT_RANGE range, bool relayout, Apply apply) {
    const auto length = static_cast<UINT32>(text_.size());
    if (range.startPosition >= length || !range.length) return S_OK;
    const UINT32 end = range.startPosition + std::min(range.length, length - range.startPosition);
    if (styles_.empty()) styles_.assign(length, CharStyle{font_->size, font_->weight});
    for (UINT32 i = range.startPosition; i < end; ++i) apply(styles_[i]);
    if (relayout) dirty_ = true;
    else restyled_ = true;
    return S_OK;
}

HRESULT TextLayout::SetMaxWidth(FLOAT maxWidth) {
    if (!(maxWidth >= 0)) return E_INVALIDARG;
    max_width_ = maxWidth;
    dirty_ = true;
    return S_OK;
}

HRESULT TextLayout::SetFontWeight(DWRITE_FONT_WEIGHT fontWeight, DWRITE_TEXT_RANGE textRange) {
    if (fontWeight < 1 || fontWeight > 999) return E_INVALIDARG;
    return style(textRange, true, [&](CharStyle& style) { style.weight = fontWeight; });
}

HRESULT TextLayout::SetFontSize(FLOAT fontSize, DWRITE_TEXT_RANGE textRange) {
    if (!(fontSize > 0)) return E_INVALIDARG;
    return style(textRange, true, [&](CharStyle& style) { style.size = fontSize; });
}

HRESULT TextLayout::SetUnderline(BOOL hasUnderline, DWRITE_TEXT_RANGE textRange) {
    return style(textRange, false, [&](CharStyle& style) { style.underline = hasUnderline != FALSE; });
}

HRESULT TextLayout::SetDrawingEffect(IUnknown* drawingEffect, DWRITE_TEXT_RANGE textRange) {
    return style(textRange, false, [&](CharStyle& style) { style.effect = drawingEffect; });
}

// Kept with the character, not applied: Impeller's typography has no letter spacing.
HRESULT TextLayout::SetCharacterSpacing(FLOAT leadingSpacing, FLOAT trailingSpacing, FLOAT minimumAdvanceWidth, DWRITE_TEXT_RANGE textRange) {
    if (!(minimumAdvanceWidth >= 0)) return E_INVALIDARG;
    return style(textRange, true, [&](CharStyle& style) { style.spacing = leadingSpacing + trailingSpacing; });
}

// ---- Layout -------------------------------------------------------------------------------

UINT32 TextLayout::characterOfUtf16(size_t unit) const {
    const auto it = std::lower_bound(utf16_.begin(), utf16_.end(), static_cast<uint32_t>(unit));
    if (it != utf16_.end() && *it == unit) return static_cast<UINT32>(it - utf16_.begin());
    return static_cast<UINT32>(std::max<std::ptrdiff_t>(0, it - utf16_.begin() - 1));
}

float TextLayout::layoutWidth() const { return std::isfinite(max_width_) && max_width_ < unbounded_width ? max_width_ : unbounded_width; }

gx::ParagraphSpec TextLayout::specFor(const Shape& shape, bool fine, const gx::Paint* paint) const {
    gx::ParagraphSpec spec;
    spec.align = shape.align;
    spec.max_lines = shape.max_lines;
    spec.ellipsis = shape.ellipsis;
    spec.width = shape.width;
    gx::TextStyle base;
    base.family = font_->resolved;
    base.size = font_->size;
    base.weight = static_cast<int>(font_->weight);
    base.italic = font_->style != DWRITE_FONT_STYLE_NORMAL;
    base.locale = font_->locale;
    if (paint) base.ink = *paint;
    else base.ink.color = {1, 1, 1, 1};
    const auto length = static_cast<UINT32>(text_.size());
    if (styles_.empty() || !length) {
        spec.runs.push_back({utf8_, base});
        return spec;
    }
    for (UINT32 first = 0; first < length;) {
        UINT32 last = first + 1;
        const CharStyle& own = styles_[first];
        while (last < length && styles_[last].size == own.size && styles_[last].weight == own.weight &&
               (!fine || (styles_[last].underline == own.underline && styles_[last].effect.Get() == own.effect.Get())))
            ++last;
        gx::TextStyle part = base;
        part.size = own.size;
        part.weight = static_cast<int>(own.weight);
        if (fine) {
            part.underline = own.underline;
            gx::Paint ink = part.ink;
            if (own.effect && paint && lm_gfx::brushPaint(own.effect.Get(), ink)) part.ink = ink;
        }
        spec.runs.push_back({utf8_.substr(offsets_[first], offsets_[last] - offsets_[first]), part});
        first = last;
    }
    return spec;
}

std::unique_ptr<gx::TextParagraph> TextLayout::build(const Shape& shape) const {
    return std::make_unique<gx::TextParagraph>(specFor(shape, false, nullptr));
}

void TextLayout::layOut() {
    const unsigned fonts = fontGeneration();
    if (fonts != fonts_) {
        fonts_ = fonts;
        dirty_ = true;
    }
    if (!dirty_) {
        restyled_ = false;
        return;
    }
    dirty_ = restyled_ = false;
    const bool wraps = paragraph_.wrapping != DWRITE_WORD_WRAPPING_NO_WRAP, trims = paragraph_.trims();
    const bool bounded = std::isfinite(max_width_) && max_width_ < unbounded_width;
    gx::TextAlign align = gx::TextAlign::left;
    if (bounded) {
        switch (paragraph_.alignment) {
        case DWRITE_TEXT_ALIGNMENT_TRAILING: align = gx::TextAlign::right; break;
        case DWRITE_TEXT_ALIGNMENT_CENTER: align = gx::TextAlign::center; break;
        case DWRITE_TEXT_ALIGNMENT_JUSTIFIED: align = wraps ? gx::TextAlign::justify : gx::TextAlign::left; break;
        default: break;
        }
    }
    shift_ = 0;
    Shape shape;
    shape.align = align;
    std::unique_ptr<gx::TextParagraph> laid;
    if (!wraps) {
        if (trims && bounded) {
            // Each line at the width, signed when it is longer. One line of text gets the line limit
            // the sign needs.
            shape.width = max_width_;
            shape.ellipsis = true;
            shape.max_lines = utf8_.find('\n') == std::string::npos ? 1 : 0;
            laid = build(shape);
        } else {
            // Untrimmed lines run on: laid out wide enough for the longest, then moved into the box.
            Shape first = shape;
            first.align = gx::TextAlign::left;
            first.width = unbounded_width;
            const auto probe = build(first);
            const float wide = probe->longestLine();
            shape.width = bounded ? std::max(max_width_, std::ceil(wide) + 1) : std::ceil(wide) + 1;
            if (bounded)
                shift_ = align == gx::TextAlign::center ? (max_width_ - shape.width) / 2 : align == gx::TextAlign::right ? max_width_ - shape.width : 0;
            laid = build(shape);
        }
    } else {
        shape.width = bounded ? max_width_ : unbounded_width;
        laid = build(shape);
        // With trimming, text below the box goes: the last line that fits (at least the first)
        // ends in the sign.
        if (trims && std::isfinite(max_height_) && laid->lines().size() >= 2) {
            shape_ = shape;
            measure(*laid);
            size_t fit = 1;
            float bottom = lines_[0].height;
            while (fit < lines_.size() && bottom + lines_[fit].height <= max_height_ + 0.001f) bottom += lines_[fit++].height;
            if (fit < lines_.size()) {
                shape.max_lines = static_cast<unsigned>(fit);
                shape.ellipsis = true;
                laid = build(shape);
            }
        }
    }
    shape_ = shape;
    laid_ = std::move(laid);
    drawn_.reset();
    measure(*laid_);
}

// The paragraph always has a line, also for empty text.
void TextLayout::measure(const gx::TextParagraph& paragraph) {
    std::vector<gx::LineInfo> metrics = paragraph.lines();
    lines_.clear();
    const auto length = static_cast<UINT32>(text_.size());
    if (metrics.empty()) {
        gx::LineInfo empty;
        const float size = font_->size;
        empty.ascent = size * 0.9f;
        empty.descent = size * 0.25f;
        empty.baseline = empty.ascent;
        empty.end = empty.end_excluding_whitespace = empty.end_including_newline = utf16_.back();
        metrics.push_back(empty);
    }
    float top = 0;
    for (size_t i = 0; i < metrics.size(); ++i) {
        const auto& m = metrics[i];
        Line line;
        const float ascent = m.ascent, descent = m.descent, natural = ascent + descent;
        switch (paragraph_.spacing_method) {
        case DWRITE_LINE_SPACING_METHOD_UNIFORM: line.height = paragraph_.spacing; line.baseline = paragraph_.baseline; break;
        case DWRITE_LINE_SPACING_METHOD_PROPORTIONAL: line.height = natural * paragraph_.spacing; line.baseline = ascent * paragraph_.baseline; break;
        default: line.height = natural; line.baseline = ascent; break;
        }
        line.top = top;
        top += line.height;
        line.first = std::min(characterOfUtf16(m.start), length);
        const UINT32 end_text = std::min(characterOfUtf16(m.end), length);
        const UINT32 end_line = std::min(std::max(characterOfUtf16(m.end_including_newline), end_text), length);
        UINT32 end_kept = std::min(characterOfUtf16(m.end_excluding_whitespace), end_text);
        while (end_kept > line.first && space(text_[end_kept - 1])) --end_kept;
        line.length = end_line > line.first ? end_line - line.first : 0;
        line.newline = end_line - end_text;
        line.trailing = end_line - std::max(end_kept, line.first); // DirectWrite counts the newline as whitespace.
        line.width = m.width;
        line.full_width = line.width;
        if (end_kept < end_text) {
            // The trailing whitespace's extent (outside the aligned width).
            for (UINT32 c = end_kept; c < end_text; ++c) {
                gx::GlyphBox box;
                if (paragraph.glyphAt(utf16_[c], box)) line.full_width = std::max(line.full_width, box.bounds.right - m.left);
            }
        }
        line.left = m.left;
        line.p_baseline = m.baseline;
        lines_.push_back(line);
    }
    // Lines continue each other: a line ends where the next starts.
    for (size_t i = 0; i + 1 < lines_.size(); ++i)
        if (lines_[i + 1].first >= lines_[i].first) lines_[i].length = lines_[i + 1].first - lines_[i].first;
    shown_ = lines_.empty() ? 0 : std::min(length, lines_.back().first + lines_.back().length);
    if (!lines_.empty() && shown_ < length && (paragraph_.trims() || shape_.ellipsis)) lines_.back().trimmed = true;

    float width = 0, full_width = 0, left = std::numeric_limits<float>::max();
    for (Line& line : lines_) {
        left = std::min(left, line.left + shift_);
        width = std::max(width, line.width);
        full_width = std::max(full_width, line.full_width);
    }
    const float room = std::isfinite(max_height_) ? max_height_ - top : 0;
    const float offset = paragraph_.vertical == DWRITE_PARAGRAPH_ALIGNMENT_FAR ? room : paragraph_.vertical == DWRITE_PARAGRAPH_ALIGNMENT_CENTER ? room / 2 : 0;
    metrics_ = {left, offset, width, full_width, top, max_width_, max_height_, 1, static_cast<UINT32>(lines_.size())};
}

// ---- Measuring ----------------------------------------------------------------------------

HRESULT TextLayout::GetMetrics(DWRITE_TEXT_METRICS* textMetrics) {
    if (!textMetrics) return E_INVALIDARG;
    layOut();
    *textMetrics = metrics_;
    return S_OK;
}

HRESULT TextLayout::GetLineMetrics(DWRITE_LINE_METRICS* lineMetrics, UINT32 maxLineCount, UINT32* actualLineCount) {
    if (!actualLineCount || (maxLineCount && !lineMetrics)) return E_INVALIDARG;
    layOut();
    const auto count = static_cast<UINT32>(lines_.size());
    *actualLineCount = count;
    for (UINT32 i = 0; i < std::min(count, maxLineCount); ++i) {
        const Line& line = lines_[i];
        lineMetrics[i] = {line.length, line.trailing, line.newline, line.height, line.baseline, line.trimmed};
    }
    return maxLineCount < count ? E_NOT_SUFFICIENT_BUFFER : S_OK;
}

// One rectangle per line of the range; characters trimmed below the box (or past the sign) are
// one trimmed, empty rectangle after the line's end.
HRESULT TextLayout::HitTestTextRange(UINT32 textPosition, UINT32 textLength, FLOAT originX, FLOAT originY,
                                     DWRITE_HIT_TEST_METRICS* hitTestMetrics, UINT32 maxHitTestMetricsCount, UINT32* actualHitTestMetricsCount) {
    if (!actualHitTestMetricsCount || (maxHitTestMetricsCount && !hitTestMetrics)) return E_INVALIDARG;
    *actualHitTestMetricsCount = 0;
    layOut();
    const UINT32 end = textPosition + std::min(textLength, std::numeric_limits<UINT32>::max() - textPosition);
    std::vector<DWRITE_HIT_TEST_METRICS> hits;
    for (size_t index = 0; index < lines_.size(); ++index) {
        const Line& line = lines_[index];
        const UINT32 first = std::max(textPosition, line.first), last = std::min({end, line.first + line.length, shown_});
        if (first >= last) continue;
        const float y = originY + metrics_.top + line.top;
        // The characters' boxes on this line, as one rectangle.
        float left = std::numeric_limits<float>::max(), right = -std::numeric_limits<float>::max();
        for (UINT32 c = first; c < last; ++c) {
            if (c > first && utf16_[c] == utf16_[c - 1]) continue; // The second half of a surrogate pair.
            gx::GlyphBox box;
            if (!laid_->glyphAt(utf16_[c], box)) continue;
            left = std::min(left, box.bounds.left);
            right = std::max(right, box.bounds.right);
        }
        if (left > right) left = right = line.left + (first >= line.first + line.length - line.trailing ? line.full_width : 0);
        const float x = originX + shift_;
        hits.push_back({first, last - first, x + left, y, right - left, line.height, 0, TRUE, FALSE});
    }
    const auto length = static_cast<UINT32>(text_.size());
    if (end > shown_ && textPosition < length && !lines_.empty()) {
        const Line& line = lines_.back();
        const UINT32 first = std::max(textPosition, shown_);
        hits.push_back({first, std::min(end, length) - first, originX + shift_ + line.left + line.full_width, originY + metrics_.top + line.top, 0,
                        line.height, 0, TRUE, TRUE});
    }
    *actualHitTestMetricsCount = static_cast<UINT32>(hits.size());
    if (hits.size() > maxHitTestMetricsCount) return E_NOT_SUFFICIENT_BUFFER;
    std::copy(hits.begin(), hits.end(), hitTestMetrics);
    return S_OK;
}
} // namespace lm_dwrite
