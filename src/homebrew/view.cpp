// Playden - Direct ps5-homebrew-ui comparison renderer.
// Copyright (C) 2026 Leo Lobato
// SPDX-License-Identifier: GPL-3.0-or-later

#include "homebrew/render.hpp"

#include "homebrew/simulation.hpp"

#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include "gfx/gl.hpp"
#include "gfx/renderer.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/components/list.hpp"
#include "ui/components/progress.hpp"
#include "ui/components/tabs.hpp"
#include "ui/fonts.hpp"
#include "ui/glyphs.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace playden::homebrew
{
namespace
{
namespace gfx = hui::gfx;
namespace ui = hui::ui;

constexpr float kWidth = 1920.0F;
constexpr float kHeight = 1080.0F;
constexpr float kSafeX = 96.0F;
constexpr float kFooterY = 986.0F;

const gfx::Color kBackground = gfx::Color::rgb(0x0E0D0C);
const gfx::Color kPanel = gfx::Color::rgb(0x16140F);
const gfx::Color kText = gfx::Color::rgb(0xF3EFE9);
const gfx::Color kSecondary = gfx::Color::rgb(0xA8A29E);
const gfx::Color kMuted = gfx::Color::rgb(0x6B655F);
const gfx::Color kAccent = gfx::Color::rgb(0xF0863A);
const gfx::Color kGreen = gfx::Color::rgb(0x7BBF6A);
const gfx::Color kAmber = gfx::Color::rgb(0xE2B53C);
const gfx::Color kRed = gfx::Color::rgb(0xE05A4F);

ui::Theme playden_theme()
{
    ui::Theme theme{};
    theme.id = "playden";
    theme.name = "Playden";
    theme.family = "Warm console";
    theme.summary = "Warm near-black surfaces with restrained orange focus";
    theme.backdrop.mode = gfx::BackdropMode::gradient;
    theme.backdrop.colors[0] = kBackground;
    theme.backdrop.colors[1] = kBackground;
    theme.page = kBackground;
    theme.page_text = kText;
    theme.page_text_muted = kSecondary;
    theme.surface = kPanel;
    theme.surface_high = gfx::Color::rgb(0x2A2623);
    theme.text = kText;
    theme.text_muted = kSecondary;
    theme.primary = kAccent;
    theme.on_primary = gfx::Color::rgb(0x1A1210);
    theme.secondary = gfx::Color::rgb(0x24211E);
    theme.on_secondary = kText;
    theme.accent = kAccent;
    theme.outline = kText.with_alpha(0.20F);
    theme.focus = kAccent;
    theme.shadow = gfx::Color::rgb(0x000000, 0.55F);
    theme.light = kText.with_alpha(0.12F);
    theme.danger = kRed;
    theme.success = kGreen;
    theme.warning = kAmber;
    theme.style = ui::SurfaceStyle::flat;
    theme.corner = ui::Corner::round;
    theme.radius = 8.0F;
    theme.radius_card = 10.0F;
    theme.border = 2.0F;
    theme.button_border = 2.0F;
    theme.focus_width = 4.0F;
    theme.focus_gap = 5.0F;
    theme.heading = ui::FontRole::display;
    theme.label = ui::FontRole::semibold;
    theme.omega = 22.0F;
    theme.damping = 1.0F;
    theme.dark = true;
    return theme;
}

bool read_file(const std::filesystem::path &path, std::string *out)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return false;
    std::ostringstream bytes;
    bytes << stream.rdbuf();
    *out = bytes.str();
    return stream.good() || stream.eof();
}

std::filesystem::path first_existing(const std::vector<std::filesystem::path> &paths)
{
    for (const auto &path : paths)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
            return path;
    }
    return {};
}

std::string fit(const ui::FontRef &font, std::string_view text, float size, float width)
{
    return font.font->fit(text, size, width);
}

gfx::Color id_color(GameId id)
{
    static constexpr std::array<std::uint32_t, 8> colors = {
        0x634B3B, 0x354F52, 0x53405B, 0x5E4936, 0x334D3E, 0x4D435E, 0x675039, 0x3D4B63};
    return gfx::Color::rgb(colors[id % colors.size()]);
}

const Game *game_by_id(const State &state, GameId id)
{
    const auto found = std::find_if(state.games.begin(), state.games.end(),
                                    [id](const Game &game) { return game.id == id; });
    return found == state.games.end() ? nullptr : &*found;
}

std::string byte_count(std::optional<std::uint64_t> bytes)
{
    if (!bytes)
        return "Unknown";
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    std::ostringstream value;
    value.setf(std::ios::fixed);
    value.precision(*bytes >= static_cast<std::uint64_t>(10.0 * gib) ? 0 : 1);
    value << static_cast<double>(*bytes) / gib << " GB";
    return value.str();
}

ui::Button legend_button(std::string_view legend, std::size_t index)
{
    if (legend.find("Favorite") != std::string_view::npos)
        return ui::Button::square;
    if (legend.find("Sort") != std::string_view::npos || legend.find("Filter") != std::string_view::npos)
        return ui::Button::triangle;
    if (legend.find("More") != std::string_view::npos || legend.find("Action") != std::string_view::npos)
        return ui::Button::options;
    if (legend.find("Back") != std::string_view::npos || legend.find("Cancel") != std::string_view::npos)
        return ui::Button::circle;
    if (legend.find("Previous") != std::string_view::npos)
        return ui::Button::l1;
    if (legend.find("Next") != std::string_view::npos)
        return ui::Button::r1;
    return index == 0 ? ui::Button::cross : ui::Button::circle;
}

ui::Status progress_status(const ViewRow &row)
{
    if (row.dangerous || row.badge.find("Failed") != std::string::npos)
        return ui::Status::danger;
    if (row.badge.find("Paused") != std::string::npos || row.badge.find("Offline") != std::string::npos)
        return ui::Status::warning;
    if (row.progress >= 1.0F)
        return ui::Status::success;
    return ui::Status::accent;
}

} // namespace

class Render::Impl
{
  public:
    explicit Impl(std::filesystem::path assets_directory)
        : assets_(std::move(assets_directory)), theme_(playden_theme())
    {
        initialise();
    }

    ~Impl()
    {
        for (const std::uint32_t texture : art_)
        {
            if (texture != 0)
                glDeleteTextures(1, &texture);
        }
        if (hero_ != 0)
            glDeleteTextures(1, &hero_);
        if (capture_color_ != 0)
            glDeleteRenderbuffers(1, &capture_color_);
        if (capture_framebuffer_ != 0)
            glDeleteFramebuffers(1, &capture_framebuffer_);
    }

