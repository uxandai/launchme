#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "quick_menu.hpp"
#include <cmath>

namespace {
using palette::gray;
using render_detail::box;
}

void Renderer::setGlanceThumbnail(size_t index, ImageBytes pixels) {
    if (index >= glance_thumbnails_.size()) return;
    auto& image = glance_thumbnails_[index];
    image = {}; image.pixels = std::move(pixels);
    if (image.pixels) createBitmap(image);
}

void Renderer::measureGlance(glance_ui::State& state) {
    state.measured_width = viewport_.width;
    if (state.timeline()) { state.card_heights.fill(116); return; }
    state.card_heights.fill(glance_ui::Layout::card_height);
    if (!dwrite_ || !menu_font_) return;
    const float width = glance_ui::Layout{viewport_.width,viewport_.height}.cardWidth()-36;
    for (size_t i = 0; i < state.feed.items.size() && i < state.card_heights.size(); ++i) {
        const auto& title = state.feed.items[i].title;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwrite_->CreateTextLayout(title.c_str(),static_cast<UINT32>(title.size()),menu_font_.Get(),width,52,layout.GetAddressOf()))) continue;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(layout->GetMetrics(&metrics))) state.card_heights[i] = 194 + std::min(52.0f,metrics.height) + 34;
    }
}

void Renderer::drawGlanceTimelineCard(size_t index,settings_ui::Rect r,float alpha,float weight) {
    const auto& state=*context_->glance; const auto& item=state.feed.items[index]; const auto& date=item.release;
    const glance_ui::Layout layout{viewport_.width,viewport_.height};
    if (layout.groupStart(static_cast<int>(index),state)) {
        const auto* heading=date.recent ? tr(L"Ostatnie 7 dni — wydane") : date.group()==0 ? tr(L"Najbliższe 30 dni") : date.group()==1 ? tr(L"Przybliżone terminy") : tr(L"Bez ustalonej daty");
        textLine(heading,layout.left,r.top-18,viewport_.width-2*layout.left,settings_body_font_.Get(),gray(0.78f,alpha));
    }
    std::wstring primary,secondary;
    const auto month=[&](const wchar_t* format) {
        SYSTEMTIME time{}; time.wYear=static_cast<WORD>(date.year); time.wMonth=static_cast<WORD>(date.month); time.wDay=1;
        wchar_t buffer[128]{};
        if (!GetDateFormatEx(i18n::localeName(),0,&time,format,buffer,128,nullptr)) return std::wstring{};
        return std::wstring(buffer);
    };
    switch (date.precision) {
    case glance::ReleasePrecision::day: primary=std::to_wstring(date.day); secondary=month(L"MMMM yyyy"); break;
    case glance::ReleasePrecision::month: primary=month(L"MMMM"); secondary=std::to_wstring(date.year); break;
    case glance::ReleasePrecision::quarter: {
        static constexpr const wchar_t* quarters[]={L"I",L"II",L"III",L"IV"};
        primary=std::wstring(quarters[(date.month-1)/3])+tr(L" kwartał"); secondary=std::to_wstring(date.year); break;
    }
    case glance::ReleasePrecision::year: primary=std::to_wstring(date.year); secondary=tr(L"Termin przybliżony"); break;
    default:
        primary=date.available ? tr(L"Bez daty") : tr(L"Brak danych");
        secondary=!date.available ? tr(L"Spróbuj odświeżyć") : date.raw.empty() || date.raw==L"Coming soon" || date.raw==L"To be announced" ? tr(L"Do ogłoszenia") : date.raw;
        break;
    }
    const float center=(r.top+r.bottom)*0.5f, rail=layout.left+192;
    textLine(primary,layout.left,center-12,174,category_font_.Get(),gray(0.96f,alpha));
    textLine(secondary,layout.left,center+18,174,friend_status_font_.Get(),gray(0.63f,alpha));
    line(rail,r.top,rail,r.bottom+12,gray(0.7f,alpha*0.25f),1);
    brush(gray(0.82f,alpha));
    const auto dot=D2D1::Ellipse(D2D1::Point2F(rail,center),4+weight,4+weight);
    if (date.precision==glance::ReleasePrecision::day) target_->FillEllipse(dot,solid_.Get()); else target_->DrawEllipse(dot,solid_.Get(),1.4f);
    const auto shape=tileShape(box(r),tile_shape_);
    if (!shape) return;
    brush(gray(0.16f+0.09f*weight,(0.44f+0.12f*weight)*alpha)); target_->FillGeometry(shape.Get(),solid_.Get());
    const auto picture=D2D1::RectF(r.left+8,r.top+8,r.left+188,r.bottom-8);
    brush(D2D1::ColorF(0.095f,0.125f,0.17f,alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(picture,12,12),solid_.Get());
    if (index<glance_thumbnails_.size() && glance_thumbnails_[index].bitmap) fillImage(glance_thumbnails_[index],picture,12,alpha*reveal(glance_thumbnails_[index]));
    else drawGlanceGlyph(r.left+98,center,30,gray(0.6f,alpha*0.6f));
    drawGlanceText(item.title,D2D1::RectF(r.left+212,r.top+18,r.right-44,r.top+72),menu_font_.Get(),gray(0.97f,alpha));
    textLine(L"Steam",r.left+212,r.bottom-23,r.right-r.left-256,friend_status_font_.Get(),gray(0.66f,alpha));
    const float x=r.right-26;
    line(x-4,center+4,x+4,center-4,gray(0.85f,alpha),1.5f);
    line(x-3,center-4,x+4,center-4,gray(0.85f,alpha),1.5f); line(x+4,center-4,x+4,center+3,gray(0.85f,alpha),1.5f);
}

float Renderer::drawGlanceText(const std::wstring& value, D2D1_RECT_F rect, IDWriteTextFormat* font, D2D1_COLOR_F ink) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(value.c_str(),static_cast<UINT32>(value.size()),font,
        rect.right-rect.left,rect.bottom-rect.top,layout.GetAddressOf()))) return 0;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    brush(ink);
    target_->DrawTextLayout(D2D1::Point2F(rect.left,rect.top),layout.Get(),solid_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    DWRITE_TEXT_METRICS metrics{}; layout->GetMetrics(&metrics);
    return std::min(metrics.height,rect.bottom-rect.top);
}

