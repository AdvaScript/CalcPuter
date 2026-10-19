# 🧮 CalcPuter

A feature-rich calculator firmware for the **M5Stack Cardputer**, built in C++. Supports expression evaluation, built-in math formulas, fraction input, and a help screen — all designed for the Cardputer's tiny keyboard and display.

---

## 📸 Screenshots

| Main Calculator | Formula Menu | Help Screen |
|:-:|:-:|:-:|
| ![Main Menu](photos/generalmenu.jpg) | ![Formulas](photos/formulas.jpg) | ![Help](photos/helpmenu.jpg) |

---

## ✨ Features

### 🔢 Expression Calculator
- Type and evaluate full math expressions like `2*(3+4)^2`
- Supports operator precedence, parentheses, and negative numbers
- Displays the expression on top and the result below
- Error messages for invalid input (e.g. division by zero, syntax errors)

### 📐 Built-in Formulas (12 total)
Navigate a formula menu, enter variables, and get instant results:

| Formula | Expression |
|---|---|
| Pythagorean theorem | `c = sqrt(a² + b²)` |
| Discriminant | `D = b² - 4ac` |
| Vieta's sum | `x1 + x2 = -b/a` |
| Vieta's product | `x1 * x2 = c/a` |
| Circle area | `S = π·r²` |
| Circumference | `C = 2·π·r` |
| Law of cosines | `c = sqrt(a²+b²−2ab·cos C)` |
| Law of sines | `b = a·sin(B)/sin(A)` |
| Sphere volume | `V = (4/3)·π·r³` |
| Trapezoid area | `S = (a+b)/2·h` |
| Logarithm base b | `log_b(x) = ln(x)/ln(b)` |
| Nth root | `x^(1/n)` |

### ➗ Fraction Input Mode
Enter a numerator and denominator separately — the result is inserted directly into the expression as `(num)/(den)`, so you can keep computing with it. Supports full math inside each part (trig, sqrt, pi, etc.).

### ⌨️ Keyboard Shortcuts (Fn + key)
| Shortcut | Action |
|---|---|
| `Fn + H` | Open Help screen |
| `Fn + M` | Open Formula menu |
| `Fn + F` | Fraction input mode |
| `Fn + R` | Insert `sqrt(` |
| `Fn + S` | Insert `sin(` |
| `Fn + O` | Insert `cos(` |
| `Fn + T` | Insert `tan(` |
| `Fn + L` | Insert `ln(` |
| `Fn + P` | Insert `pi` |
| `Fn + D` | Toggle DEG / RAD mode |
| `Fn + C` | Clear expression / Cancel |
| `Fn + ^` | Insert `^` (exponent) |
| `Fn + ; / .` | Up / Down navigation in Formula menu |
| `Enter` | Evaluate expression / Next step |
| `Del` | Backspace / Undo step / Back |

### 🧮 Supported Math Functions
`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `sinh`, `cosh`, `tanh`, `sqrt`, `cbrt`, `ln`, `log` (base 10), `abs`, `floor`, `ceil`, `round`, `exp`, `fac` (factorial), `ncr`, `npr`

Constants: `pi`, `PI`, `e`, `E`

---

## 🛠️ Building & Flashing

### Requirements
- [PlatformIO](https://platformio.org/) or Arduino IDE with ESP32 board support
- M5Stack library: `M5Cardputer`

### PlatformIO (recommended)
```ini
[env:m5stack-cardputer]
platform = espressif32
board = m5stack-stamps3
framework = arduino
build_flags = 
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DTE_POW_FROM_RIGHT
lib_deps = 
    m5stack/M5Cardputer@^1.1.1
```

```bash
pio run --target upload
```

### Arduino IDE
1. Install board: `ESP32 by Espressif` via Board Manager
2. Install library: `M5Cardputer` via Library Manager
3. Select board: **M5Stack Cardputer**
4. Upload `main.cpp`

---

## 📦 Hardware

| | |
|---|---|
| **Device** | M5Stack Cardputer |
| **MCU** | ESP32-S3 |
| **Display** | 240×135 TFT |
| **Input** | Built-in QWERTY keyboard |

---

## 📁 Project Structure

```
├── src/
|   ├── main.cpp
|   ├── tinyexpr.c
|   └── tinyexpr.h
├── photos/
│   ├── generalmenu.jpg
│   ├── formulas.jpg
│   └── helpmenu.jpg
└── README.md
```

---

## 📝 Changelog

### v1.7.1
- **Cardputer & Cardputer ADV Compatibility**: Fixed boot failure on actual hardware by disabling unused audio peripherals (ES8311 codec / microphone) to free DMA RAM and prevent I2C bus hangs on boot.
- **Bootloader & Partition Integrity**: Merged binary now properly bundles `boot_app0.bin` at offset `0xe000` to ensure ESP32-S3 OTA bootloader boots reliably on virgin devices and in M5Burner.
- **Math Engine (TinyExpr)**: Replaced buggy custom parser with robust TinyExpr engine. Correct operator precedence, right-associative power (`^`), correct unary minus in expressions like `2^-3` and `-2^2`.
- **Implicit Multiplication**: Automatically parses `2pi`, `2(3+4)`, `)2`, and fraction insertions seamlessly.
- **Angle Modes**: Added toggle between **DEG** (Degrees) and **RAD** (Radians) via `Fn + D`, with visual status indicator.
- **Flicker-Free Rendering**: Implemented double-buffering using `M5Canvas` with safe fallback to direct drawing.
- **Domain Validation**: Proper math error reporting for out-of-range functions (`asin`, `acos`, `tan(90)`) instead of silent NaNs or overflow.
- **UI & UX Improvements**: Fixed Help screen state machine lockup, added undo/back navigation in formula inputs, and added interactive cursor.

---

## 📜 License

MIT License — do whatever you want, attribution appreciated.