    void draw(Simulation &simulation, int width, int height)
    {
        if (!ready_ || width <= 0 || height <= 0)
            return;

        scene_.clear();
        overlay_.clear();
        const State &state = simulation.state();
        const ViewData &view = simulation.view();
        current_state_ = &state;
        clock_ = static_cast<float>(state.clock_ticks) / 60.0F;
        ui::Canvas scene{scene_, fonts_, renderer_.glass_texture(), clock_};

        draw_background();
        const bool root = state.navigation.route == Route::Home || state.navigation.route == Route::Library ||
                          state.navigation.route == Route::Downloads || state.navigation.route == Route::Settings;
        if (root)
            draw_top_navigation(scene, state.navigation.route, state.reduced_motion);

        switch (state.navigation.route)
        {
        case Route::Startup:
            draw_startup(scene, view, state);
            break;
        case Route::SignIn:
            draw_sign_in(scene, view, state);
            break;
        case Route::Home:
            draw_home(scene, simulation);
            break;
        case Route::Library:
            draw_library(scene, simulation);
            break;
        case Route::Downloads:
            draw_downloads(scene, view, state);
            break;
        case Route::Settings:
            draw_settings(scene, view, state);
            break;
        case Route::Details:
            draw_details(scene, view, state);
            break;
        case Route::Session:
            draw_session(scene, view, state);
            break;
        }

        draw_persistent_status(scene, state);
        draw_footer(scene, view);
        draw_overlays(state, view);

        renderer_.begin();
        gfx::BackdropSpec backdrop = theme_.backdrop;
        backdrop.time = clock_;
        renderer_.backdrop(backdrop);
        renderer_.draw(scene_);
        if (!overlay_.empty())
        {
            renderer_.glass();
            renderer_.draw(overlay_);
        }
        renderer_.present(0, width, height);
        last_width_ = width;
        last_height_ = height;
        has_frame_ = true;
    }

    bool capture_png(const std::filesystem::path &path, int width, int height) const
    {
        if (!ready_ || !has_frame_ || width <= 0 || height <= 0 || !ensure_capture(width, height))
            return false;

        glBindFramebuffer(GL_FRAMEBUFFER, capture_framebuffer_);
        glViewport(0, 0, width, height);
        glClearColor(kBackground.r, kBackground.g, kBackground.b, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        renderer_.present(capture_framebuffer_, width, height);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (std::size_t index = 3; index < pixels.size(); index += 4)
            pixels[index] = 255;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        stbi_flip_vertically_on_write(1);
        return stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(), width * 4) != 0;
    }

  private:
    bool initialise()
    {
        if (!renderer_.init())
            return false;
        if (!load_fonts())
            return false;
        load_art();
        configure_components();
        ready_ = true;
        return true;
    }

    std::filesystem::path asset(std::string_view relative) const
    {
        const std::filesystem::path direct = assets_ / relative;
        const std::filesystem::path nested = assets_ / "assets" / relative;
        const std::filesystem::path comparison = assets_ / "ui-comparison" / "assets" / relative;
        const std::filesystem::path fixture = assets_ / "fixtures" / "ui-comparison" / "assets" / relative;
        return first_existing({direct, nested, comparison, fixture});
    }

    bool load_face(gfx::Font *face, ui::FontRef *ref,
                   const std::vector<std::string> &names)
    {
        std::vector<std::filesystem::path> candidates;
        for (const std::string &name : names)
        {
            candidates.push_back(asset("fonts/" + name));
            candidates.push_back(assets_ / "fonts" / name);
        }
        const auto path = first_existing(candidates);
        std::string bytes;
        if (path.empty() || !read_file(path, &bytes) || !face->load(bytes))
            return false;
        ref->font = face;
        ref->texture = renderer_.batch().create_font_texture(*face);
        return ref->texture != 0;
    }

    bool load_fonts()
    {
        const bool regular = load_face(&regular_, &fonts_.regular,
                                       {"barlow-regular.huifont", "inter-regular.huifont"});
        const bool semibold = load_face(&semibold_, &fonts_.semibold,
                                        {"barlow-semibold.huifont", "barlow-medium.huifont",
                                         "inter-semibold.huifont"});
        const bool display = load_face(&display_, &fonts_.display,
                                       {"barlow-condensed-semibold.huifont",
                                        "barlow-condensed-medium.huifont", "montserrat-medium.huifont"});
        if (!regular || !semibold || !display)
            return false;
        fonts_.mono = fonts_.regular;
        fonts_.pixel = fonts_.semibold;
        fonts_.hand = fonts_.regular;
        load_face(&cjk_face_, &cjk_, {"noto-cjk-fallback.huifont"});
        load_face(&arabic_face_, &arabic_, {"noto-arabic-fallback.huifont"});
        return true;
    }

