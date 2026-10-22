#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <iostream>
#include <ostream>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

void sighandler(int s) {
  std::cout << "\x1B[2J\x1B[H";
  std::cout << "\e[?25h";
  exit(s);
}

class InputController {
private:
  std::thread thread;
  std::atomic<bool> active{false};
  std::atomic<char> key = 0;
  termios originalTerminal{};

private:
  void loop() {
    if (tcgetattr(STDIN_FILENO, &originalTerminal) == -1) {
      perror("tcgetattr");
      return;
    }

    termios raw = originalTerminal;

    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
      perror("tcsetattr");
      return;
    }

    while (active.load()) {
      pollfd input{};
      input.fd = STDIN_FILENO;
      input.events = POLLIN;

      int result = poll(&input, 1, 500);

      if (result == -1) {
        perror("poll");
        break;
      }

      if (result == 0) {
        // key.store(' ');
        continue;
      }

      char c;
      ssize_t bytesRead = read(STDIN_FILENO, &c, 1);

      if (bytesRead == 1) {
        key.store(c);
      }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &originalTerminal);
  }

public:
  InputController() {
    active.store(true);
    thread = std::thread([this]() { loop(); });
  }

  ~InputController() {
    active.store(false);

    if (thread.joinable()) {
      thread.join();
    }
  }

  InputController(const InputController &) = delete;
  InputController &operator=(const InputController &) = delete;

  bool isAlive() { return active.load(); }

  char getKey() {
    char c = key.load();
    return c;
  }
};

struct Window {
  int w, h;
};

struct Pos {
  int x, y;
};

const int APPLE_COUNT = 6;
const int APPLE_POWER = 6;
const int APPLE_RADIUS = 6;
int main() {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  std::signal(SIGINT, sighandler);

  const Window window = {w.ws_col - 2, w.ws_row - 3};
  std::cout << std::nounitbuf;
  srand(time(NULL));
  InputController controller;

  size_t size = 1;
  std::deque<Pos> poses;
  std::deque<Pos> applePoses;
  poses.push_back({static_cast<int>(random() % window.w),
                   static_cast<int>(random() % (window.h * 2))});
  std::cout << "\x1B[2J\x1B[H";
  std::cout << "\e[?25l";
  std::vector<int> grid(window.w * window.h * 2);

  for (size_t i = 0; i < APPLE_COUNT; i++) {
    applePoses.push_back({static_cast<int>(random() % window.w),
                          static_cast<int>(random() % (window.h * 2))});
  }

  char c = 0;
  while (controller.isAlive()) {
    c = controller.getKey();
    switch (c) {
    case 'z':
    case 'A':
      poses.push_back({poses.back().x,
                       (poses.back().y - 1 + (window.h * 2)) % (window.h * 2)});
      break;
    case 'd':
    case 'C':
      poses.push_back({(poses.back().x + 1) % window.w, poses.back().y});
      break;
    case 's':
    case 'B':
      poses.push_back({poses.back().x, (poses.back().y + 1) % (window.h * 2)});
      break;
    case 'q':
    case 'D':
      poses.push_back(
          {(poses.back().x - 1 + window.w) % window.w, poses.back().y});
      break;
    }

    for (auto &applePos : applePoses) {
      if ((poses.back().x - applePos.x) * (poses.back().x - applePos.x) +
              (poses.back().y - applePos.y) * (poses.back().y - applePos.y) <=
          APPLE_RADIUS * APPLE_RADIUS) {
        size += APPLE_POWER;
        applePos.x = random() % window.w;
        applePos.y = random() % (window.h * 2);
      }
    }

    std::fill(grid.begin(), grid.end(), 0);
    while (poses.size() > size)
      poses.pop_front();
    for (auto pos : poses) {
      grid[pos.x + pos.y * window.w] = 7;
    }
    grid[poses.back().x + poses.back().y * window.w] = 9;
    for (auto applePos : applePoses) {
      for (size_t i = 0; i < APPLE_RADIUS * 2 + 1; i++)
        for (size_t j = 0; j < APPLE_RADIUS * 2 + 1; j++) {
          int x = applePos.x + i - APPLE_RADIUS;
          int y = applePos.y + j - APPLE_RADIUS;
          int w = x - applePos.x;
          int h = y - applePos.y;
          if (w * w + h * h <= APPLE_RADIUS * APPLE_RADIUS && x >= 0 &&
              y >= 0 && x < window.w && y < window.h * 2)
            grid[x + y * window.w] = 1;
        }
    }
    std::cout << std::flush;
    std::cout << "\033[0;0H";
    std::cout << "╭";
    for (size_t j = 0; j < window.w; j++)
      std::cout << "─";
    std::cout << "╮\n";
    for (size_t i = 0; i < window.h; i++) {
      std::cout << "│";
      for (size_t j = 0; j < window.w; j++) {
        int bottomColor = 0;
        int topColor = 0;
        topColor = grid[j + i * 2 * window.w];
        bottomColor = grid[j + (i * 2 + 1) * window.w];
        if (topColor || bottomColor) {
          if (topColor)
            std::cout << "\e[3" << topColor << "m";
          if (bottomColor && topColor)
            std::cout << "\e[4" << bottomColor << "m";
          if (bottomColor && !topColor)
            std::cout << "\e[3" << bottomColor << "m";
          if (topColor)
            std::cout << "▀";
          else
            std::cout << "▄";
          std::cout << "\e[0m";
        } else
          std::cout << " ";
      }
      std::cout << "│\n";
    }
    std::cout << "╰";
    for (size_t j = 0; j < window.w; j++)
      std::cout << "─";
    std::cout << "╯\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(
        static_cast<int>(1000.0f / (20.0f + std::log(size) * 10.0f))));
  }
  std::cout << "\x1B[2J\x1B[H";
  std::cout << "\e[?25h";
  return 0;
}
