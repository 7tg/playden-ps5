// Playden - Direct ps5-homebrew-ui renderer boundary.
// Copyright (C) 2026 Leo Lobato
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

namespace playden::homebrew
{

class Simulation;

class Render final
{
  public:
    explicit Render(std::filesystem::path assets_directory);
    ~Render();

    Render(Render &&) noexcept;
    Render &operator=(Render &&) noexcept;
    Render(const Render &) = delete;
    Render &operator=(const Render &) = delete;

    void draw(Simulation &simulation, int width, int height);
    bool capture_png(const std::filesystem::path &path, int width, int height) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace playden::homebrew