    const ui::FontRef &font_for(std::string_view value, const ui::FontRef &preferred) const
    {
        std::size_t index = 0;
        while (index < value.size())
        {
            const std::uint32_t codepoint = gfx::next_codepoint(value, &index);
            const bool cjk = (codepoint >= 0x2E80 && codepoint <= 0x9FFF) ||
                             (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
                             (codepoint >= 0x3040 && codepoint <= 0x30FF);
            const bool arabic = codepoint >= 0x0600 && codepoint <= 0x06FF;
            if (cjk && cjk_.font != nullptr)
                return cjk_;
            if (arabic && arabic_.font != nullptr)
                return arabic_;
        }
        return preferred;
    }

    void dynamic_text(std::string_view value, float x, float y, float size, gfx::Color color,
                      float max_width, bool display = true,
                      gfx::Align align = gfx::Align::left)
    {
        const ui::FontRef &preferred = display ? fonts_.display : fonts_.regular;
        const ui::FontRef &face = font_for(value, preferred);
        ui::text(scene_, face, face.font->fit(value, size, max_width), x, y, size, color, align);
    }

    void dynamic_paragraph(std::string_view value, float x, float y, float size, float width,
                           float line_height, gfx::Color color, int lines,
                           gfx::Align align = gfx::Align::left)
    {
        const ui::FontRef &face = font_for(value, fonts_.regular);
        ui::paragraph(scene_, face, value, x, y, size, width, line_height, color, lines, align);
    }

    std::uint32_t load_texture(const std::filesystem::path &path)
    {
        if (path.empty())
            return 0;
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc *pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (pixels == nullptr)
            return 0;
        const std::uint32_t texture = renderer_.batch().create_texture(width, height, pixels);
        stbi_image_free(pixels);
        return texture;
    }

    void load_art()
    {
        for (std::size_t index = 0; index < art_.size(); ++index)
            art_[index] = load_texture(asset("art/poster" + std::to_string(index + 1) + ".png"));
        hero_ = load_texture(asset("art/hero.png"));
    }

    void configure_components()
    {
        tabs_.style.theme = theme_;
        tabs_.style.theme.primary = kText.with_alpha(0.12F);
        tabs_.style.theme.on_primary = kText;
        tabs_.style.kind = ui::TabKind::boxed;
        tabs_.style.height = 56.0F;
        tabs_.style.gap = 14.0F;
        tabs_.style.padding = 20.0F;
        tabs_.style.text_size = 29.0F;
        tabs_.style.track = false;
        tabs_.style.focus_ring = false;
        tabs_.style.on_page = true;
        tabs_.set_tabs({{"Home"}, {"Library"}, {"Downloads", 1}, {"Settings"}});
        tabs_.set_bounds({96.0F, 54.0F, 760.0F, 56.0F});

        grid_.style.theme = theme_;
        grid_.style.columns = 6;
        grid_.style.cell_width = 210.0F;
        grid_.style.cell_height = 315.0F;
        grid_.style.gap_x = 24.0F;
        grid_.style.gap_y = 24.0F;
        grid_.style.padding = 7.0F;
        grid_.style.scroll_thumb = false;
        grid_.style.edge_fade = 0.0F;
        grid_.style.entrance_step = 0.0F;
        grid_.style.card.text = ui::CardText::none;
        grid_.style.card.focus_scale = 1.08F;
        grid_.style.card.lift = 0.0F;
        grid_.style.card.radius = 8.0F;
        // The third row continues beneath the y=986 legend, matching the pinned
        // reference rather than reserving a separate blank footer strip.
        grid_.set_bounds({437.0F, 143.0F, 1394.0F, 883.0F});

        list_.style.theme = theme_;
        list_.style.panel = false;
        list_.style.cards = true;
        list_.style.row_height = 90.0F;
        list_.style.header_height = 48.0F;
        list_.style.gap = 10.0F;
        list_.style.title_size = 28.0F;
        list_.style.subtitle_size = 20.0F;
        list_.style.value_size = 23.0F;
        list_.style.entrance_step = 0.0F;
        list_.style.highlight.kind = ui::HighlightKind::ring;

        keyboard_.style.theme = theme_;
        keyboard_.style.reduced_motion = false;
        keyboard_.style.key_width = 76.0F;
        keyboard_.style.key_height = 66.0F;
        keyboard_.style.gap = 10.0F;
        keyboard_.style.label_size = 28.0F;
        keyboard_.style.panel = true;
        keyboard_.set_bounds({500.0F, 466.0F, 920.0F, 440.0F});
    }

    void draw_background()
    {
        scene_.rounded_rect({0.0F, 0.0F, kWidth, kHeight}, 0.0F, kBackground);
    }

    void draw_top_navigation(ui::Canvas &canvas, Route route, bool reduced_motion)
    {
        int selected = 0;
        if (route == Route::Library)
            selected = 1;
        else if (route == Route::Downloads)
            selected = 2;
        else if (route == Route::Settings)
            selected = 3;
        tabs_.style.reduced_motion = reduced_motion;
        tabs_.set_active(selected, true);
        tabs_.set_focused(false);
        tabs_.update(1.0F / 60.0F);
        tabs_.draw(canvas);

        scene_.circle(1680.0F, 82.0F, 20.0F, kAccent);
        ui::text(scene_, fonts_.semibold, "Leo", 1714.0F, 91.0F, 24.0F, kText);
        ui::text(scene_, fonts_.regular, "21:42", 1824.0F, 91.0F, 22.0F, kSecondary,
                 gfx::Align::right);
    }

    void section_label(std::string_view value, float x, float y)
    {
        ui::text(scene_, fonts_.display, ui::upper(value), x, y, 22.0F, kSecondary,
                 gfx::Align::left, 2.6F);
    }

    void draw_title(const ViewData &view, float x = kSafeX, float y = 212.0F, float width = 760.0F)
    {
        ui::text(scene_, fonts_.display, fit(fonts_.display, view.title, 68.0F, width), x, y, 68.0F,
                 kText);
        if (!view.subtitle.empty())
            ui::paragraph(scene_, fonts_.regular, view.subtitle, x, y + 58.0F, 27.0F, width, 37.0F,
                          kSecondary, 4);
    }

    void draw_action(const ViewRow &row, const gfx::Rect &rect, bool focused, bool primary = false)
    {
        ui::Painter painter(scene_, fonts_, theme_, renderer_.glass_texture());
        ui::Look look{focused ? 1.0F : 0.0F, 0.0F, !row.enabled};
        painter.button(rect, fit(fonts_.display, row.title, primary ? 34.0F : 27.0F, rect.w - 36.0F),
                       primary ? ui::ButtonKind::primary : ui::ButtonKind::secondary, look);
        if (!row.value.empty())
            ui::text(scene_, fonts_.regular, row.value, rect.x + rect.w - 20.0F, rect.cy() + 7.0F,
                     21.0F, primary ? theme_.on_primary.with_alpha(0.8F) : kSecondary,
                     gfx::Align::right);
    }

    void draw_actions(const ViewData &view, float x, float y, float width, bool large_first = true)
    {
        std::vector<const ViewRow *> actions;
        if (!view.actions.empty())
        {
            for (const ViewRow &row : view.actions)
                actions.push_back(&row);
        }
        else
        {
            for (const ViewRow &row : view.rows)
            {
                if (row.kind == RowKind::Action)
                    actions.push_back(&row);
            }
        }
        const int focused = view.actions.empty() ? view.focused_row : view.focused_action;
        for (std::size_t index = 0; index < actions.size(); ++index)
        {
            const float height = large_first && index == 0 ? 84.0F : 64.0F;
            draw_action(*actions[index], {x, y, width, height},
                        static_cast<int>(index) == focused, index == 0);
            y += height + 16.0F;
        }
    }

    void draw_startup(ui::Canvas &, const ViewData &view, const State &state)
    {
        section_label("Playden", 96.0F, 78.0F);
        draw_title(view, 96.0F, 292.0F, 720.0F);
        draw_actions(view, 96.0F, 520.0F, 560.0F);

        scene_.rounded_rect({988.0F, 194.0F, 724.0F, 680.0F}, 14.0F, kPanel);
        scene_.glow({1050.0F, 268.0F, 600.0F, 360.0F}, 12.0F, 42.0F, kAccent.with_alpha(0.08F));
        if (hero_ != 0)
            scene_.image_gradient(hero_, {1050.0F, 268.0F, 600.0F, 360.0F}, gfx::kFullUv,
                                  kText.with_alpha(0.75F), kBackground.with_alpha(0.90F), 12.0F);
        else
            scene_.gradient_rect({1050.0F, 268.0F, 600.0F, 360.0F}, 12.0F,
                                 kAccent.with_alpha(0.25F), kPanel);
        ui::text(scene_, fonts_.display, "YOUR GAMES", 1100.0F, 704.0F, 48.0F, kText);
        ui::text(scene_, fonts_.regular,
                 state.persistence_available ? "Ready when you are." : "Browsing can continue without saving.",
                 1100.0F, 752.0F, 25.0F, state.persistence_available ? kSecondary : kAmber);
    }

    void draw_qr(float x, float y, float side)
    {
        scene_.rounded_rect({x, y, side, side}, 16.0F, kText);
        constexpr int cells = 21;
        const float cell = (side - 48.0F) / static_cast<float>(cells);
        for (int row = 0; row < cells; ++row)
        {
            for (int column = 0; column < cells; ++column)
            {
                const bool finder = ((row < 7 || row >= cells - 7) && column < 7) ||
                                    (row < 7 && column >= cells - 7);
                const bool bit = finder ? (row % 6 != 1 && column % 6 != 1) :
                                          ((row * 17 + column * 31 + row * column) % 7 < 3);
                if (bit)
                    scene_.rounded_rect({x + 24.0F + column * cell, y + 24.0F + row * cell,
                                         std::ceil(cell), std::ceil(cell)},
                                        0.0F, kBackground);
            }
        }
    }

    void draw_sign_in(ui::Canvas &, const ViewData &view, const State &state)
    {
        section_label("Connect Steam", 96.0F, 78.0F);
        draw_title(view, 96.0F, 272.0F, 720.0F);
        draw_actions(view, 96.0F, 594.0F, 540.0F);
        draw_qr(1116.0F, 224.0F, 430.0F);
        scene_.rounded_rect({1185.0F, 678.0F, 292.0F, 42.0F}, 21.0F, kAmber.with_alpha(0.18F));
        ui::text(scene_, fonts_.semibold, "SIMULATED QR — NOT LIVE", 1331.0F, 706.0F, 18.0F,
                 kAmber, gfx::Align::center);
        const char *status = "Waiting for approval";
        gfx::Color color = kAccent;
        if (state.account.authentication == AuthState::Expired)
        {
            status = "Challenge expired";
            color = kAmber;
        }
        else if (state.account.authentication == AuthState::Failed)
        {
            status = state.account.error.empty() ? "Connection failed" : state.account.error.c_str();
            color = kRed;
        }
        else if (state.account.authentication == AuthState::Syncing)
        {
            status = "Approved · Syncing library";
            color = kGreen;
        }
        scene_.circle(1198.0F, 772.0F, 7.0F, color);
        ui::text(scene_, fonts_.regular, status, 1220.0F, 780.0F, 25.0F, kText);
    }

    void draw_home(ui::Canvas &, Simulation &simulation)
    {
        const ViewData &view = simulation.view();
        section_label(view.title.empty() ? "Home" : view.title, 96.0F, 168.0F);
        const auto games = simulation.visible_games();
        if (games.empty())
        {
            ViewData copy = view;
            draw_title(copy, 96.0F, 360.0F, 820.0F);
            draw_actions(copy, 96.0F, 560.0F, 520.0F);
            return;
        }

        const std::array<const char *, 2> shelf_names = {"Continue playing", "Downloading & recent"};
        std::size_t game_index = 0;
        for (int shelf = 0; shelf < 2; ++shelf)
        {
            const float top = 205.0F + shelf * 380.0F;
            section_label(shelf_names[static_cast<std::size_t>(shelf)], 96.0F, top);
            for (int column = 0; column < 7 && game_index < games.size(); ++column, ++game_index)
            {
                const Game &game = *games[game_index];
                const gfx::Rect art{96.0F + column * 233.0F, top + 28.0F, 213.0F, 320.0F};
                const bool focused = static_cast<int>(game_index) == view.focused_row;
                draw_poster(game, art, focused, {}, -1.0F, false);
            }
        }
    }

    void draw_library(ui::Canvas &canvas, Simulation &simulation)
    {
        const State &state = simulation.state();
        const ViewData &view = simulation.view();
        const auto games = simulation.visible_games();
        draw_library_rail(view, state, games.size());

        if (games.empty())
        {
            ui::text(scene_, fonts_.display, view.title.empty() ? "No games here" : view.title,
                     1134.0F, 434.0F, 56.0F, kText, gfx::Align::center);
            ui::paragraph(scene_, fonts_.regular,
                          view.subtitle.empty() ? "Clear the search or choose another library view."
                                                : view.subtitle,
                          784.0F, 486.0F, 26.0F, 700.0F, 36.0F, kSecondary, 3,
                          gfx::Align::center);
            draw_actions(view, 914.0F, 620.0F, 440.0F);
            return;
        }

        visible_grid_games_ = games;
        grid_.style.reduced_motion = state.reduced_motion;
        grid_.set_count(static_cast<int>(games.size()));
        grid_.set_focus(std::clamp(view.focused_row, 0, static_cast<int>(games.size()) - 1), true);
        grid_.set_active(true);
        grid_.content = [this](ui::Canvas &, const gfx::Rect &cell, const ui::CardItem &, int index,
                              float focus) {
            const Game &game = *visible_grid_games_[static_cast<std::size_t>(index)];
            draw_poster(game, cell, focus > 0.5F, {}, job_progress(game.id));
        };
        grid_.accent = [this](int index) {
            return id_color(visible_grid_games_[static_cast<std::size_t>(index)]->id);
        };
        grid_.update(1.0F / 60.0F);
        grid_.draw(canvas);
    }

    void draw_library_rail(const ViewData &view, const State &state, std::size_t count)
    {
        const std::array<std::pair<const char *, const char *>, 6> entries = {{{"Installed", "14"},
                                                                               {"All", "24"},
                                                                               {"Favorites", "8"},
                                                                               {"Hidden", "2"},
                                                                               {"Short sessions", "11"},
                                                                               {"New collection", "+"}}};
        const int selected = state.library.scope == "installed" ? 0 : state.library.scope == "favorites" ? 2 :
                             state.library.scope == "hidden" ? 3 : 1;
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            const float y = 155.0F + index * 66.0F;
            if (static_cast<int>(index) == selected)
                scene_.rounded_rect({96.0F, y, 300.0F, 60.0F}, 8.0F, kText.with_alpha(0.10F));
            ui::text(scene_, fonts_.display, entries[index].first, 118.0F, y + 39.0F, 27.0F,
                     static_cast<int>(index) == selected ? kText : kSecondary);
            const std::string value = index == 1 ? std::to_string(count) : entries[index].second;
            ui::text(scene_, fonts_.regular, value, 374.0F, y + 38.0F, 20.0F, kMuted,
                     gfx::Align::right);
        }
        if (!state.library.query.empty())
        {
            scene_.rounded_rect({96.0F, 600.0F, 300.0F, 62.0F}, 8.0F, kPanel);
            ui::text(scene_, fonts_.regular, fit(fonts_.regular, state.library.query, 23.0F, 240.0F),
                     122.0F, 640.0F, 23.0F, kText);
        }
        if (!view.subtitle.empty())
            ui::paragraph(scene_, fonts_.regular, view.subtitle, 96.0F, 722.0F, 21.0F, 300.0F,
                          29.0F, kSecondary, 4);
    }

