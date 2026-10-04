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
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
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
    std::FILE *file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr)
        return false;

    const auto close_file = [&file]() { std::fclose(file); };
    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        close_file();
        return false;
    }
    const long length = std::ftell(file);
    if (length < 0 || static_cast<unsigned long>(length) > std::numeric_limits<std::size_t>::max())
    {
        close_file();
        return false;
    }
    if (std::fseek(file, 0, SEEK_SET) != 0)
    {
        close_file();
        return false;
    }

    std::string bytes(static_cast<std::size_t>(length), '\0');
    const std::size_t read = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
    const bool ok = read == bytes.size() && std::ferror(file) == 0;
    close_file();
    if (!ok)
        return false;
    *out = std::move(bytes);
    return true;
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
    std::array<char, 32> value{};
    const int precision = *bytes >= static_cast<std::uint64_t>(10.0 * gib) ? 0 : 1;
    const int written = std::snprintf(value.data(), value.size(), "%.*f GB", precision,
                                      static_cast<double>(*bytes) / gib);
    return written > 0 ? std::string(value.data(), static_cast<std::size_t>(written)) : "Unknown";
}

std::string transfer_rate(std::optional<std::uint64_t> bytes_per_second)
{
    if (!bytes_per_second)
        return "—";
    std::array<char, 32> value{};
    const double mib = static_cast<double>(*bytes_per_second) / (1024.0 * 1024.0);
    const int written = std::snprintf(value.data(), value.size(), "%.1f MB/s", mib);
    return written > 0 ? std::string(value.data(), static_cast<std::size_t>(written)) : "—";
}

std::string join_words(const std::vector<std::string> &words)
{
    if (words.empty())
        return "Unknown";
    std::string result;
    for (const std::string &word : words)
    {
        if (!result.empty())
            result += " · ";
        result += word;
    }
    return result;
}

std::string playtime_label(std::optional<std::uint64_t> minutes)
{
    if (!minutes)
        return "Unknown";
    if (*minutes < 60)
        return std::to_string(*minutes) + " min";
    return std::to_string(*minutes / 60) + " h " + std::to_string(*minutes % 60) + " min";
}

std::string relative_time(std::optional<std::int64_t> timestamp, std::int64_t now)
{
    if (!timestamp)
        return "Unknown";
    const std::int64_t seconds = std::max<std::int64_t>(0, now - *timestamp);
    if (seconds < 3600)
        return "Less than an hour ago";
    if (seconds < 86400)
        return std::to_string(seconds / 3600) + " h ago";
    return std::to_string(seconds / 86400) + " d ago";
}

