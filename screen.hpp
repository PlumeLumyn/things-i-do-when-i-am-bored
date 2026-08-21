#pragma once
#include "vec.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

namespace Screen {

static void cleanScreen() {
  std::cout << "\x1B[2J\x1B[H";
  std::cout << "\e[?25h";
}

// Store the previous handler so we can chain to it.
static struct sigaction gscreen_prev_sigint;
static std::atomic<int> gscreen_sigint_count { 0 };

static void my_sigint_handler(int sig, siginfo_t *info, void *ctx) {
  // ---- your own work (must be async-signal-safe!) ----
  gscreen_sigint_count.fetch_add(1, std::memory_order_relaxed);
  cleanScreen();

  // ---- chain to the previous handler ----
  if (gscreen_prev_sigint.sa_flags & SA_SIGINFO) {
    if (gscreen_prev_sigint.sa_sigaction &&
        gscreen_prev_sigint.sa_sigaction != (void (*)(int, siginfo_t *, void *))SIG_DFL &&
        gscreen_prev_sigint.sa_sigaction != (void (*)(int, siginfo_t *, void *))SIG_IGN) {
      gscreen_prev_sigint.sa_sigaction(sig, info, ctx);
    }
  } else {
    auto h = gscreen_prev_sigint.sa_handler;
    if (h == SIG_DFL) {
      // Restore default and re-raise so default action happens.
      struct sigaction dfl {};
      dfl.sa_handler = SIG_DFL;
      sigemptyset(&dfl.sa_mask);
      sigaction(sig, &dfl, nullptr);
      raise(sig);
    } else if (h != SIG_IGN && h != nullptr) {
      h(sig);
    }
    // SIG_IGN → do nothing
  }
}

static void install_sigint_handler() {
  struct sigaction sa {};
  sa.sa_sigaction = my_sigint_handler;
  sa.sa_flags     = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);

  // Installs new handler AND returns the previous one in g_prev_sigint.
  if (sigaction(SIGINT, &sa, &gscreen_prev_sigint) != 0) {
    std::perror("sigaction");
  }
}

class Window {
  private:
    int x, y, w, h;
    std::vector<std::string> grid;
    std::vector<float> zBuffer;
    std::vector<Vec3f> colGrid;

  public:
    Window(int x, int y, int w, int h) {
      this->x = x;
      this->y = y;
      this->w = w - 2;
      this->h = h - 2;
      grid.resize(this->w * this->h);
      zBuffer.resize(this->w * this->h * 2);
      colGrid.resize(this->w * this->h * 2);
      clear();
    }

    int getWidth() {
      return w;
    }
    int getHeight() {
      return h;
    }
    int getSubPixelHeight() {
      return h * 2;
    }
    int getX() {
      return x;
    }
    int getY() {
      return y;
    }

    void clear() {
      std::fill(grid.begin(), grid.end(), ' ');
      std::fill(zBuffer.begin(), zBuffer.end(), INFINITY);
      for (auto &col : colGrid)
        col = { 0.6, 0.6, 0.9 };
    }

    void write(std::string str, int32_t x, int32_t y) {
      if (x <= this->w && y <= this->h - 1 && y >= 0 &&
          (x > -static_cast<int32_t>(str.size()))) {
        std::string limitedStr = str;
        if (x < 0) {
          limitedStr.erase(0, abs(x));
          x = 0;
        }
        size_t overflow =
            str.size() - std::min(static_cast<int>(str.size()), this->w - x);
        limitedStr.erase(limitedStr.size() - overflow, overflow);
        for (size_t i = 0; i < limitedStr.size(); i++) {
          grid[x + 1 + i + y * this->w]              = limitedStr[i];
          colGrid[x + 1 + i + (y * 2) * this->w]     = { 1.0, 1.0, 1.0 };
          colGrid[x + 1 + i + (y * 2 + 1) * this->w] = { 1.0, 0.0, 0.0 };
        }
      }
    }