    float job_progress(GameId id) const
    {
        if (current_state_ == nullptr)
            return -1.0F;
        const auto found = std::find_if(current_state_->jobs.begin(), current_state_->jobs.end(),
                                        [id](const Job &job) { return job.game_id == id; });
        if (found == current_state_->jobs.end() || found->total_bytes == 0)
            return -1.0F;
        return static_cast<float>(found->completed_bytes) / static_cast<float>(found->total_bytes);
    }

    void draw_poster(const Game &game, const gfx::Rect &rect, bool focused, std::string_view badge,
                     float progress, bool title_overlay = true)
    {
        const std::size_t texture_index = game.id >= 900001 ? (game.id - 900001) % art_.size() :
                                                              game.id % art_.size();
        const std::uint32_t texture = game.art_failed ? 0 : art_[texture_index];
        scene_.shadow(rect, 8.0F, focused ? 26.0F : 12.0F,
                      gfx::Color::rgb(0x000000, focused ? 0.65F : 0.35F));
        if (texture != 0)
            scene_.image(texture, rect, gfx::kFullUv, kText, 8.0F);
        else
        {
            scene_.gradient_rect(rect, 8.0F, id_color(game.id), gfx::Color::rgb(0x24201D));
            const ui::FontRef &face = font_for(game.title, fonts_.display);
            ui::paragraph(scene_, face, game.title, rect.x + 18.0F, rect.y + rect.h * 0.42F,
                          28.0F, rect.w - 36.0F, 31.0F, kText, 4, gfx::Align::center);
        }
        if (focused && title_overlay)
        {
            scene_.gradient_rect({rect.x, rect.y + rect.h - 98.0F, rect.w, 98.0F}, 8.0F,
                                 kBackground.with_alpha(0.78F), kBackground.with_alpha(0.98F));
            dynamic_text(game.title, rect.x + 13.0F, rect.y + rect.h - 45.0F, 23.0F, kText,
                         rect.w - 26.0F);
        }
        if (focused)
        {
            scene_.glow(rect.inset(-6.0F), 13.0F, 24.0F, kAccent.with_alpha(0.38F));
            scene_.bordered_rect(rect.inset(-6.0F), 13.0F, kBackground.with_alpha(0.0F), 4.0F,
                                 kAccent);
        }
        if (!badge.empty())
        {
            scene_.rounded_rect({rect.x + 10.0F, rect.y + 10.0F, 112.0F, 32.0F}, 6.0F,
                                kBackground.with_alpha(0.88F));
            ui::text(scene_, fonts_.semibold, badge, rect.x + 22.0F, rect.y + 33.0F, 16.0F, kText);
        }
        if (progress >= 0.0F)
        {
            scene_.rounded_rect({rect.x + 12.0F, rect.y + rect.h - 15.0F, rect.w - 24.0F, 6.0F},
                                3.0F, kText.with_alpha(0.20F));
            scene_.rounded_rect({rect.x + 12.0F, rect.y + rect.h - 15.0F,
                                 (rect.w - 24.0F) * std::clamp(progress, 0.0F, 1.0F), 6.0F},
                                3.0F, kAccent);
        }
    }