std::size_t utf8_boundary(std::string_view text, std::size_t offset)
{
    offset = std::min(offset, text.size());
    while (offset > 0 && offset < text.size() &&
           (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U)
        --offset;
    return offset;
}

const char *rating_label(Compatibility rating)
{
    switch (rating)
    {
    case Compatibility::Untested:
        return "Untested";
    case Compatibility::Works:
        return "Works · user rating";
    case Compatibility::Playable:
        return "Playable · user rating";
    case Compatibility::Broken:
        return "Broken · user rating";
    }
    return "Untested";
}

const char* job_stage_label(JobStage stage)
{
    switch (stage)
    {
    case JobStage::Download:
        return "Download";
    case JobStage::Verify:
        return "Verification";
    case JobStage::Install:
        return "Install";
    case JobStage::Uninstall:
        return "Removal";
    }
    return "Transfer";
}

const char* job_state_label(JobState state)
{
    switch (state)
    {
    case JobState::Queued:
        return "Queued";
    case JobState::Downloading:
        return "Downloading";
    case JobState::Pausing:
        return "Pausing";
    case JobState::Paused:
        return "Paused";
    case JobState::Verifying:
        return "Verifying";
    case JobState::Downloaded:
        return "Complete";
    case JobState::Failed:
        return "Failed";
    case JobState::Cancelled:
        return "Cancelled";
    case JobState::Interrupted:
        return "Interrupted";
    }
    return "Transfer";
}

ui::Button legend_button(std::string_view legend, std::size_t index)
{
    if (legend.rfind("Cross", 0) == 0)
        return ui::Button::cross;
    if (legend.rfind("Circle", 0) == 0)
        return ui::Button::circle;
    if (legend.rfind("Square", 0) == 0)
        return ui::Button::square;
    if (legend.rfind("Triangle", 0) == 0)
        return ui::Button::triangle;
    if (legend.rfind("Options", 0) == 0)
        return ui::Button::options;
    if (legend.find("Favorite") != std::string_view::npos)
        return ui::Button::square;
    if (legend.find("Backspace") != std::string_view::npos)
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
        stats_.grid_focused_game_id = 0;
        stats_.grid_drawn_cell_count = 0;
        stats_.native_width = width;
        stats_.native_height = height;
        const State &state = simulation.state();
        const ViewData &view = simulation.view();
        const ViewData &route_view = simulation.route_view();
        const std::string overlay_key = state.editor.active ? "keyboard" :
            state.navigation.modals.empty() ? std::string{} : state.navigation.modals.back().id;
        if (overlay_key != overlay_key_)
        {
            overlay_key_ = overlay_key;
            overlay_progress_ = state.reduced_motion || overlay_key.empty() ? 1.0F : 0.0F;
        }
        else
            overlay_progress_ = state.reduced_motion ? 1.0F :
                std::min(1.0F, overlay_progress_ + (1.0F / 60.0F) / 0.22F);
        route_focus_ = !state.editor.active && state.navigation.modals.empty();
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
            draw_startup(scene, route_view, state);
            break;
        case Route::SignIn:
            draw_sign_in(scene, route_view, state);
            break;
        case Route::Home:
            draw_home(scene, simulation);
            break;
        case Route::Library:
            draw_library(scene, simulation);
            break;
        case Route::Downloads:
            draw_downloads(scene, route_view, state);
            break;
        case Route::Settings:
            draw_settings(scene, route_view, state);
            break;
        case Route::Details:
            draw_details(scene, route_view, state);
            break;
        case Route::Session:
            draw_session(scene, route_view, state);
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

    [[nodiscard]] RenderStats diagnostics() const noexcept
    {
        return stats_;
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
        stats_.artwork_texture_count = static_cast<std::size_t>(std::count_if(
            art_.begin(), art_.end(), [](std::uint32_t texture) { return texture != 0; })) +
            (hero_ == 0 ? 0U : 1U);
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
            const bool arabic = codepoint >= 0x0600 && codepoint <= 0x06FF;
            if (arabic && arabic_.font != nullptr)
                return arabic_;
            if (!preferred.font->has_glyph(codepoint) && cjk_.font != nullptr &&
                cjk_.font->has_glyph(codepoint))
                return cjk_;
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
        tabs_.style.theme.label = ui::FontRole::display;
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
        grid_.style.padding = 27.0F;
        grid_.style.scroll_thumb = false;
        grid_.style.edge_fade = 0.0F;
        grid_.style.entrance_step = 0.0F;
        grid_.style.card.text = ui::CardText::none;
        grid_.style.card.focus_scale = 1.08F;
        grid_.style.card.lift = 0.0F;
        grid_.style.card.radius = 8.0F;
        // The third row continues beneath the y=986 legend, matching the pinned
        // reference rather than reserving a separate blank footer strip.
        grid_.set_bounds({417.0F, 123.0F, 1434.0F, 903.0F});

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
        ui::Look look{focused && route_focus_ ? 1.0F : 0.0F, 0.0F, !row.enabled};
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
        const ViewData &view = simulation.route_view();
        const State &state = simulation.state();
        section_label(view.title.empty() ? "Home" : view.title, 96.0F, 168.0F);
        if (view.shelves.empty())
        {
            ViewData copy = view;
            draw_title(copy, 96.0F, 360.0F, 820.0F);
            draw_actions(copy, 96.0F, 560.0F, 520.0F);
            return;
        }

        const int active_shelf = std::clamp(state.navigation.home_shelf, 0,
                                            static_cast<int>(view.shelves.size()) - 1);
        const int first_shelf = std::clamp(active_shelf > 0 ? active_shelf - 1 : 0, 0,
                                           std::max(0, static_cast<int>(view.shelves.size()) - 2));
        for (int visible = 0; visible < 2 && first_shelf + visible < static_cast<int>(view.shelves.size());
             ++visible)
        {
            const int shelf_index = first_shelf + visible;
            const ViewData::Shelf &shelf = view.shelves[static_cast<std::size_t>(shelf_index)];
            const float top = 205.0F + visible * 380.0F;
            section_label(shelf.title, 96.0F, top);
            const auto remembered = state.navigation.home_shelf_focus.find(shelf.key);
            const int focused_item = remembered == state.navigation.home_shelf_focus.end() ?
                                         shelf.focused_item : remembered->second;
            for (int column = 0; column < 7 && column < static_cast<int>(shelf.items.size()); ++column)
            {
                const Game *game = game_by_id(state, shelf.items[static_cast<std::size_t>(column)].game_id);
                if (game == nullptr)
                    continue;
                const gfx::Rect art{96.0F + column * 233.0F, top + 28.0F, 213.0F, 320.0F};
                const bool focused = route_focus_ && shelf_index == active_shelf && column == focused_item;
                draw_poster(*game, art, focused, shelf.items[static_cast<std::size_t>(column)].badge,
                            job_progress(game->id), false);
            }
        }
    }

    void draw_library(ui::Canvas &canvas, Simulation &simulation)
    {
        const State &state = simulation.state();
        const ViewData &view = simulation.route_view();
        const auto games = simulation.visible_games();
        draw_library_rail(view, state);

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
        const bool grid_active = route_focus_ && !state.navigation.library_rail_focused;
        grid_.style.theme.focus = grid_active ? kAccent : kAccent.with_alpha(0.0F);
        grid_.set_count(static_cast<int>(games.size()));
        int grid_focus = std::clamp(state.navigation.selected_index, 0,
                                    static_cast<int>(games.size()) - 1);
        const auto selected = std::find_if(games.begin(), games.end(), [&state](const Game *game) {
            return game->id == state.navigation.selected_game_id;
        });
        if (selected != games.end())
            grid_focus = static_cast<int>(std::distance(games.begin(), selected));
        stats_.grid_focused_game_id = games[static_cast<std::size_t>(grid_focus)]->id;
        grid_.set_focus(grid_focus, true);
        grid_.set_active(grid_active);
        grid_.content = [this](ui::Canvas &, const gfx::Rect &cell, const ui::CardItem &, int index,
                              float focus) {
            ++stats_.grid_drawn_cell_count;
            const Game &game = *visible_grid_games_[static_cast<std::size_t>(index)];
            std::string badge;
            const auto job = current_state_ == nullptr ? std::vector<Job>::const_iterator{} :
                std::find_if(current_state_->jobs.begin(), current_state_->jobs.end(),
                             [&game](const Job &candidate) {
                                 return candidate.game_id == game.id && candidate.state != JobState::Cancelled;
                             });
            if (current_state_ != nullptr && job != current_state_->jobs.end())
                badge = job_state_label(job->state);
            else if (current_state_ != nullptr &&
                     std::any_of(current_state_->installations.begin(), current_state_->installations.end(),
                                 [&game](const Installation &installation) {
                                     return installation.game_id == game.id && installation.available;
                                 }))
                badge = "Installed";
            draw_poster(game, cell, focus > 0.5F, badge, job_progress(game.id));
        };
        grid_.accent = [this](int index) {
            return id_color(visible_grid_games_[static_cast<std::size_t>(index)]->id);
        };
        grid_.update(1.0F / 60.0F);
        grid_.draw(canvas);
        if (state.library_syncing)
        {
            scene_.rounded_rect({1398.0F, 86.0F, 426.0F, 48.0F}, 8.0F, kPanel);
            scene_.circle(1424.0F, 110.0F, 6.0F, kAccent);
            ui::text(scene_, fonts_.regular, "Refreshing library · focus preserved", 1444.0F,
                     118.0F, 19.0F, kSecondary);
        }
    }

    void draw_library_rail(const ViewData &view, const State &state)
    {
        struct RailEntry
        {
            std::string title;
            std::string value;
            bool selected = false;
        };
        const std::size_t installed = std::count_if(
            state.installations.begin(), state.installations.end(),
            [](const Installation &installation) { return installation.available; });
        std::vector<RailEntry> entries = {
            {"Installed", std::to_string(installed), state.library.scope == "installed"},
            {"All", std::to_string(state.games.size()), state.library.scope == "all"},
            {"Favorites", std::to_string(state.library.favorites.size()),
             state.library.scope == "favorites"},
            {"Hidden", std::to_string(state.library.hidden.size()), state.library.scope == "hidden"}};
        for (const Collection &collection : state.library.collections)
            entries.push_back({collection.name, std::to_string(collection.members.size()),
                               state.library.scope == collection.id});
        entries.push_back({"New collection", "+", false});
        const int selected = state.library.scope == "installed" ? 0 :
                             state.library.scope == "all" ? 1 :
                             state.library.scope == "favorites" ? 2 :
                             state.library.scope == "hidden" ? 3 : -1;
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            const float y = 155.0F + index * 66.0F;
            const bool focused = route_focus_ && state.navigation.library_rail_focused &&
                                 static_cast<int>(index) == state.navigation.selected_index;
            if (entries[index].selected || static_cast<int>(index) == selected)
                scene_.rounded_rect({96.0F, y, 300.0F, 60.0F}, 8.0F, kText.with_alpha(0.10F));
            if (focused)
                scene_.bordered_rect({96.0F, y, 300.0F, 60.0F}, 8.0F,
                                     kBackground.with_alpha(0.0F), 4.0F, kAccent);
            ui::text(scene_, fonts_.display,
                     fit(fonts_.display, entries[index].title, 27.0F, 225.0F), 118.0F, y + 39.0F,
                     27.0F, entries[index].selected || static_cast<int>(index) == selected ? kText
                                                                                           : kSecondary);
            ui::text(scene_, fonts_.regular, entries[index].value, 374.0F, y + 38.0F, 20.0F, kMuted,
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
        const std::uint32_t texture = game.art_failed || game.art_pending ? 0 : art_[texture_index];
        scene_.shadow(rect, 8.0F, focused ? 26.0F : 12.0F,
                      gfx::Color::rgb(0x000000, focused ? 0.65F : 0.35F));
        if (texture != 0)
            scene_.image(texture, rect, gfx::kFullUv, kText, 8.0F);
        else
        {
            scene_.gradient_rect(rect, 8.0F, id_color(game.id), gfx::Color::rgb(0x24201D));
            if (game.art_pending)
            {
                ui::text(scene_, fonts_.display, "Artwork loading", rect.cx(), rect.y + rect.h * 0.52F,
                         25.0F, kText, gfx::Align::center);
                scene_.rounded_rect({rect.x + 12.0F, rect.y + 12.0F, rect.w - 24.0F, 30.0F}, 6.0F,
                                    kBackground.with_alpha(0.82F));
                ui::text(scene_, fonts_.semibold, "ARTWORK PENDING", rect.cx(), rect.y + 33.0F,
                         14.0F, kSecondary, gfx::Align::center, 1.0F);
            }
            else
            {
                const ui::FontRef &face = font_for(game.title, fonts_.display);
                ui::paragraph(scene_, face, game.title, rect.x + 18.0F, rect.y + rect.h * 0.42F,
                              28.0F, rect.w - 36.0F, 31.0F, kText, 4, gfx::Align::center);
            }
        }
        if (focused && title_overlay && !game.art_pending)
        {
            scene_.gradient_rect({rect.x, rect.y + rect.h - 98.0F, rect.w, 98.0F}, 8.0F,
                                 kBackground.with_alpha(0.96F), kBackground);
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
            ui::text(scene_, fonts_.semibold, fit(fonts_.semibold, badge, 16.0F, 90.0F),
                     rect.x + 22.0F, rect.y + 33.0F, 16.0F, kText);
        }
        if (current_state_ != nullptr)
        {
            const auto installation = std::find_if(current_state_->installations.begin(),
                                                    current_state_->installations.end(),
                                                    [&game](const Installation &item) {
                                                        return item.game_id == game.id && item.available;
                                                    });
            if (installation != current_state_->installations.end() && installation->volume_id != "internal")
            {
                scene_.rounded_rect({rect.x + rect.w - 86.0F, rect.y + 10.0F, 76.0F, 32.0F}, 6.0F,
                                    kBackground.with_alpha(0.88F));
                ui::text(scene_, fonts_.semibold, "DRIVE", rect.x + rect.w - 48.0F,
                         rect.y + 33.0F, 14.0F, kText, gfx::Align::center, 1.0F);
            }
        }
        if (progress >= 0.0F)
        {
            const auto job = current_state_ == nullptr ? std::vector<Job>::const_iterator{} :
                std::find_if(current_state_->jobs.begin(), current_state_->jobs.end(),
                             [&game](const Job &item) {
                                 return item.game_id == game.id && item.state != JobState::Cancelled;
                             });
            if (current_state_ != nullptr && job != current_state_->jobs.end())
                ui::text(scene_, fonts_.semibold, job_state_label(job->state), rect.x + rect.w - 12.0F,
                         rect.y + rect.h - 24.0F, 14.0F, kText, gfx::Align::right);
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
        list_.style.theme.focus = route_focus_ ? kAccent : kAccent.with_alpha(0.0F);
        list_.set_items(list_items(view));
        list_.set_bounds(bounds);
        list_.set_focus(std::clamp(view.focused_row, 0, std::max(0, static_cast<int>(view.rows.size()) - 1)),
                        true);
        list_.set_active(route_focus_);
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
        (void)view;
        std::vector<const Job*> jobs;
        for (const Job& job : state.jobs)
            if (job.state != JobState::Cancelled)
                jobs.push_back(&job);
        std::stable_sort(jobs.begin(), jobs.end(), [](const Job* left, const Job* right) {
            return left->queue_order < right->queue_order;
        });
        const auto active = std::find_if(jobs.begin(), jobs.end(), [](const Job* job) {
            return job->state == JobState::Downloading || job->state == JobState::Verifying ||
                   job->state == JobState::Pausing || job->state == JobState::Paused ||
                   job->state == JobState::Failed || job->state == JobState::Interrupted;
        });
        const Job* primary = active == jobs.end() ? (jobs.empty() ? nullptr : jobs.front()) : *active;

        section_label("Transfers", 96.0F, 146.0F);
        const gfx::Rect active_card{96.0F, 180.0F, 1000.0F, 230.0F};
        scene_.shadow(active_card, 12.0F, 24.0F, gfx::Color::rgb(0x000000, 0.35F));
        scene_.rounded_rect(active_card, 12.0F, kPanel);
        if (primary == nullptr)
        {
            ui::text(scene_, fonts_.display, "No transfers", 136.0F, 262.0F, 42.0F, kText);
            ui::text(scene_, fonts_.regular, "Install a game to start a simulated transfer.",
                     136.0F, 310.0F, 23.0F, kSecondary);
        }
        else
        {
            const Game* game = game_by_id(state, primary->game_id);
            if (game != nullptr)
                draw_poster(*game, {116.0F, 196.0F, 132.0F, 198.0F}, false, {}, -1.0F, false);
            else
                scene_.rounded_rect({116.0F, 196.0F, 132.0F, 198.0F}, 8.0F, kMuted.with_alpha(0.25F));
            const std::string title = game == nullptr ? "Unknown game" : game->title;
            dynamic_text(title, 282.0F, 235.0F, 34.0F, kText, 750.0F);
            ui::text(scene_, fonts_.semibold, job_stage_label(primary->stage), 282.0F, 274.0F,
                     22.0F, primary->state == JobState::Failed ? kRed :
                            primary->state == JobState::Paused ? kAmber : kAccent);
            ui::text(scene_, fonts_.regular, job_state_label(primary->state), 1038.0F, 274.0F,
                     20.0F, kSecondary, gfx::Align::right);
            const float progress = primary->total_bytes == 0 ? 0.0F :
                static_cast<float>(primary->completed_bytes) / static_cast<float>(primary->total_bytes);
            ui::ProgressBar bar;
            bar.style.theme = theme_;
            bar.style.height = 12.0F;
            bar.style.placement = ui::LabelPlacement::none;
            bar.style.status = primary->state == JobState::Failed ? ui::Status::danger :
                               primary->state == JobState::Paused ? ui::Status::warning : ui::Status::accent;
            bar.style.finish_flash = false;
            bar.set_bounds({282.0F, 302.0F, 756.0F, 12.0F});
            bar.set_value(std::clamp(progress, 0.0F, 1.0F), true);
            bar.draw(canvas);
            const std::string bytes = byte_count(primary->completed_bytes) + " / " +
                                      byte_count(primary->total_bytes);
            ui::text(scene_, fonts_.regular, bytes, 282.0F, 348.0F, 21.0F, kText);
            ui::text(scene_, fonts_.regular, transfer_rate(primary->bytes_per_second), 1038.0F,
                     348.0F, 21.0F, kSecondary, gfx::Align::right);
            if (route_focus_ && state.navigation.focus_key == "job-" + primary->id)
                scene_.bordered_rect(active_card.inset(-5.0F), 14.0F, kBackground.with_alpha(0.0F),
                                     4.0F, kAccent);
        }

        float row_y = 438.0F;
        for (const Job* job : jobs)
        {
            if (job == primary || row_y > 914.0F)
                continue;
            const Game* game = game_by_id(state, job->game_id);
            const gfx::Rect row{96.0F, row_y, 1000.0F, 82.0F};
            scene_.rounded_rect(row, 8.0F, kPanel);
            dynamic_text(game == nullptr ? std::string_view("Unknown game") : std::string_view(game->title),
                         122.0F, row_y + 35.0F, 25.0F, kText, 570.0F);
            ui::text(scene_, fonts_.regular, job_state_label(job->state), 742.0F, row_y + 35.0F,
                     20.0F, job->state == JobState::Failed ? kRed : kSecondary);
            const std::string bytes = byte_count(job->completed_bytes) + " / " + byte_count(job->total_bytes);
            ui::text(scene_, fonts_.regular, bytes, 1068.0F, row_y + 35.0F, 19.0F, kMuted,
                     gfx::Align::right);
            const float progress = job->total_bytes == 0 ? 0.0F :
                static_cast<float>(job->completed_bytes) / static_cast<float>(job->total_bytes);
            scene_.rounded_rect({122.0F, row_y + 60.0F, 946.0F, 5.0F}, 2.5F, kText.with_alpha(0.12F));
            scene_.rounded_rect({122.0F, row_y + 60.0F, 946.0F * std::clamp(progress, 0.0F, 1.0F), 5.0F},
                                2.5F, job->state == JobState::Failed ? kRed : kAccent);
            if (route_focus_ && state.navigation.focus_key == "job-" + job->id)
                scene_.bordered_rect(row.inset(-4.0F), 10.0F, kBackground.with_alpha(0.0F), 3.0F,
                                     kAccent);
            row_y += 94.0F;
        }

        scene_.rounded_rect({1160.0F, 160.0F, 664.0F, 780.0F}, 12.0F, kPanel);
        ui::text(scene_, fonts_.display, "Storage", 1200.0F, 218.0F, 38.0F, kText);
        float y = 276.0F;
        for (const Volume &volume : state.volumes)
        {
            const std::uint64_t occupied = std::min(volume.capacity_bytes,
                                                     volume.used_bytes + volume.reserved_bytes);
            const std::uint64_t free = volume.capacity_bytes - occupied;
            const float fraction = volume.capacity_bytes == 0 ? 0.0F :
                static_cast<float>(volume.used_bytes) / static_cast<float>(volume.capacity_bytes);
            const float reserved = volume.capacity_bytes == 0 ? 0.0F :
                static_cast<float>(volume.reserved_bytes) / static_cast<float>(volume.capacity_bytes);
            ui::text(scene_, fonts_.display, volume.label, 1200.0F, y, 29.0F, kText);
            ui::text(scene_, fonts_.regular,
                     volume.available ? (volume.writable ? "Available" : "Read only") : "Disconnected",
                     1784.0F, y, 21.0F, volume.available && volume.writable ? kSecondary : kAmber,
                     gfx::Align::right);
            const float bar_x = 1200.0F;
            const float bar_y = y + 28.0F;
            const float bar_w = 584.0F;
            scene_.rounded_rect({bar_x, bar_y, bar_w, 12.0F}, 6.0F, kText.with_alpha(0.14F));
            scene_.rounded_rect({bar_x, bar_y, bar_w * std::clamp(fraction, 0.0F, 1.0F), 12.0F},
                                5.0F, volume.available ? kAccent : kMuted);
            scene_.rounded_rect({bar_x + bar_w * std::clamp(fraction, 0.0F, 1.0F), bar_y,
                                 bar_w * std::clamp(reserved, 0.0F, 1.0F), 12.0F},
                                5.0F, volume.available ? kAmber : kMuted);
            ui::text(scene_, fonts_.regular, "Capacity  " + byte_count(volume.capacity_bytes),
                     1200.0F, y + 76.0F, 18.0F, kSecondary);
            ui::text(scene_, fonts_.regular, "Used  " + byte_count(volume.used_bytes),
                     1200.0F, y + 106.0F, 18.0F, kSecondary);
            ui::text(scene_, fonts_.regular, "Reserved  " + byte_count(volume.reserved_bytes),
                     1784.0F, y + 76.0F, 18.0F, kSecondary, gfx::Align::right);
            ui::text(scene_, fonts_.semibold, "Free  " + byte_count(free),
                     1784.0F, y + 106.0F, 18.0F, volume.available ? kText : kMuted,
                     gfx::Align::right);
            y += 192.0F;
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
        const auto detail_scroll = state.navigation.scroll_offsets.find("details-content");
        constexpr std::size_t page_size = 420;
        const int requested_page = detail_scroll == state.navigation.scroll_offsets.end() ? 0 :
                                       std::max(0, detail_scroll->second);
        const int max_page = description.empty() ? 0 :
                                 static_cast<int>((description.size() - 1) / page_size);
        const int page = std::min(requested_page, max_page);
        const std::size_t offset = utf8_boundary(description, static_cast<std::size_t>(page) * page_size);
        const std::size_t end = utf8_boundary(description, std::min(description.size(), offset + page_size));
        std::string page_text = description.substr(offset, end - offset);
        if (offset > 0)
            page_text.insert(0, "…");
        if (offset + page_size < description.size())
            page_text += "…";
        ui::text(scene_, fonts_.display, "About", 550.0F, 674.0F, 25.0F, kText);
        ui::paragraph(scene_, fonts_.regular, page_text, 550.0F, 706.0F, 19.0F, 520.0F, 26.0F,
                      kSecondary, 8);
        if (description.size() > page_size)
            ui::text(scene_, fonts_.regular, "L2/R2 Read more · page " +
                         std::to_string(page + 1) + " of " + std::to_string(max_page + 1),
                     550.0F, 934.0F, 17.0F, kMuted);
        if (game != nullptr)
        {
            scene_.rounded_rect({1112.0F, 638.0F, 712.0F, 300.0F}, 12.0F, kPanel);
            const Installation *installation = nullptr;
            const auto found_installation = std::find_if(
                state.installations.begin(), state.installations.end(), [&game](const Installation &item) {
                    return item.game_id == game->id && item.available;
                });
            if (found_installation != state.installations.end())
                installation = &*found_installation;
            std::string location = "Not installed";
            if (installation != nullptr)
            {
                const auto volume = std::find_if(state.volumes.begin(), state.volumes.end(),
                                                 [installation](const Volume &item) {
                                                     return item.id == installation->volume_id;
                                                 });
                location = volume == state.volumes.end() ? installation->volume_id : volume->label;
            }
            const auto rating = state.library.ratings.find(game->id);
            const Compatibility compatibility = rating == state.library.ratings.end() ?
                                                    Compatibility::Untested : rating->second;
            const std::string history = relative_time(game->last_played, state.clock_ticks) + " · " +
                                        playtime_label(game->playtime_minutes);
            detail_line("Genres", join_words(game->genres), 1144.0F, 682.0F, 632.0F);
            detail_line("Source", game->source.empty() ? "Unknown" : game->source, 1144.0F, 714.0F,
                        632.0F);
            detail_line("Download size", byte_count(game->download_bytes), 1144.0F, 746.0F, 632.0F);
            detail_line("Install size", byte_count(game->install_bytes), 1144.0F, 778.0F, 632.0F);
            detail_line("Play history", history, 1144.0F, 810.0F, 632.0F);
            detail_line("Compatibility", rating_label(compatibility), 1144.0F, 842.0F, 632.0F);
            detail_line("Location", location, 1144.0F, 874.0F, 632.0F);
            detail_line("Controller", game->controller_support.empty() ? "Unknown" : game->controller_support,
                        1144.0F, 906.0F, 632.0F);
        }
    }

    void detail_line(std::string_view label, std::string_view value, float x, float y, float width)
    {
        ui::text(scene_, fonts_.regular, label, x, y, 20.0F, kMuted);
        ui::text(scene_, fonts_.semibold, fit(fonts_.semibold, value, 19.0F, width - 180.0F),
                 x + width, y, 19.0F, kText, gfx::Align::right);
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
            for (std::size_t index = 0; index < labels.size() && hints.size() < 5; ++index)
            {
                if (labels[index].find("Sections") != std::string::npos)
                    continue;
                hints.push_back({legend_button(labels[index], index), labels[index].c_str()});
            }
            const bool has_back = std::any_of(labels.begin(), labels.end(), [](const std::string &label) {
                return label.find("Back") != std::string::npos ||
                       label.find("Cancel") != std::string::npos;
            });
            if (!has_back)
                hints.push_back({ui::Button::circle, "Back"});
        }
        hints.push_back({ui::Button::l1, "Sections", ui::Button::r1});
        ui::HintLayout layout;
        layout.size = 38.0F;
        layout.text_size = 22.0F;
        layout.cy = kFooterY + 20.0F;
        layout.item_gap = 28.0F;
        layout.font = &fonts_.semibold;
        ui::GlyphStyle glyphs = ui::GlyphStyle::mono(kText, kBackground);
        ui::draw_hints(scene_, fonts_, glyphs, hints.data(), static_cast<int>(hints.size()), 96.0F,
                       false, layout);
        if (current_state_ != nullptr)
        {
            const auto job = std::find_if(current_state_->jobs.begin(), current_state_->jobs.end(),
                                          [](const Job &value) {
                                              return value.state == JobState::Downloading ||
                                                     value.state == JobState::Verifying ||
                                                     value.state == JobState::Paused;
                                          });
            if (job != current_state_->jobs.end())
            {
                const Game *game = game_by_id(*current_state_, job->game_id);
                const float progress = job->total_bytes == 0 ? 0.0F :
                    static_cast<float>(job->completed_bytes) / static_cast<float>(job->total_bytes);
                const float x = 1480.0F;
                scene_.rounded_rect({1464.0F, 986.0F, 168.0F, 40.0F}, 8.0F,
                                    kBackground.with_alpha(0.86F));
                dynamic_text(game == nullptr ? std::string_view("Transfer") : std::string_view(game->title),
                             x, 1004.0F, 16.0F, kText, 136.0F, false);
                scene_.rounded_rect({x, 1013.0F, 136.0F, 6.0F}, 3.0F, kText.with_alpha(0.18F));
                scene_.rounded_rect({x, 1013.0F, 136.0F * std::clamp(progress, 0.0F, 1.0F), 6.0F},
                                    3.0F, job->state == JobState::Paused ? kAmber : kAccent);
            }
        }
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
            const ModalEntry &modal = state.navigation.modals.back();
            if (modal.destructive || modal.id == "confirmation")
                draw_modal_overlay(modal, view);
            else
                draw_sheet_overlay(view);
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
                      {0.0F, 0.0F, false});
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
        const std::size_t count = std::max<std::size_t>(1, view.rows.size());
        const float button_width = (panel.w - 112.0F) / count;
        for (std::size_t index = 0; index < view.rows.size(); ++index)
        {
            std::string label = overlay_label(view.rows[index]);
            if (view.rows[index].action == "confirm-destructive")
            {
                if (modal.title.find("Uninstall") != std::string::npos)
                    label = "Uninstall";
                else if (modal.title.find("download") != std::string::npos)
                    label = "Cancel download";
                else
                    label = "Confirm";
            }
            const gfx::Rect button{panel.x + 48.0F + index * (button_width + 16.0F), panel.y + 388.0F,
                                   button_width, 68.0F};
            ui::Painter painter(overlay_, fonts_, theme_, renderer_.glass_texture());
            painter.button(button, label,
                           view.rows[index].dangerous ?
                               ui::ButtonKind::primary : ui::ButtonKind::secondary,
                           {static_cast<int>(index) == view.focused_row ? 1.0F : 0.0F, 0.0F,
                            !view.rows[index].enabled});
        }
    }

    void draw_sheet_overlay(const ViewData &view)
    {
        const float ease = 1.0F - std::pow(1.0F - overlay_progress_, 3.0F);
        overlay_.rounded_rect({0.0F, 0.0F, kWidth, kHeight}, 0.0F,
                              gfx::Color::rgb(0x000000, 0.58F * ease));
        const bool wide = view.surface.find("settings") != std::string::npos ||
                          view.surface.find("profile") != std::string::npos ||
                          view.surface == "controller" || view.surface == "logs" ||
                          view.surface == "storage";
        const float width = wide ? 960.0F : 640.0F;
        const gfx::Rect panel{kWidth - width + (1.0F - ease) * width, 0.0F, width, kHeight};
        overlay_.shadow({panel.x - 18.0F, 0.0F, panel.w + 18.0F, panel.h}, 0.0F, 36.0F,
                        gfx::Color::rgb(0x000000, 0.75F));
        overlay_.rounded_rect(panel, 0.0F, kPanel);
        section_overlay(view.surface, panel.x + 48.0F, 72.0F);
        ui::text(overlay_, fonts_.display, view.title, panel.x + 48.0F, 142.0F, 48.0F, kText);
        if (!view.subtitle.empty())
            ui::paragraph(overlay_, fonts_.regular, view.subtitle, panel.x + 48.0F, 190.0F, 23.0F,
                          panel.w - 96.0F, 32.0F, kSecondary, 3);
        if (view.surface == "storage")
            draw_storage_overlay_rows(view, {panel.x + 40.0F, 246.0F, panel.w - 80.0F, 710.0F});
        else if (view.surface == "logs")
            draw_log_overlay(view, {panel.x + 40.0F, 246.0F, panel.w - 80.0F, 710.0F});
        else if (view.surface == "controller")
            draw_controller_overlay(view, {panel.x + 40.0F, 236.0F, panel.w - 80.0F, 720.0F});
        else
            draw_overlay_rows(view, {panel.x + 40.0F, 292.0F, panel.w - 80.0F, 650.0F});
    }

    void draw_storage_overlay_rows(const ViewData &view, const gfx::Rect &bounds)
    {
        if (current_state_ == nullptr)
            return;
        float y = bounds.y;
        ui::Painter painter(overlay_, fonts_, theme_, renderer_.glass_texture());
        for (const ViewRow &row : view.rows)
        {
            if (row.action.rfind("volume-", 0) != 0)
                continue;
            const std::string id = row.action.substr(7);
            const auto found = std::find_if(current_state_->volumes.begin(), current_state_->volumes.end(),
                                            [&id](const Volume &volume) { return volume.id == id; });
            if (found == current_state_->volumes.end())
                continue;
            const Volume &volume = *found;
            const std::uint64_t occupied = std::min(volume.capacity_bytes,
                                                     volume.used_bytes + volume.reserved_bytes);
            const std::uint64_t free = volume.capacity_bytes - occupied;
            const int row_index = static_cast<int>(&row - view.rows.data());
            const gfx::Rect card{bounds.x, y, bounds.w, 156.0F};
            overlay_.rounded_rect(card, 8.0F, kText.with_alpha(row.selected ? 0.10F : 0.04F));
            if (row_index == view.focused_row)
                overlay_.bordered_rect(card.inset(-4.0F), 10.0F, kBackground.with_alpha(0.0F),
                                       4.0F, kAccent);
            ui::text(overlay_, fonts_.display, volume.label, card.x + 22.0F, card.y + 38.0F,
                     27.0F, row.enabled ? kText : kMuted);
            ui::text(overlay_, fonts_.regular, row.subtitle, card.x + card.w - 22.0F,
                     card.y + 38.0F, 18.0F, row.enabled ? kSecondary : kAmber, gfx::Align::right);
            const float fraction = volume.capacity_bytes == 0 ? 0.0F :
                static_cast<float>(occupied) / static_cast<float>(volume.capacity_bytes);
            overlay_.rounded_rect({card.x + 22.0F, card.y + 58.0F, card.w - 44.0F, 8.0F}, 4.0F,
                                  kText.with_alpha(0.14F));
            overlay_.rounded_rect({card.x + 22.0F, card.y + 58.0F,
                                   (card.w - 44.0F) * std::clamp(fraction, 0.0F, 1.0F), 8.0F},
                                  4.0F, volume.available ? kAccent : kMuted);
            ui::text(overlay_, fonts_.regular, "Capacity  " + byte_count(volume.capacity_bytes),
                     card.x + 22.0F, card.y + 98.0F, 17.0F, kSecondary);
            ui::text(overlay_, fonts_.regular, "Used  " + byte_count(volume.used_bytes),
                     card.x + 22.0F, card.y + 128.0F, 17.0F, kSecondary);
            ui::text(overlay_, fonts_.regular, "Reserved  " + byte_count(volume.reserved_bytes),
                     card.x + card.w - 22.0F, card.y + 98.0F, 17.0F, kSecondary,
                     gfx::Align::right);
            ui::text(overlay_, fonts_.semibold, "Free  " + byte_count(free),
                     card.x + card.w - 22.0F, card.y + 128.0F, 17.0F, kText,
                     gfx::Align::right);
            y += 174.0F;
        }
        const auto cancel = std::find_if(view.rows.begin(), view.rows.end(), [](const ViewRow &row) {
            return row.action == "cancel";
        });
        if (cancel != view.rows.end() && y + 70.0F < bounds.y + bounds.h)
        {
            const int index = static_cast<int>(std::distance(view.rows.begin(), cancel));
            painter.row({bounds.x, y, bounds.w, 66.0F}, "Cancel", "", 0.0F,
                        {index == view.focused_row ? 1.0F : 0.0F, 0.0F, false});
        }
    }

    void draw_log_overlay(const ViewData &view, const gfx::Rect &bounds)
    {
        overlay_.rounded_rect(bounds, 8.0F, kBackground.with_alpha(0.72F));
        if (current_state_ == nullptr)
            return;
        const State &state = *current_state_;
        const gfx::Rect viewport{bounds.x + 20.0F, bounds.y + 20.0F,
                                 bounds.w - 40.0F, bounds.h - 132.0F};
        overlay_.rounded_rect(viewport, 6.0F, kPanel.with_alpha(0.48F));

        if (state.log_state == LogState::Loading)
        {
            overlay_.circle(viewport.x + 34.0F, viewport.y + 44.0F, 7.0F, kAccent);
            ui::text(overlay_, fonts_.semibold, "Loading fixture logs…", viewport.x + 58.0F,
                     viewport.y + 52.0F, 22.0F, kText);
            overlay_.rounded_rect({viewport.x + 24.0F, viewport.y + 78.0F,
                                   viewport.w - 48.0F, 5.0F}, 2.5F, kText.with_alpha(0.12F));
            const float pulse = 0.18F + 0.22F * (0.5F + 0.5F * std::sin(clock_ * 5.0F));
            overlay_.rounded_rect({viewport.x + 24.0F, viewport.y + 78.0F,
                                   (viewport.w - 48.0F) * pulse, 5.0F}, 2.5F, kAccent);
        }
        else if (state.log_state == LogState::Failed)
        {
            ui::text(overlay_, fonts_.display, "Logs unavailable", viewport.x + 24.0F,
                     viewport.y + 54.0F, 30.0F, kRed);
            const auto error = std::find_if(view.rows.begin(), view.rows.end(), [](const ViewRow &row) {
                return row.key == "logs-error";
            });
            const std::string_view message = error == view.rows.end() || error->subtitle.empty() ?
                                                 std::string_view("The simulated log source failed.") :
                                                 std::string_view(error->subtitle);
            dynamic_overlay_text(message, viewport.x + 24.0F, viewport.y + 96.0F, 20.0F,
                                 kSecondary, viewport.w - 48.0F);
        }
        else if (state.log_lines.empty())
        {
            ui::text(overlay_, fonts_.display, "No log entries", viewport.cx(),
                     viewport.y + viewport.h * 0.48F, 34.0F, kSecondary, gfx::Align::center);
            ui::text(overlay_, fonts_.regular, "This fixture has no lines for the current context.",
                     viewport.cx(), viewport.y + viewport.h * 0.48F + 38.0F, 19.0F, kMuted,
                     gfx::Align::center);
        }
        else
        {
            constexpr int visible_lines = 8;
            const int begin = std::clamp(state.log_offset, 0,
                                         std::max(0, static_cast<int>(state.log_lines.size()) - visible_lines));
            const int end = std::min(static_cast<int>(state.log_lines.size()), begin + visible_lines);
            float y = viewport.y + 38.0F;
            for (int index = begin; index < end; ++index)
            {
                std::array<char, 12> number{};
                std::snprintf(number.data(), number.size(), "%03d", index + 1);
                ui::text(overlay_, fonts_.regular, number.data(), viewport.x + 22.0F, y,
                         16.0F, kMuted);
                dynamic_overlay_text(state.log_lines[static_cast<std::size_t>(index)],
                                     viewport.x + 72.0F, y, 18.0F, kText, viewport.w - 106.0F);
                y += 52.0F;
            }
            const std::string range = std::to_string(begin + 1) + "–" + std::to_string(end) +
                                      " of " + std::to_string(state.log_lines.size());
            ui::text(overlay_, fonts_.regular, range, viewport.x + viewport.w - 24.0F,
                     viewport.y + viewport.h - 18.0F, 16.0F, kMuted, gfx::Align::right);
            if (state.log_lines.size() > visible_lines)
            {
                const float track_h = viewport.h - 52.0F;
                const float thumb_h = std::max(36.0F, track_h * visible_lines /
                                                        static_cast<float>(state.log_lines.size()));
                const float travel = track_h - thumb_h;
                const float denominator = static_cast<float>(state.log_lines.size() - visible_lines);
                const float thumb_y = viewport.y + 18.0F + travel * begin / denominator;
                overlay_.rounded_rect({viewport.x + viewport.w - 7.0F, viewport.y + 18.0F,
                                       3.0F, track_h}, 1.5F, kText.with_alpha(0.10F));
                overlay_.rounded_rect({viewport.x + viewport.w - 7.0F, thumb_y, 3.0F, thumb_h},
                                      1.5F, kSecondary);
            }
        }

        std::vector<const ViewRow*> actions;
        for (const ViewRow &row : view.rows)
            if (!row.action.empty())
                actions.push_back(&row);
        if (!actions.empty())
        {
            ui::Painter painter(overlay_, fonts_, theme_, renderer_.glass_texture());
            const float width = std::min(260.0F, (bounds.w - 48.0F) / actions.size() - 10.0F);
            for (std::size_t index = 0; index < actions.size(); ++index)
            {
                const bool focused = state.navigation.focus_key == actions[index]->key;
                painter.button({bounds.x + 24.0F + index * (width + 16.0F),
                                bounds.y + bounds.h - 82.0F, width, 58.0F},
                               overlay_label(*actions[index]), ui::ButtonKind::secondary,
                               {focused ? 1.0F : 0.0F, 0.0F, !actions[index]->enabled});
            }
        }
    }

    void draw_controller_overlay(const ViewData &view, const gfx::Rect &bounds)
    {
        const bool connected = current_state_ != nullptr && current_state_->controller_connected;
        const ControllerState input = current_state_ == nullptr ? ControllerState{} :
                                                                  current_state_->controller;
        overlay_.rounded_rect(bounds, 12.0F, kBackground.with_alpha(0.52F));
        ui::text(overlay_, fonts_.display, connected ? "Controller connected" : "Controller disconnected",
                 bounds.x + bounds.w * 0.5F, bounds.y + 54.0F, 31.0F,
                 connected ? kGreen : kAmber, gfx::Align::center);
        const gfx::Color shell = connected ? kText.with_alpha(0.12F) : kMuted.with_alpha(0.10F);
        const float cx = bounds.x + bounds.w * 0.5F;
        const float cy = bounds.y + 292.0F;
        overlay_.rounded_rect({cx - 286.0F, cy - 136.0F, 572.0F, 250.0F}, 88.0F, shell);
        overlay_.circle(cx - 224.0F, cy + 96.0F, 92.0F, shell);
        overlay_.circle(cx + 224.0F, cy + 96.0F, 92.0F, shell);
        overlay_.rounded_rect({cx - 242.0F, cy - 164.0F, 158.0F, 24.0F}, 8.0F,
                              input.l2 > 0.5F ? kAccent : kText.with_alpha(0.16F));
        overlay_.rounded_rect({cx + 84.0F, cy - 164.0F, 158.0F, 24.0F}, 8.0F,
                              input.r2 > 0.5F ? kAccent : kText.with_alpha(0.16F));
        ui::text(overlay_, fonts_.semibold, "L2", cx - 163.0F, cy - 146.0F, 16.0F, kText,
                 gfx::Align::center);
        ui::text(overlay_, fonts_.semibold, "R2", cx + 163.0F, cy - 146.0F, 16.0F, kText,
                 gfx::Align::center);

        const auto stick = [this](float x, float y, float axis_x, float axis_y) {
            overlay_.circle(x, y, 48.0F, kBackground.with_alpha(0.72F));
            overlay_.circle(x + axis_x * 24.0F, y + axis_y * 24.0F, 22.0F,
                            (axis_x != 0.0F || axis_y != 0.0F) ? kAccent : kSecondary);
        };
        stick(cx - 112.0F, cy + 48.0F, input.left_x, input.left_y);
        stick(cx + 72.0F, cy + 48.0F, input.right_x, input.right_y);

        const auto face = [this](float x, float y, bool active, ui::Button button) {
            overlay_.circle(x, y, 23.0F, active ? kAccent : kBackground.with_alpha(0.78F));
            ui::draw_button(overlay_, fonts_,
                            ui::GlyphStyle::mono(active ? kBackground : kText, kBackground),
                            button, x - ui::button_width(button, 30.0F) * 0.5F, y, 30.0F);
        };
        face(cx + 200.0F, cy - 28.0F, input.triangle, ui::Button::triangle);
        face(cx + 248.0F, cy + 20.0F, input.circle, ui::Button::circle);
        face(cx + 200.0F, cy + 68.0F, input.cross, ui::Button::cross);
        face(cx + 152.0F, cy + 20.0F, input.square, ui::Button::square);

        const gfx::Color dpad = (input.left_x != 0.0F || input.left_y != 0.0F) ? kAccent : kSecondary;
        overlay_.rounded_rect({cx - 232.0F, cy - 4.0F, 92.0F, 28.0F}, 5.0F, dpad);
        overlay_.rounded_rect({cx - 200.0F, cy - 36.0F, 28.0F, 92.0F}, 5.0F, dpad);
        overlay_.rounded_rect({cx - 34.0F, cy - 18.0F, 68.0F, 30.0F}, 8.0F,
                              input.options ? kAccent : kBackground.with_alpha(0.72F));
        ui::text(overlay_, fonts_.semibold, "OPTIONS", cx, cy + 3.0F, 12.0F, kText,
                 gfx::Align::center, 0.8F);
        ui::text(overlay_, fonts_.regular, "Left stick", cx - 112.0F, cy + 122.0F, 16.0F,
                 kSecondary, gfx::Align::center);
        ui::text(overlay_, fonts_.regular, "Right stick", cx + 72.0F, cy + 122.0F, 16.0F,
                 kSecondary, gfx::Align::center);
        draw_overlay_rows(view, {bounds.x + 20.0F, bounds.y + bounds.h - 104.0F,
                                 bounds.w - 40.0F, 84.0F});
    }

    void dynamic_overlay_text(std::string_view value, float x, float y, float size,
                              gfx::Color color, float max_width)
    {
        const ui::FontRef &face = font_for(value, fonts_.regular);
        ui::text(overlay_, face, face.font->fit(value, size, max_width), x, y, size, color);
    }

    void section_overlay(std::string_view value, float x, float y)
    {
        ui::text(overlay_, fonts_.display, ui::upper(value), x, y, 20.0F, kSecondary,
                 gfx::Align::left, 2.4F);
    }

    std::string overlay_label(const ViewRow &row) const
    {
        if (row.action == "profile-picker")
            return "Profile";
        if (row.action == "resolution-picker")
            return "Resolution";
        if (row.action == "frame-picker")
            return "Frame limit";
        if (row.action == "toggle-vsync")
            return "VSync";
        if (row.action == "edit-arguments")
            return "Launch arguments";
        if (row.action == "reset-settings")
            return "Reset all to profile";
        if (row.action == "cancel")
            return "Cancel";
        if (row.action.rfind("resolution-", 0) == 0)
        {
            std::string resolution = row.action.substr(11);
            const std::size_t separator = resolution.find('x');
            if (separator != std::string::npos)
                resolution.replace(separator, 1, "×");
            return resolution;
        }
        if (row.action.rfind("frame-", 0) == 0)
            return row.action.substr(6);
        if (row.action.rfind("profile-", 0) == 0 && current_state_ != nullptr)
        {
            const std::string id = row.action.substr(8);
            const auto found = std::find_if(current_state_->profiles.begin(), current_state_->profiles.end(),
                                            [&id](const Profile &profile) { return profile.id == id; });
            if (found != current_state_->profiles.end())
                return found->name;
        }
        std::string label = row.title.empty() ? row.action : row.title;
        std::replace(label.begin(), label.end(), '-', ' ');
        if (!label.empty() && label.front() >= 'a' && label.front() <= 'z')
            label.front() = static_cast<char>(label.front() - 'a' + 'A');
        return label;
    }

    std::string overlay_value(const ViewRow &row) const
    {
        if (row.key == "account-auth")
        {
            std::string status = row.value;
            if (!status.empty() && status.front() >= 'a' && status.front() <= 'z')
                status.front() = static_cast<char>(status.front() - 'a' + 'A');
            if (!row.subtitle.empty())
                status += (status.empty() ? "" : " · ") + row.subtitle;
            return status;
        }
        if (row.key == "account-last-sync" && current_state_ != nullptr && row.value != "Never")
        {
            std::int64_t timestamp = 0;
            const auto parsed = std::from_chars(row.value.data(), row.value.data() + row.value.size(), timestamp);
            if (parsed.ec == std::errc{})
                return relative_time(timestamp, current_state_->clock_ticks);
        }
        if (!row.value.empty())
            return row.value;
        if (!row.badge.empty())
            return row.badge;
        if (!row.subtitle.empty())
            return row.subtitle;
        if (current_state_ == nullptr || current_state_->profiles.empty())
            return {};
        const Profile &profile = current_state_->profiles.size() > 1 ? current_state_->profiles[1]
                                                                    : current_state_->profiles.front();
        if (row.action == "profile-picker")
            return profile.name;
        if (row.action == "resolution-picker")
            return profile.resolution;
        if (row.action == "frame-picker")
            return profile.frame_limit;
        if (row.action == "toggle-vsync")
            return profile.vsync ? "On" : "Off";
        if (row.action == "edit-arguments")
            return profile.launch_arguments.empty() ? "None" : profile.launch_arguments;
        return {};
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
            painter.row(rect, overlay_label(row), overlay_value(row),
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
    bool route_focus_ = true;
    std::string overlay_key_;
    float overlay_progress_ = 1.0F;
    int last_width_ = 0;
    int last_height_ = 0;
    mutable GLuint capture_framebuffer_ = 0;
    mutable GLuint capture_color_ = 0;
    mutable int capture_width_ = 0;
    mutable int capture_height_ = 0;
    RenderStats stats_{};
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

RenderStats Render::diagnostics() const noexcept
{
    return impl_->diagnostics();
}

} // namespace playden::homebrew
