#pragma once
#include "controller.hpp"
#include "vec.hpp"
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ostream>
#include <sys/poll.h>
#include <termios.h>
#include <unordered_map>

namespace Inputs {

#ifdef INPUTS_HPP_IMPLEMENTATION
struct termios orig;
#endif

extern struct termios orig;

// Kitty keyboard protocol key codepoints.
// Reference:
// https://sw.kovidgoyal.net/kitty/keyboard-protocol/#functional-key-definitions
//
// Regular printable keys are reported by their Unicode codepoint
// (e.g. 'a' = 97, ' ' = 32). Functional / non-textual keys use the
// dedicated PUA codepoints defined below.
enum class Keycode : uint32_t {
  // ---- ASCII controls that may still appear ----
  Tab       = 9,
  Enter     = 13,
  Escape    = 27,
  Space     = 32,
  Backspace = 127,

  // ---- Printable ASCII (a small selection; use the codepoint directly) ----
  Digit0 = '0',
  Digit1 = '1',
  Digit2 = '2',
  Digit3 = '3',
  Digit4 = '4',
  Digit5 = '5',
  Digit6 = '6',
  Digit7 = '7',
  Digit8 = '8',
  Digit9 = '9',

  A = 'a',
  B = 'b',
  C = 'c',
  D = 'd',
  E = 'e',
  F = 'f',
  G = 'g',
  H = 'h',
  I = 'i',
  J = 'j',
  K = 'k',
  L = 'l',
  M = 'm',
  N = 'n',
  O = 'o',
  P = 'p',
  Q = 'q',
  R = 'r',
  S = 's',
  T = 't',
  U = 'u',
  V = 'v',
  W = 'w',
  X = 'x',
  Y = 'y',
  Z = 'z',

  Apostrophe   = '\'',
  Comma        = ',',
  Minus        = '-',
  Period       = '.',
  Slash        = '/',
  Semicolon    = ';',
  Equal        = '=',
  LeftBracket  = '[',
  Backslash    = '\\',
  RightBracket = ']',
  Grave        = '`',

  // =====================================================================
  // Functional keys — Kitty PUA range (U+E000+)
  // =====================================================================

  // ---- Editing / navigation ----
  Insert   = 57348,
  Delete   = 57349,
  Left     = 57350,
  Right    = 57351,
  Up       = 57352,
  Down     = 57353,
  PageUp   = 57354,
  PageDown = 57355,
  Home     = 57356,
  End      = 57357,

  // ---- Locks ----
  CapsLock   = 57358,
  ScrollLock = 57359,
  NumLock    = 57360,

  // ---- System ----
  PrintScreen = 57361,
  Pause       = 57362,
  Menu        = 57363,

  // ---- Function keys F1..F35 ----
  F1  = 57364,
  F2  = 57365,
  F3  = 57366,
  F4  = 57367,
  F5  = 57368,
  F6  = 57369,
  F7  = 57370,
  F8  = 57371,
  F9  = 57372,
  F10 = 57373,
  F11 = 57374,
  F12 = 57375,
  F13 = 57376,
  F14 = 57377,
  F15 = 57378,
  F16 = 57379,
  F17 = 57380,
  F18 = 57381,
  F19 = 57382,
  F20 = 57383,
  F21 = 57384,
  F22 = 57385,
  F23 = 57386,
  F24 = 57387,
  F25 = 57388,
  F26 = 57389,
  F27 = 57390,
  F28 = 57391,
  F29 = 57392,
  F30 = 57393,
  F31 = 57394,
  F32 = 57395,
  F33 = 57396,
  F34 = 57397,
  F35 = 57398,

  // ---- Numeric keypad ----
  Kp0         = 57399,
  Kp1         = 57400,
  Kp2         = 57401,
  Kp3         = 57402,
  Kp4         = 57403,
  Kp5         = 57404,
  Kp6         = 57405,
  Kp7         = 57406,
  Kp8         = 57407,
  Kp9         = 57408,
  KpDecimal   = 57409,
  KpDivide    = 57410,
  KpMultiply  = 57411,
  KpSubtract  = 57412,
  KpAdd       = 57413,
  KpEnter     = 57414,
  KpEqual     = 57415,
  KpSeparator = 57416,
  KpLeft      = 57417,
  KpRight     = 57418,
  KpUp        = 57419,
  KpDown      = 57420,
  KpPageUp    = 57421,
  KpPageDown  = 57422,
  KpHome      = 57423,
  KpEnd       = 57424,
  KpInsert    = 57425,
  KpDelete    = 57426,
  KpBegin     = 57427,