    std::vector<ui::ListItem> list_items(const ViewData &view) const
    {
        std::vector<ui::ListItem> items;
        items.reserve(view.rows.size());
        for (const ViewRow &row : view.rows)
        {
            ui::ListItem item;
            item.title = row.title;
            item.subtitle = row.subtitle;
            item.value = row.value;
            item.badge = row.badge;
            item.disabled = !row.enabled;
            item.header = row.kind == RowKind::Heading || row.kind == RowKind::Separator;
            item.chevron = row.kind == RowKind::Choice || row.kind == RowKind::Profile ||
                           !row.action.empty();
            items.push_back(std::move(item));
        }
        return items;
    }

    void draw_view_list(ui::Canvas &canvas, const ViewData &view, const gfx::Rect &bounds,
                        float row_height = 90.0F)
    {
        list_.style.row_height = row_height;
        list_.set_items(list_items(view));
        list_.set_bounds(bounds);
        list_.set_focus(std::clamp(view.focused_row, 0, std::max(0, static_cast<int>(view.rows.size()) - 1)),
                        true);
        list_.set_active(true);
        list_.update(1.0F / 60.0F);
        list_.draw(canvas);

        for (std::size_t index = 0; index < view.rows.size(); ++index)
        {
            const ViewRow &row = view.rows[index];
            if (row.progress <= 0.0F && row.kind != RowKind::Job && row.kind != RowKind::Volume)
                continue;
            const gfx::Rect row_rect = list_.row_rect(static_cast<int>(index));
            if (row_rect.y + row_rect.h < bounds.y || row_rect.y > bounds.y + bounds.h)
                continue;
            ui::ProgressBar progress;
            progress.style.theme = theme_;
            progress.style.height = 7.0F;
            progress.style.placement = ui::LabelPlacement::none;
            progress.style.status = progress_status(row);
            progress.style.finish_flash = false;
            progress.set_bounds({row_rect.x + 24.0F, row_rect.y + row_rect.h - 16.0F,
                                 row_rect.w - 48.0F, 7.0F});
            progress.set_value(std::clamp(row.progress, 0.0F, 1.0F), true);
            progress.draw(canvas);
        }
    }