    void putPixel(int32_t x, int32_t y, float z, Vec3f penCol) {
      if (x < this->getWidth() && y < this->getSubPixelHeight() && x >= 0 && y >= 0) {
        float &zBufferPixel = zBuffer[x + y * this->getWidth()];
        if (z < zBufferPixel) {
          zBufferPixel       = z;
          std::string &pixel = grid[x + (y / 2) * this->getWidth()];
          Vec3f &col         = colGrid[x + y * this->getWidth()];
          col                = penCol;
          if (pixel == "▀") return;
          if ((pixel == "▄" && y % 2 == 0)) {
            pixel = "▀";
            return;
          }
          pixel = y % 2 ? "▄" : "▀";
        }
      }
    };

    bool putZ(int32_t x, int32_t y, float z) {
      if (x < this->getWidth() && y < this->getSubPixelHeight() && x >= 0 && y >= 0) {
        float &zBufferPixel = zBuffer[x + y * this->getWidth()];
        if (z < zBufferPixel) {
          zBufferPixel = z;
          return true;
        }
      }
      return false;
    };

    void refresh() {
      std::cout << "\033[" << this->y + 1 << ";" << this->x + 1 << "H";
      std::cout << "╭";
      for (size_t j = 0; j < this->w; j++)
        std::cout << "─";
      std::cout << "╮";
      for (size_t i = 0; i < this->h; i++) {
        std::cout << "\033[" << this->y + 2 + i << ";" << this->x + 1 << "H";
        std::cout << "│";
        for (size_t j = 0; j < this->w; j++) {
          Vec3i fgCol = {
            static_cast<int>(colGrid[j + (i * 2) * this->w][0] * 255.0f),
            static_cast<int>(colGrid[j + (i * 2) * this->w][1] * 255.0f),
            static_cast<int>(colGrid[j + (i * 2) * this->w][2] * 255.0f),
          };
          Vec3i bgCol = {
            static_cast<int>(colGrid[j + (i * 2 + 1) * this->w][0] * 255.0f),
            static_cast<int>(colGrid[j + (i * 2 + 1) * this->w][1] * 255.0f),
            static_cast<int>(colGrid[j + (i * 2 + 1) * this->w][2] * 255.0f),
          };
          std::string character = grid[j + i * this->w];
          if (character == "▄") {
            std::cout
                << "\033[48;2;" << fgCol[0] << ";" << fgCol[1] << ";" << fgCol[2] << "m";
            std::cout
                << "\033[38;2;" << bgCol[0] << ";" << bgCol[1] << ";" << bgCol[2] << "m";
          } else {
            std::cout
                << "\033[38;2;" << fgCol[0] << ";" << fgCol[1] << ";" << fgCol[2] << "m";
            std::cout
                << "\033[48;2;" << bgCol[0] << ";" << bgCol[1] << ";" << bgCol[2] << "m";
          }
          std::cout << character;
        }
        std::cout << "\033[0m│";
      }
      std::cout << "\033[" << this->y + this->h + 1 << ";" << this->x + 1 << "H";
      std::cout << "╰";
      for (size_t j = 0; j < this->w; j++)
        std::cout << "─";
      std::cout << "╯" << std::flush;
    }
}; // namespace Screen

class ScreenController {
  private:
    std::vector<Window> windows;

  public:
    ScreenController() {

      std::cout << std::nounitbuf;
      std::cout << "\x1B[2J\x1B[H";
      std::cout << "\e[?25l";
      install_sigint_handler();
    }

    ~ScreenController() {
      cleanScreen();
    }

    Window &getWindow(size_t id) {
      return windows[id];
    }

    size_t createWindow(int x, int y, int w, int h) {
      struct winsize ws;
      ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);

      if (w <= 0) w = ws.ws_col;
      if (h <= 0) h = ws.ws_row;
      windows.emplace_back(x, y, w, h);
      return windows.size() - 1;
    }
};

} // namespace Screen