void Renderer::drawGlanceGlyph(float x, float y, float size, D2D1_COLOR_F ink) {
    // One closed, axis-symmetric outline. The old off-centre dot skewed the visual centre.
    const float r = size * 0.5f, inner = r * 0.25f;
    const D2D1_POINT_2F points[] = {{x,y-r},{x+inner,y-inner},{x+r,y},{x+inner,y+inner},
        {x,y+r},{x-inner,y+inner},{x-r,y},{x-inner,y-inner}};
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> shape;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(&shape)) || FAILED(shape->Open(&sink))) return;
    sink->BeginFigure(points[0],D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLines(points+1,static_cast<UINT32>(std::size(points)-1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;
    brush(ink); target_->DrawGeometry(shape.Get(),solid_.Get(),1.8f);
}

void Renderer::drawGlanceShortcut() {
    const float alpha = context_->glance_alpha;
    if (alpha <= 0.003f) return;
    const float hover = context_->glance_hover;
    if (context_->home_widgets == home_widgets::Placement::pills && context_->flix_icons) {
        // Flix with the menu's icons at the top: only the glyph on a small circle, like them.
        const auto rect = box(context_->glance_spot);
        const float shown = alpha * context_->flix_icons_alpha;
        if (shown <= 0.003f) return;
        // The pad's icon: a white disc with a dark glyph; its name stands left of the row (drawFlixIcons).
        const bool focused = context_->flix_icon_focus == quick_menu::count(static_cast<int>(context_->plugin_menu.size()));
        const float cx = (rect.left+rect.right)*0.5f, cy = (rect.top+rect.bottom)*0.5f;
        if (focused) { brush(gray(0.96f,shown)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy),(rect.right-rect.left)*0.5f+2,(rect.bottom-rect.top)*0.5f+2),solid_.Get()); }
        else drawWidgetPill(rect,shown,hover);
        drawGlanceGlyph(cx,cy,18,focused ? gray(0.06f,shown) : gray(0.95f,shown));
        return;
    }
    if (context_->home_widgets == home_widgets::Placement::pills) {
        // Flix: a slim pill — the glyph, the name and the key.
        const auto rect = box(context_->glance_spot);
        drawWidgetPill(rect,alpha,hover);
        const float cy = (rect.top+rect.bottom)*0.5f, key_x = rect.right-4-15;
        drawGlanceGlyph(rect.left+22,cy,18,gray(0.95f,alpha));
        textLine(L"Glance",rect.left+40,cy,key_x-15-8-(rect.left+40),settings_body_font_.Get(),gray(0.98f,alpha));
        brush(gray(0.98f,alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(key_x,cy),15,15),solid_.Get());
        textLine(context_->glance_key,key_x-15,cy,30,toast_sub_font_.Get(),gray(0.08f,alpha),DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }
    home_widgets::Spots spots;
    spots.placement = context_->home_widgets; spots.glance = context_->glance_spot;
    const auto r = home_widgets::glanceArea(spots,hover);
    const auto rect = box(r);
    const float previous_corner = squircle_corner_, previous_reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 80;
    const auto shape = tileShape(rect, tile_shape_);
    squircle_corner_ = previous_corner; corner_reference_ = previous_reference;
    if (!shape) return;
    const Glass glass{shape, glass_level_ > 0.01f && frostCapture(shape.Get(),glassBlur(24))};
#ifdef LAUNCHME_UI_TEST
    glance_frosted_for_test = glass.frosted;
#endif
    // A light smoked pane: the wallpaper stays visible through the blur. One tint only,
    // instead of the old opaque navy fill plus a second coloured coat over it.
    glassFill(glass,D2D1::ColorF(0.04f,0.05f,0.07f,glass_level_ > 0.01f ? 0.30f : 0.94f),alpha);
    brush(gray(1,(0.035f + hover*0.075f)*alpha)); target_->FillGeometry(shape.Get(),solid_.Get());
    brush(gray(1,(0.13f+hover*0.42f)*alpha)); target_->DrawGeometry(shape.Get(),solid_.Get(),1.0f);
    const float cx = r.right-47, cy = (r.top+r.bottom)*0.5f;
    textGroup(L"Glance",tr(L"Aktualności Twoich gier i świata gamingu"),r.left+24,cy,r.right-r.left-118,
        category_font_.Get(),gray(0.98f,alpha),settings_body_font_.Get(),gray(0.85f,alpha),2,false,6);
    brush(gray(0.98f,alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy),23,23),solid_.Get());
    textLine(context_->glance_key,cx-23,cy,46,friend_section_font_.Get(),gray(0.08f,alpha),DWRITE_TEXT_ALIGNMENT_CENTER);
}