    void draw_downloads(ui::Canvas &canvas, const ViewData &view, const State &state)
    {
        section_label("Downloads", 96.0F, 170.0F);
        draw_title(view, 96.0F, 246.0F, 980.0F);
        draw_view_list(canvas, view, {96.0F, 350.0F, 1040.0F, 590.0F}, 104.0F);

        scene_.rounded_rect({1190.0F, 196.0F, 634.0F, 744.0F}, 12.0F, kPanel);
        ui::text(scene_, fonts_.display, "Storage", 1230.0F, 258.0F, 38.0F, kText);
        float y = 314.0F;
        for (const Volume &volume : state.volumes)
        {
            const std::uint64_t used = volume.used_bytes + volume.reserved_bytes;
            const float fraction = volume.capacity_bytes == 0 ? 0.0F :
                static_cast<float>(used) / static_cast<float>(volume.capacity_bytes);
            ui::text(scene_, fonts_.display, volume.label, 1230.0F, y, 29.0F, kText);
            ui::text(scene_, fonts_.regular,
                     volume.available ? (volume.writable ? "Available" : "Read only") : "Disconnected",
                     1780.0F, y, 21.0F, volume.available && volume.writable ? kSecondary : kAmber,
                     gfx::Align::right);
            scene_.rounded_rect({1230.0F, y + 24.0F, 550.0F, 10.0F}, 5.0F, kText.with_alpha(0.14F));
            scene_.rounded_rect({1230.0F, y + 24.0F, 550.0F * std::clamp(fraction, 0.0F, 1.0F), 10.0F},
                                5.0F, volume.available ? kAccent : kMuted);
            y += 112.0F;
        }
    }

    void draw_settings(ui::Canvas &canvas, const ViewData &view, const State &state)
    {
        section_label("Settings", 96.0F, 170.0F);
        draw_title(view, 96.0F, 246.0F, 560.0F);
        draw_view_list(canvas, view, {720.0F, 168.0F, 1104.0F, 772.0F}, 84.0F);
        scene_.rounded_rect({96.0F, 494.0F, 530.0F, 188.0F}, 12.0F, kPanel);
        ui::text(scene_, fonts_.display, state.account.display_name.empty() ? "Signed out" : state.account.display_name,
                 132.0F, 552.0F, 34.0F, kText);
        ui::text(scene_, fonts_.regular,
                 state.account.authentication == AuthState::Offline ? "Cached library · Offline" :
                 state.account.authentication == AuthState::SignedIn ? "Steam connected · Synced" : "Connect Steam",
                 132.0F, 594.0F, 23.0F,
                 state.account.authentication == AuthState::Offline ? kAmber : kSecondary);
        ui::text(scene_, fonts_.regular, "Direct ps5-homebrew-ui comparison", 132.0F, 646.0F, 20.0F, kMuted);
    }

    void draw_details(ui::Canvas &, const ViewData &view, const State &state)
    {
        const GameId id = state.navigation.detail_id.value_or(state.navigation.selected_game_id);
        const Game *game = game_by_id(state, id);
        if (hero_ != 0)
            scene_.image_gradient(hero_, {0.0F, 0.0F, 1920.0F, 620.0F}, gfx::kFullUv,
                                  kText.with_alpha(0.72F), kBackground.with_alpha(1.0F));
        else
            scene_.gradient_rect({0.0F, 0.0F, 1920.0F, 620.0F}, 0.0F, id_color(id), kBackground);
        scene_.gradient_rect_h({0.0F, 0.0F, 1180.0F, 620.0F}, 0.0F, kBackground.with_alpha(0.90F),
                               kBackground.with_alpha(0.0F));
        section_label("Game details", 96.0F, 80.0F);
        const std::string title = game == nullptr ? view.title : game->title;
        const ui::FontRef &title_face = font_for(title, fonts_.display);
        ui::paragraph(scene_, title_face, title, 96.0F, 320.0F, 68.0F, 800.0F, 72.0F, kText, 3);
        draw_actions(view, 96.0F, 638.0F, 390.0F, true);

        const std::string description = game == nullptr || game->description.empty() ?
                                            "Description unavailable." : game->description;
        ui::paragraph(scene_, fonts_.regular, description, 550.0F, 670.0F, 25.0F, 820.0F, 35.0F,
                      kSecondary, 7);
        if (game != nullptr)
        {
            scene_.rounded_rect({1420.0F, 638.0F, 404.0F, 300.0F}, 12.0F, kPanel);
            detail_line("Source", game->source, 1452.0F, 700.0F);
            detail_line("Download", byte_count(game->download_bytes), 1452.0F, 760.0F);
            detail_line("Installed", byte_count(game->install_bytes), 1452.0F, 820.0F);
            detail_line("Controller", game->controller_support.empty() ? "Unknown" : game->controller_support,
                        1452.0F, 880.0F);
        }
    }

    void detail_line(std::string_view label, std::string_view value, float x, float y)
    {
        ui::text(scene_, fonts_.regular, label, x, y, 20.0F, kMuted);
        ui::text(scene_, fonts_.semibold, value, x + 338.0F, y, 21.0F, kText, gfx::Align::right);
    }

    void draw_session(ui::Canvas &, const ViewData &view, const State &state)
    {
        if (hero_ != 0)
            scene_.image(hero_, {0.0F, 0.0F, 1920.0F, 1080.0F}, gfx::kFullUv,
                         kText.with_alpha(0.42F));
        scene_.rounded_rect({0.0F, 0.0F, 1920.0F, 1080.0F}, 0.0F, kBackground.with_alpha(0.68F));
        const Game *game = game_by_id(state, state.session_game_id);
        section_label("Simulated game session", 96.0F, 82.0F);
        dynamic_text(game == nullptr ? std::string_view(view.title) : std::string_view(game->title),
                     96.0F, 256.0F, 64.0F, kText, 1000.0F);
        ui::text(scene_, fonts_.display, view.subtitle, 96.0F, 320.0F, 34.0F,
                 state.session == SessionState::Failed || state.session == SessionState::Crashed ? kRed : kSecondary);
        draw_actions(view, 96.0F, 428.0F, 540.0F);

        scene_.rounded_rect({1210.0F, 226.0F, 614.0F, 540.0F}, 16.0F, kPanel.with_alpha(0.94F));
        ui::text(scene_, fonts_.display, "Session controls", 1254.0F, 294.0F, 44.0F, kText);
        ui::paragraph(scene_, fonts_.regular,
                      state.session_error.empty() ? "This is a deterministic fixture. No game process is launched."
                                                  : state.session_error,
                      1254.0F, 352.0F, 25.0F, 522.0F, 36.0F,
                      state.session_error.empty() ? kSecondary : kAmber, 5);
    }