  // ---- Media keys ----
  MediaPlay        = 57428,
  MediaPause       = 57429,
  MediaPlayPause   = 57430,
  MediaReverse     = 57431,
  MediaStop        = 57432,
  MediaFastForward = 57433,
  MediaRewind      = 57434,
  MediaTrackNext   = 57435,
  MediaTrackPrev   = 57436,
  MediaRecord      = 57437,
  LowerVolume      = 57438,
  RaiseVolume      = 57439,
  MuteVolume       = 57440,

  // ---- Modifier keys reported as standalone events ----
  LeftShift    = 57441,
  LeftControl  = 57442,
  LeftAlt      = 57443,
  LeftSuper    = 57444,
  LeftHyper    = 57445,
  LeftMeta     = 57446,
  RightShift   = 57447,
  RightControl = 57448,
  RightAlt     = 57449,
  RightSuper   = 57450,
  RightHyper   = 57451,
  RightMeta    = 57452,

  // ---- IME / layout switch keys ----
  IsoLevel3Shift = 57453,
  IsoLevel5Shift = 57454,

  MouseLeft   = 100000,
  MouseMiddle = 100001,
  MouseRight  = 100002
};

// Convenience: turn a raw codepoint from the CSI-u sequence into KittyKey.
constexpr Keycode toKeycode(std::uint32_t codepoint) noexcept {
  return static_cast<Keycode>(codepoint);
}

// Convenience: raw codepoint value.
constexpr std::uint32_t to_codepoint(Keycode k) noexcept {
  return static_cast<std::uint32_t>(k);
}

inline static void StopInputMode(void) {
  // Pop the kitty keyboard flags we pushed
  std::cout << "\x1b[<u" << std::flush;
  std::cout << "\x1b[?1016l\x1b[?1006l\x1b[?1003l" << std::flush;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
}

// Store the previous handler so we can chain to it.
static struct sigaction ginputs_prev_sigint;
static std::atomic<int> ginputs_sigint_count { 0 };

static void my_sigint_handler(int sig, siginfo_t *info, void *ctx) {
  // ---- your own work (must be async-signal-safe!) ----
  ginputs_sigint_count.fetch_add(1, std::memory_order_relaxed);
  StopInputMode();

  // ---- chain to the previous handler ----
  if (ginputs_prev_sigint.sa_flags & SA_SIGINFO) {
    if (ginputs_prev_sigint.sa_sigaction &&
        ginputs_prev_sigint.sa_sigaction != (void (*)(int, siginfo_t *, void *))SIG_DFL &&
        ginputs_prev_sigint.sa_sigaction != (void (*)(int, siginfo_t *, void *))SIG_IGN) {
      ginputs_prev_sigint.sa_sigaction(sig, info, ctx);
    }
  } else {
    auto h = ginputs_prev_sigint.sa_handler;
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
  if (sigaction(SIGINT, &sa, &ginputs_prev_sigint) != 0) {
    std::perror("sigaction");
  }
}

inline static void StartInputMode() {
  tcgetattr(STDIN_FILENO, &orig);
  atexit(StopInputMode);
  install_sigint_handler();

  struct termios raw = orig;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  raw.c_iflag &= ~(IXON | ICRNL);
  raw.c_cc[VMIN]  = 0;
  raw.c_cc[VTIME] = 1; // 100ms read timeout
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  std::cout << "\x1b[>11u" << std::flush;
  std::cout << "\x1b[?1003h\x1b[?1006h\x1b[?1016h" << std::flush;
}

enum class KeyState {
  RELEASED,
  JUST_PRESSED,
  PRESSED,
  JUST_RELEASED,
};

class InputController : Controller {
  private:
    std::unordered_map<Keycode, KeyState> keys;
    Vec2i mousePos = { -1, -1 };
    termios originalTerminal {};

  private:
    void loop() override {
      char buf[64];
      if (isPressed(Keycode::LeftControl) && isPressed(Keycode::C)) raise(SIGINT);

      pollfd input {};
      input.fd     = STDIN_FILENO;
      input.events = POLLIN;
      int result   = poll(&input, 1, 500);

      if (result == -1) {
        perror("poll");
        exit(EXIT_FAILURE);
      }

      if (result == 0) {
        return;
      }

      ssize_t bytesRead = read(STDIN_FILENO, buf, sizeof buf);

      if (bytesRead > 0) {
        std::string buffer(buf, bytesRead);
        size_t pos = 0;

        // Process all escape sequences in the buffer
        while (pos < buffer.size()) {
          // Find the next escape character
          size_t escIndex = buffer.find('\x1b', pos);
          if (escIndex == std::string::npos) {
            break;
          }

          // Check for mouse event: ESC[<mode;x;yM or ESC[<mode;x;ym
          if (escIndex + 2 < buffer.size() && buffer[escIndex + 1] == '[' &&
              buffer[escIndex + 2] == '<') {
            // Find the end of mouse sequence (M for press/move, m for release)
            size_t endIndex = std::string::npos;
            for (size_t i = escIndex + 3; i < buffer.size(); ++i) {
              if (buffer[i] == 'M' || buffer[i] == 'm') {
                endIndex = i;
                break;
              }
            }

            if (endIndex == std::string::npos) {
              break; // Incomplete sequence
            }

            // Extract the parameters: mode;x;y
            std::string params = buffer.substr(escIndex + 3, endIndex - escIndex - 3);
            int mode = 0, mouseX = 0, mouseY = 0;
            if (sscanf(params.c_str(), "%d;%d;%d", &mode, &mouseX, &mouseY) == 3) {
              bool released = (buffer[endIndex] == 'm');

              switch (mode) {
              case 0:
                if (released) {
                  keys[Keycode::MouseLeft] = KeyState::JUST_RELEASED;
                } else {
                  keys[Keycode::MouseLeft] = KeyState::JUST_PRESSED;
                }
                mousePos = { mouseX, mouseY };
                break;
              case 1:
                if (released) {
                  keys[Keycode::MouseMiddle] = KeyState::JUST_RELEASED;
                } else {
                  keys[Keycode::MouseMiddle] = KeyState::JUST_PRESSED;
                }
                mousePos = { mouseX, mouseY };
                break;
              case 2:
                if (released) {
                  keys[Keycode::MouseRight] = KeyState::JUST_RELEASED;
                } else {
                  keys[Keycode::MouseRight] = KeyState::JUST_PRESSED;
                }
                mousePos = { mouseX, mouseY };
                break;
              case 35:
                mousePos = { mouseX, mouseY };
                break;
              }
            }

            pos = endIndex + 1;
            continue;
          }

          // Find the end of keyboard escape sequence (ends with 'u' for press or '~' for
          // release in kitty protocol)
          size_t endIndex = std::string::npos;
          for (size_t i = escIndex + 1; i < buffer.size(); ++i) {
            if (buffer[i] == 'u' || buffer[i] == '~') {
              endIndex = i;
              break;
            }
          }

          // If we didn't find the end, the sequence is incomplete - wait for more data
          if (endIndex == std::string::npos) {
            break;
          }

          // Extract the escape sequence (without the ESC character)
          std::string sequence = buffer.substr(escIndex + 1, endIndex - escIndex);

          // Parse the keycode: format is "[keycode;modifiers:eventtype" followed by 'u'
          // or '~'
          size_t rawKeycode = 0;
          if (sscanf(sequence.c_str(), "[%zu;", &rawKeycode) == 1 ||
              sscanf(sequence.c_str(), "[%zu:", &rawKeycode) == 1 ||
              sscanf(sequence.c_str(), "[%zu", &rawKeycode) == 1) {

            Keycode keycode = toKeycode(rawKeycode);

            // Find the event type indicator (after the last colon or semicolon)
            size_t colonPos = sequence.rfind(':');
            char eventType  = 0;
            if (colonPos != std::string::npos && colonPos + 1 < sequence.size()) {
              // Event type is the character after the colon: 1=press, 2=repeat, 3=release
              eventType = sequence[colonPos + 1];
            }

            char terminator = buffer[endIndex];

            // Determine key state based on event type or terminator
            if (eventType == '3' || (eventType == 0 && terminator == '~')) {
              keys[keycode] = KeyState::JUST_RELEASED;
            } else {
              if (keys[keycode] != KeyState::PRESSED) {
                keys[keycode] = KeyState::JUST_PRESSED;
              }
            }
          }

          // Move past this sequence to process the next one
          pos = endIndex + 1;
        }
      }
    }

  public:
    InputController() {
      StartInputMode();
      Start();
    }

    ~InputController() {
      Stop();
      StopInputMode();
    }

    InputController(const InputController &)            = delete;
    InputController &operator=(const InputController &) = delete;

    void refresh() {
      for (auto &[_, key] : keys) {
        if (key == KeyState::JUST_PRESSED)
          key = KeyState::PRESSED;
        else if (key == KeyState::JUST_RELEASED)
          key = KeyState::RELEASED;
      }
    }

    const bool isJustPressed(Keycode c) {
      return keys[c] == KeyState::JUST_PRESSED;
    }
    const bool isJustReleased(Keycode c) {
      return keys[c] == KeyState::JUST_RELEASED;
    }
    const bool isPressed(Keycode c) {
      return keys[c] == KeyState::PRESSED || isJustPressed(c);
    }
    const bool isReleased(Keycode c) {
      return keys[c] == KeyState::RELEASED || isJustPressed(c);
    }

    const Vec2i &getMousePos() {
      return mousePos;
    }
};

} // namespace Inputs