void Renderer::drawGlance() {
    drawBackground(); drawFrostedBackdrop(0.70f);
    if (!context_->glance) return;
    beginScreenForeground(); // Animate the hub's UI over the stationary glass wallpaper.
    const auto& state = *context_->glance;
    const glance_ui::Layout layout{viewport_.width,viewport_.height};
    const auto back = layout.back(), refresh = layout.refresh();
    const float back_focus = state.motion_valid ? state.back_focus.value : state.focus == -2 ? 1.0f : 0.0f;
    const float refresh_focus = state.motion_valid ? state.refresh_focus.value : state.focus == -3 ? 1.0f : 0.0f;
    const float tab_focus = state.motion_valid ? state.tab_focus.value : state.focus == -1 ? 1.0f : 0.0f;
    drawButtonGlass(box(back),1,false,state.hover == -2);
    drawButtonFocus(box(back),back_focus);
    const auto ink = buttonInk(back_focus);
    const float cy = (back.top+back.bottom)*0.5f;
    line(back.left+22,cy,back.left+34,cy,ink,2);
    line(back.left+22,cy,back.left+28,cy-6,ink,2);
    line(back.left+22,cy,back.left+28,cy+6,ink,2);
    textLine(L"Glance",back.right+26,cy,250,settings_heading_font_.Get(),gray(0.97f));
    drawButtonGlass(box(refresh),1,false,state.hover == -3);
    drawButtonFocus(box(refresh),refresh_focus);
    textLine(tr(L"Odśwież"),refresh.left,65,refresh.right-refresh.left,settings_body_font_.Get(),
        buttonInk(refresh_focus),DWRITE_TEXT_ALIGNMENT_CENTER);
    for (int i = 0; i < glance::section_count; ++i) drawButtonGlass(box(layout.tab(i)),1,false,state.hover == -10-i);
    const auto selected_tab = layout.tabPosition(state.motion_valid ? state.tab_motion.value : static_cast<float>(glance_ui::tabIndex(state.section)));
    drawButtonFocus(box(selected_tab),tab_focus);
    for (int i = 0; i < glance::section_count; ++i) {
        const auto tab = layout.tab(i);
        const float ink_focus = tab_focus*std::clamp(1-std::abs(selected_tab.left-tab.left)/(tab.right-tab.left),0.0f,1.0f);
        textLine(glance_ui::title(i),tab.left+8,(tab.top+tab.bottom)*0.5f,tab.right-tab.left-16,
            settings_body_font_.Get(),buttonInk(ink_focus),DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (tab_focus < 0.999f) {
        const float cx = (selected_tab.left+selected_tab.right)*0.5f;
        line(cx-14,selected_tab.bottom+7,cx+14,selected_tab.bottom+7,gray(0.85f,1-tab_focus),2);
    }
    std::wstring status;
    if (state.loading) status = tr(L"Wczytywanie…");
    else if (state.section == static_cast<int>(glance::Section::owned)) {
        if (!state.feed.games_total) status = tr(L"Brak gier Steam w bibliotece launchera.");
        else if (state.feed.unavailable) status = tr(L"Nie udało się pobrać treści. Spróbuj odświeżyć.");
        else if (state.feed.items.empty()) status = tr(L"Brak ogłoszeń twórców dla wybranych gier.");
        else status = tr(L"Ogłoszenia twórców · ") + std::to_wstring(state.feed.games_checked) + L" / " + std::to_wstring(state.feed.games_total) +
            tr(L" gier Steam · priorytet: ostatnio grane") + (state.feed.stale ? tr(L" · część źródeł niedostępna") : L"");
    }
    else if (state.feed.stale) status = tr(L"Źródło niedostępne — ostatnio zapisane treści");
    else if (state.feed.unavailable) status = tr(L"Nie udało się pobrać treści. Spróbuj odświeżyć.");
    else if (state.feed.items.empty()) status = tr(L"Brak nowych treści w tym źródle.");
    else if (state.timeline()) status = state.feed.dates_partial ? tr(L"Wybrane premiery · część katalogu Steam jest niedostępna") : tr(L"Wybrane premiery Steam · 30 dni wprzód · ostatnie 7 dni: do 12 gier");
    else status = state.section < 2 ? tr(L"Źródło: Steam · gry i dodatki") :
        (i18n::english() ? L"Eurogamer · RSS" : L"Eurogamer.pl · RSS");
    textLine(status,layout.left,187,viewport_.width-2*layout.left,friend_status_font_.Get(),palette::muted());
    target_->PushAxisAlignedClip(D2D1::RectF(layout.left-5,layout.top-5,viewport_.width-layout.left+5,layout.bottom()+4),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Large editorial cards keep the shortcut's compact corners; never stretch a unit squircle.
    const float previous_corner = squircle_corner_, previous_reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 80;
    for (size_t i = 0; i < state.feed.items.size(); ++i) {
        const auto r = layout.visualCard(static_cast<int>(i),state);
        if (r.bottom < layout.top || r.top > layout.bottom()) continue;
        const float alpha = state.appearance(i);
        const float weight = state.motion_valid ? state.emphasis[i].value : state.focus == static_cast<int>(i) ? 1.0f : 0.0f;
        if (state.timeline()) { drawGlanceTimelineCard(i,r,alpha,weight); continue; }
        const auto shape = tileShape(box(r), tile_shape_);
        if (!shape) continue;
        brush(gray(0.16f+0.09f*weight,(0.44f+0.12f*weight)*alpha)); target_->FillGeometry(shape.Get(),solid_.Get());
        const auto picture = D2D1::RectF(r.left+8,r.top+8,r.right-8,r.top+182);
        brush(D2D1::ColorF(0.095f,0.125f,0.17f,alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(picture,14,14),solid_.Get());
        if (i < glance_thumbnails_.size() && glance_thumbnails_[i].bitmap)
            fillImage(glance_thumbnails_[i],picture,14,alpha*reveal(glance_thumbnails_[i]),false);
        else drawGlanceGlyph((picture.left+picture.right)*0.5f,(picture.top+picture.bottom)*0.5f,42,gray(0.6f,0.6f*alpha));
        const auto& item = state.feed.items[i];
        const float text_top = r.top+194;
        const float title_height = drawGlanceText(item.title,D2D1::RectF(r.left+18,text_top,r.right-18,r.top+246),menu_font_.Get(),gray(0.97f,alpha));
        const float source_y = std::min(r.bottom-17,text_top+title_height+13);
        const auto source = item.source + (item.date.empty() ? L"" : L" · " + item.date);
        textLine(source,r.left+18,source_y,r.right-r.left-60,friend_status_font_.Get(),gray(0.72f,alpha));
        const float x = r.right-25, y = source_y;
        line(x-4,y+4,x+4,y-4,gray(0.85f,alpha),1.5f);
        line(x-3,y-4,x+4,y-4,gray(0.85f,alpha),1.5f); line(x+4,y-4,x+4,y+3,gray(0.85f,alpha),1.5f);
    }
    if (state.focus >= 0 && state.focus < static_cast<int>(state.feed.items.size())) {
        auto r = layout.visualCard(state.focus,state);
        if (state.focus_valid) {
            const auto& f = state.focus_box;
            const float dx = r.left-layout.card(state.focus,state.drawScroll(),&state).left;
            const float dy = r.top-layout.card(state.focus,state.drawScroll(),&state).top;
            r = {f.left.value+dx,f.top.value-state.drawScroll()+dy,f.right.value+dx,f.bottom.value-state.drawScroll()+dy};
        }
        if (const auto shape = tileShape(box(r),tile_shape_)) {
            brush(gray(1,0.85f*state.appearance(static_cast<size_t>(state.focus)))); target_->DrawGeometry(shape.Get(),solid_.Get(),1.6f);
        }
    }
    if (state.loading && state.feed.items.empty()) {
        for (int i = 0; i < layout.columns(&state); ++i) {
            const auto r = layout.card(i,0,&state);
            brush(gray(1,0.04f + 0.018f*(1+std::sin(elapsed_*3+i))));
            target_->FillRoundedRectangle(D2D1::RoundedRect(box(r),24,24),solid_.Get());
            const auto picture=state.timeline() ? D2D1::RectF(r.left+8,r.top+8,r.left+188,r.bottom-8) :
                D2D1::RectF(r.left+8,r.top+8,r.right-8,r.top+182);
            brush(gray(1,0.035f)); target_->FillRoundedRectangle(D2D1::RoundedRect(picture,14,14),solid_.Get());
            drawGlanceGlyph((picture.left+picture.right)*0.5f,(picture.top+picture.bottom)*0.5f,38,gray(0.65f,0.4f));
        }
    }
    squircle_corner_ = previous_corner; corner_reference_ = previous_reference;
    target_->PopAxisAlignedClip();
    textLine(tr(L"Artykuły i gry otwierają się w oknie przeglądarki launchera."),layout.left,viewport_.height-60,
        viewport_.width-2*layout.left,friend_status_font_.Get(),gray(0.54f));
}