    void draw_persistent_status(ui::Canvas &, const State &state)
    {
        float y = 126.0F;
        for (const Notice &notice : state.notices)
        {
            if (!notice.persistent)
                continue;
            const float width = 560.0F;
            scene_.rounded_rect({kWidth - kSafeX - width, y, width, 54.0F}, 8.0F,
                                kAmber.with_alpha(0.16F));
            scene_.circle(kWidth - kSafeX - width + 25.0F, y + 27.0F, 6.0F, kAmber);
            ui::text(scene_, fonts_.regular, fit(fonts_.regular, notice.message, 21.0F, width - 64.0F),
                     kWidth - kSafeX - width + 44.0F, y + 35.0F, 21.0F, kText);
            y += 64.0F;
        }
    }

    void draw_footer(ui::Canvas &, const ViewData &view)
    {
        std::vector<std::string> fallback;
        const std::vector<std::string> &labels = view.legends.empty() ? fallback : view.legends;
        std::vector<ui::Hint> hints;
        hints.reserve(std::max<std::size_t>(labels.size(), 2));
        if (labels.empty())
        {
            hints.push_back({ui::Button::cross, "Select"});
            hints.push_back({ui::Button::circle, "Back"});
        }
        else
        {
            for (std::size_t index = 0; index < labels.size() && index < 7; ++index)
                hints.push_back({legend_button(labels[index], index), labels[index].c_str()});
        }
        ui::HintLayout layout;
        layout.size = 38.0F;
        layout.text_size = 22.0F;
        layout.cy = kFooterY + 20.0F;
        layout.item_gap = 28.0F;
        layout.font = &fonts_.semibold;
        ui::GlyphStyle glyphs = ui::GlyphStyle::mono(kText, kBackground);
        ui::draw_hints(scene_, fonts_, glyphs, hints.data(), static_cast<int>(hints.size()), 96.0F,
                       false, layout);
        if (view.show_simulation_label)
        {
            scene_.rounded_rect({1652.0F, 989.0F, 172.0F, 34.0F}, 17.0F, kText.with_alpha(0.08F));
            ui::text(scene_, fonts_.semibold, "UI SIMULATION", 1738.0F, 1012.0F, 16.0F, kSecondary,
                     gfx::Align::center, 1.2F);
        }
    }

    void draw_overlays(const State &state, const ViewData &view)
    {
        ui::Canvas overlay{overlay_, fonts_, renderer_.glass_texture(), clock_};
        if (state.editor.active)
        {
            draw_editor_overlay(overlay, state);
            return;
        }
        if (!state.navigation.modals.empty())
        {
            draw_modal_overlay(state.navigation.modals.back(), view);
            return;
        }
        if (view.surface != "home" && view.surface != "library" && view.surface != "downloads" &&
            view.surface != "settings" && view.surface != "details" && view.surface != "qr" &&
            view.surface != "startup" && view.surface != "session")
            draw_sheet_overlay(view);

        float y = 118.0F;
        for (const Notice &notice : state.notices)
        {
            if (notice.persistent)
                continue;
            overlay_.shadow({1288.0F, y, 536.0F, 84.0F}, 10.0F, 28.0F, gfx::Color::rgb(0x000000, 0.6F));
            overlay_.rounded_rect({1288.0F, y, 536.0F, 84.0F}, 10.0F, kPanel);
            overlay_.circle(1320.0F, y + 42.0F, 7.0F, kAccent);
            ui::text(overlay_, fonts_.semibold, fit(fonts_.semibold, notice.message, 22.0F, 440.0F),
                     1342.0F, y + 36.0F, 22.0F, kText);
            if (!notice.action.empty())
                ui::text(overlay_, fonts_.regular, notice.action, 1342.0F, y + 62.0F, 18.0F, kAccent);
            y += 98.0F;
        }
    }

    void draw_editor_overlay(ui::Canvas &canvas, const State &state)
    {
        overlay_.rounded_rect({0.0F, 0.0F, kWidth, kHeight}, 0.0F, gfx::Color::rgb(0x000000, 0.68F));
        overlay_.rounded_rect({420.0F, 116.0F, 1080.0F, 820.0F}, 16.0F, kPanel);
        ui::text(overlay_, fonts_.display,
                 state.editor.purpose.empty() ? "Enter text" : state.editor.purpose,
                 476.0F, 188.0F, 46.0F, kText);
        ui::Painter painter(overlay_, fonts_, theme_, renderer_.glass_texture());
        const gfx::Rect field{476.0F, 232.0F, 968.0F, 76.0F};
        painter.field(field, state.editor.value, false,
                      {1.0F, 0.0F, false});
        const std::size_t caret = std::min(state.editor.caret, state.editor.value.size());
        const float caret_x = field.x + 22.0F +
                              fonts_.regular.measure(std::string_view(state.editor.value).substr(0, caret),
                                                     24.0F);
        overlay_.rounded_rect({std::min(caret_x, field.x + field.w - 24.0F), field.y + 18.0F,
                               2.0F, field.h - 36.0F},
                              1.0F, kAccent);
        keyboard_.style.reduced_motion = state.reduced_motion;
        keyboard_.set_length(static_cast<int>(state.editor.value.size()));
        keyboard_.set_shift(state.editor.shifted ? ui::KeyboardShift::lock : ui::KeyboardShift::off);
        keyboard_.set_layout(state.editor.symbols ? 1 : 0);
        keyboard_.set_focus(state.editor.focus_row, state.editor.focus_key, true);
        keyboard_.set_active(true);
        keyboard_.update(1.0F / 60.0F);
        keyboard_.draw(canvas);
    }

    void draw_modal_overlay(const ModalEntry &modal, const ViewData &view)
    {
        overlay_.rounded_rect({0.0F, 0.0F, kWidth, kHeight}, 0.0F, gfx::Color::rgb(0x000000, 0.72F));
        const float width = modal.destructive ? 820.0F : 960.0F;
        const gfx::Rect panel{(kWidth - width) * 0.5F, 254.0F, width, 500.0F};
        overlay_.shadow(panel, 16.0F, 40.0F, gfx::Color::rgb(0x000000, 0.65F));
        overlay_.rounded_rect(panel, 16.0F, kPanel);
        ui::text(overlay_, fonts_.display, modal.title, panel.x + 48.0F, panel.y + 76.0F, 48.0F,
                 modal.destructive ? kAmber : kText);
        const std::string body = view.subtitle.empty() ?
            "Review the impact before continuing. This fixture changes simulated state only." : view.subtitle;
        ui::paragraph(overlay_, fonts_.regular, body, panel.x + 48.0F, panel.y + 132.0F, 25.0F,
                      panel.w - 96.0F, 35.0F, kSecondary, 5);
        const float button_width = (panel.w - 112.0F) / std::max<std::size_t>(1, modal.action_keys.size());
        for (std::size_t index = 0; index < modal.action_keys.size(); ++index)
        {
            const gfx::Rect button{panel.x + 48.0F + index * (button_width + 16.0F), panel.y + 388.0F,
                                   button_width, 68.0F};
            ui::Painter painter(overlay_, fonts_, theme_, renderer_.glass_texture());
            painter.button(button, modal.action_keys[index],
                           modal.destructive && index + 1 == modal.action_keys.size() ?
                               ui::ButtonKind::secondary : ui::ButtonKind::primary,
                           {static_cast<int>(index) == modal.focused_action ? 1.0F : 0.0F, 0.0F, false});
        }
    }

    void draw_sheet_overlay(const ViewData &view)
    {
        overlay_.rounded_rect({0.0F, 0.0F, kWidth, kHeight}, 0.0F, gfx::Color::rgb(0x000000, 0.58F));
        const bool wide = view.surface.find("settings") != std::string::npos ||
                          view.surface.find("profile") != std::string::npos;
        const float width = wide ? 960.0F : 640.0F;
        const gfx::Rect panel{kWidth - width, 0.0F, width, kHeight};
        overlay_.shadow({panel.x - 18.0F, 0.0F, panel.w + 18.0F, panel.h}, 0.0F, 36.0F,
                        gfx::Color::rgb(0x000000, 0.75F));
        overlay_.rounded_rect(panel, 0.0F, kPanel);
        section_overlay(view.surface, panel.x + 48.0F, 72.0F);
        ui::text(overlay_, fonts_.display, view.title, panel.x + 48.0F, 142.0F, 48.0F, kText);
        if (!view.subtitle.empty())
            ui::paragraph(overlay_, fonts_.regular, view.subtitle, panel.x + 48.0F, 190.0F, 23.0F,
                          panel.w - 96.0F, 32.0F, kSecondary, 3);
        draw_overlay_rows(view, {panel.x + 40.0F, 292.0F, panel.w - 80.0F, 650.0F});
    }

    void section_overlay(std::string_view value, float x, float y)
    {
        ui::text(overlay_, fonts_.display, ui::upper(value), x, y, 20.0F, kSecondary,
                 gfx::Align::left, 2.4F);
    }

    void draw_overlay_rows(const ViewData &view, const gfx::Rect &bounds)
    {
        float y = bounds.y;
        const int first = std::max(0, view.focused_row - 4);
        const int last = std::min(static_cast<int>(view.rows.size()), first + 8);
        ui::Painter painter(overlay_, fonts_, theme_, renderer_.glass_texture());
        for (int index = first; index < last; ++index)
        {
            const ViewRow &row = view.rows[static_cast<std::size_t>(index)];
            if (row.kind == RowKind::Heading || row.kind == RowKind::Separator)
            {
                ui::text(overlay_, fonts_.display, ui::upper(row.title), bounds.x + 12.0F, y + 28.0F,
                         18.0F, kMuted, gfx::Align::left, 1.8F);
                y += 54.0F;
                continue;
            }
            const gfx::Rect rect{bounds.x, y, bounds.w, 72.0F};
            painter.row(rect, row.title, row.value.empty() ? row.badge : row.value,
                        row.selected ? 1.0F : 0.0F,
                        {index == view.focused_row ? 1.0F : 0.0F, 0.0F, !row.enabled});
            if (row.progress > 0.0F)
                painter.progress({rect.x + 22.0F, rect.y + 58.0F, rect.w - 44.0F, 6.0F}, row.progress);
            y += 80.0F;
        }
    }

    bool ensure_capture(int width, int height) const
    {
        if (capture_framebuffer_ != 0 && capture_width_ == width && capture_height_ == height)
            return true;
        if (capture_color_ != 0)
            glDeleteRenderbuffers(1, &capture_color_);
        if (capture_framebuffer_ != 0)
            glDeleteFramebuffers(1, &capture_framebuffer_);
        capture_color_ = 0;
        capture_framebuffer_ = 0;
        glGenFramebuffers(1, &capture_framebuffer_);
        glGenRenderbuffers(1, &capture_color_);
        glBindRenderbuffer(GL_RENDERBUFFER, capture_color_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, capture_framebuffer_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  capture_color_);
        const bool okay = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!okay)
            return false;
        capture_width_ = width;
        capture_height_ = height;
        return true;
    }

    std::filesystem::path assets_;
    ui::Theme theme_;
    mutable gfx::Renderer renderer_;
    gfx::Font regular_;
    gfx::Font semibold_;
    gfx::Font display_;
    gfx::Font cjk_face_;
    gfx::Font arabic_face_;
    ui::Fonts fonts_;
    ui::FontRef cjk_;
    ui::FontRef arabic_;
    gfx::DrawList scene_;
    gfx::DrawList overlay_;
    ui::TabBar tabs_;
    ui::GridView grid_;
    ui::ListView list_;
    ui::Keyboard keyboard_;
    std::array<std::uint32_t, 8> art_{};
    std::uint32_t hero_ = 0;
    std::vector<const Game *> visible_grid_games_;
    const State *current_state_ = nullptr;
    float clock_ = 0.0F;
    bool ready_ = false;
    bool has_frame_ = false;
    int last_width_ = 0;
    int last_height_ = 0;
    mutable GLuint capture_framebuffer_ = 0;
    mutable GLuint capture_color_ = 0;
    mutable int capture_width_ = 0;
    mutable int capture_height_ = 0;
};

Render::Render(std::filesystem::path assets_directory)
    : impl_(std::make_unique<Impl>(std::move(assets_directory)))
{
}

Render::~Render() = default;
Render::Render(Render &&) noexcept = default;
Render &Render::operator=(Render &&) noexcept = default;

void Render::draw(Simulation &simulation, int width, int height)
{
    impl_->draw(simulation, width, height);
}

bool Render::capture_png(const std::filesystem::path &path, int width, int height) const
{
    return impl_->capture_png(path, width, height);
}

} // namespace playden::homebrew
