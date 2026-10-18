#include <M5Cardputer.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>
#include "tinyexpr.h"

#define VERSION   "1.7"

#define SCR_W     240
#define SCR_H     135
#define EXPR_Y    24
#define EXPR_SIZE 2
#define RES_Y     72
#define STATUS_Y  122

enum AppState {
    STATE_CALC,
    STATE_FORMULA_MENU,
    STATE_FRACTION_INPUT,
    STATE_FORMULA_VARS,
    STATE_HELP
};

struct Formula {
    const char* name;
    const char* desc;
    const char* vars[4];
    int varCount;
};

static const Formula formulas[] = {
    { "Pythagoras",    "c=sqrt(a^2+b^2)",          {"a", "b", "", ""},          2 },
    { "Discriminant",  "D=b^2-4ac",                {"a", "b", "c", ""},         3 },
    { "Vieta sum",     "x1+x2=-b/a",               {"a", "b", "", ""},          2 },
    { "Vieta product", "x1*x2=c/a",                {"a", "c", "", ""},          2 },
    { "Circle area",   "S=pi*r^2",                 {"r", "", "", ""},            1 },
    { "Circumference", "C=2*pi*r",                 {"r", "", "", ""},            1 },
    { "Law of cos",    "c=sqrt(a^2+b^2-2ab*cosC)", {"a", "b", "C_deg", ""},     3 },
    { "Law of sin",    "b=a*sin(B)/sin(A)",         {"a", "A_deg", "B_deg", ""}, 3 },
    { "Sphere volume", "V=(4/3)*pi*r^3",            {"r", "", "", ""},            1 },
    { "Trapezoid area","S=(a+b)/2*h",               {"a", "b", "h", ""},          3 },
    { "Log base b",    "log_b(x)=ln(x)/ln(b)",      {"x", "b", "", ""},          2 },
    { "Nth root",      "x^(1/n)",                   {"x", "n", "", ""},           2 },
};
static const int FORMULA_COUNT = 12;

static AppState    appState    = STATE_CALC;
static std::string expression  = "";
static std::string resultStr   = "";
static bool        hasError    = false;
static bool        useDegrees  = true;
static int         formulaIdx  = 0;

static int         fracStep    = 0;
static std::string fracNum     = "";
static std::string fracDen     = "";

static int         fvStep      = 0;
static double      fvValues[4] = {0, 0, 0, 0};
static std::string fvInput     = "";

static M5Canvas    canvas(&M5Cardputer.Display);

static int         cachedBatPct = 100;
static unsigned long lastBatCheck = 0;

// --- Battery helper ---

static int getCachedBattery() {
    unsigned long now = millis();
    if (now - lastBatCheck > 10000 || lastBatCheck == 0) {
        lastBatCheck = now;
        int pct = M5Cardputer.Power.getBatteryLevel();
        if (pct >= 0 && pct <= 100) cachedBatPct = pct;
        else cachedBatPct = -1; // Charging / external power
    }
    return cachedBatPct;
}

// --- Custom Math Functions for TinyExpr ---

static double my_sin(double x) {
    if (useDegrees) x = x * (M_PI / 180.0);
    return sin(x);
}

static double my_cos(double x) {
    if (useDegrees) {
        double rem = fmod(fabs(x), 360.0);
        if (fabs(rem - 90.0) < 1e-9 || fabs(rem - 270.0) < 1e-9) return 0.0;
        x = x * (M_PI / 180.0);
    }
    return cos(x);
}

static double my_tan(double x) {
    if (useDegrees) {
        double rem = fmod(fabs(x), 360.0);
        if (fabs(rem - 90.0) < 1e-9 || fabs(rem - 270.0) < 1e-9) return NAN;
        x = x * (M_PI / 180.0);
    }
    return tan(x);
}

static double my_asin(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    double r = asin(x);
    if (useDegrees) r = r * (180.0 / M_PI);
    return r;
}

static double my_acos(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    double r = acos(x);
    if (useDegrees) r = r * (180.0 / M_PI);
    return r;
}

static double my_atan(double x) {
    double r = atan(x);
    if (useDegrees) r = r * (180.0 / M_PI);
    return r;
}

static double my_cbrt(double x) {
    return cbrt(x);
}

static double my_round(double x) {
    return round(x);
}

static double my_pi() {
    return M_PI;
}

static double my_e() {
    return M_E;
}

static const te_variable custom_vars[] = {
    {"E",     (const void*)my_e,     TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"PI",    (const void*)my_pi,    TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"acos",  (const void*)my_acos,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"asin",  (const void*)my_asin,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan",  (const void*)my_atan,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cbrt",  (const void*)my_cbrt,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cos",   (const void*)my_cos,   TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"round", (const void*)my_round, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sin",   (const void*)my_sin,   TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tan",   (const void*)my_tan,   TE_FUNCTION1 | TE_FLAG_PURE, 0},
};

// --- Expression Preprocessing (Implicit Multiplication & Normalization) ---

static std::string preprocess(const std::string& in) {
    std::string s;
    for (char c : in) {
        if (!isspace((unsigned char)c)) s += c;
    }
    if (s.empty()) return "";

    std::string out;
    int n = s.size();
    for (int i = 0; i < n; i++) {
        out += s[i];
        if (i + 1 < n) {
            char curr = s[i];
            char next = s[i + 1];

            bool currIsDigit = isdigit((unsigned char)curr) || curr == '.';
            bool nextIsDigit = isdigit((unsigned char)next) || next == '.';
            bool currIsAlpha = isalpha((unsigned char)curr) || curr == '_';
            bool nextIsAlpha = isalpha((unsigned char)next) || next == '_';

            // 1) 2( -> 2*(
            if (currIsDigit && next == '(') {
                out += '*';
            }
            // 2) )2 -> )*2  or  )( -> )*(
            else if (curr == ')' && (nextIsDigit || next == '(')) {
                out += '*';
            }
            // 3) )pi -> )*pi  or  )sin( -> )*sin(
            else if (curr == ')' && nextIsAlpha) {
                out += '*';
            }
            // 4) 2pi -> 2*pi, 2sqrt( -> 2*sqrt(
            // Don't insert * if scientific notation: e.g. 1e5, 2.5e-3
            else if (currIsDigit && nextIsAlpha) {
                bool isSci = false;
                if ((next == 'e' || next == 'E') && i + 2 < n) {
                    char afterE = s[i + 2];
                    if (isdigit((unsigned char)afterE) || afterE == '+' || afterE == '-') {
                        isSci = true;
                    }
                }
                if (!isSci) {
                    out += '*';
                }
            }
        }
    }
    return out;
}

static std::string fmtNum(double v) {
    if (isnan(v)) return "Error (NaN)";
    if (isinf(v)) return (v < 0) ? "-Infinity" : "Infinity";
    if (fabs(v) < 1e-12) v = 0.0;
    char buf[32];
    if (v == floor(v) && fabs(v) < 1e14) {
        snprintf(buf, sizeof(buf), "%.0f", v);
    } else {
        snprintf(buf, sizeof(buf), "%.10g", v);
    }
    return buf;
}

static double evaluate(const std::string& rawExpr, std::string& err) {
    std::string expr = preprocess(rawExpr);
    if (expr.empty()) {
        err = "Empty expr";
        return 0;
    }
    int errPos = 0;
    int varCount = sizeof(custom_vars) / sizeof(custom_vars[0]);
    te_expr* n = te_compile(expr.c_str(), custom_vars, varCount, &errPos);
    if (!n) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Syntax error at %d", errPos);
        err = buf;
        return 0;
    }
    double val = te_eval(n);
    te_free(n);

    if (isnan(val)) {
        err = "Math error (NaN)";
        return 0;
    }
    if (isinf(val)) {
        err = "Div by 0 (Inf)";
        return 0;
    }
    return val;
}

static double computeFormula(int idx, double* v, std::string& err) {
    const double D = M_PI / 180.0;
    switch(idx) {
        case 0: return sqrt(v[0]*v[0] + v[1]*v[1]);
        case 1: return v[1]*v[1] - 4.0*v[0]*v[2];
        case 2: if (v[0] == 0) { err = "a=0"; return 0; } return -v[1]/v[0];
        case 3: if (v[0] == 0) { err = "a=0"; return 0; } return v[1]/v[0];
        case 4: if (v[0] < 0) { err = "r < 0"; return 0; } return M_PI*v[0]*v[0];
        case 5: if (v[0] < 0) { err = "r < 0"; return 0; } return 2.0*M_PI*v[0];
        case 6: {
            double under = v[0]*v[0] + v[1]*v[1] - 2.0*v[0]*v[1]*cos(v[2]*D);
            if (under < 0) { err = "Invalid triangle"; return 0; }
            return sqrt(under);
        }
        case 7: {
            double sA = sin(v[1]*D);
            if (fabs(sA) < 1e-9) { err = "sin(A)=0"; return 0; }
            return v[0] * sin(v[2]*D) / sA;
        }
        case 8: if (v[0] < 0) { err = "r < 0"; return 0; } return (4.0/3.0)*M_PI*v[0]*v[0]*v[0];
        case 9: return (v[0] + v[1]) / 2.0 * v[2];
        case 10: {
            if (v[0] <= 0) { err = "x <= 0"; return 0; }
            if (v[1] <= 0 || v[1] == 1.0) { err = "b<=0 or b=1"; return 0; }
            return log(v[0]) / log(v[1]);
        }
        case 11: {
            if (v[1] == 0) { err = "n = 0"; return 0; }
            if (v[0] < 0) {
                long int n_int = lround(v[1]);
                if (fabs(v[1] - (double)n_int) < 1e-9 && (n_int % 2 != 0)) {
                    return -pow(-v[0], 1.0 / v[1]);
                } else {
                    err = "x<0 with even n";
                    return 0;
                }
            }
            return pow(v[0], 1.0 / v[1]);
        }
    }
    err = "Unknown formula";
    return 0;
}

// --- Screen Drawing (All using M5Canvas for smooth flicker-free UI) ---

static void drawCalc() {
    canvas.fillScreen(TFT_BLACK);

    // Header bar
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_CYAN);
    canvas.setCursor(2, 2);
    canvas.print("fn+M:form fn+F:frac fn+H:help");

    // DEG/RAD badge in header
    canvas.setTextColor(useDegrees ? TFT_GREEN : TFT_YELLOW);
    canvas.setCursor(195, 2);
    canvas.print(useDegrees ? "[DEG]" : "[RAD]");

    canvas.drawLine(0, 12, SCR_W, 12, TFT_DARKGREY);

    // Expression input with cursor
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(EXPR_SIZE);
    std::string disp = expression + "_";
    if (disp.size() > 19) disp = "..." + disp.substr(disp.size() - 16);
    canvas.setCursor(2, EXPR_Y);
    canvas.print(disp.c_str());

    canvas.drawLine(0, RES_Y - 4, SCR_W, RES_Y - 4, TFT_DARKGREY);

    // Result output
    canvas.setTextSize(2);
    canvas.setTextColor(hasError ? TFT_RED : TFT_GREEN);
    std::string res = resultStr;
    if (res.size() > 19) res = res.substr(0, 16) + "...";
    canvas.setCursor(2, RES_Y);
    canvas.print(res.c_str());

    // Status bar at bottom
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_DARKGREY);
    char botRight[32];
    int bat = getCachedBattery();
    if (bat >= 0) snprintf(botRight, sizeof(botRight), "v" VERSION " %d%%", bat);
    else          snprintf(botRight, sizeof(botRight), "v" VERSION " CHG");

    int brX = SCR_W - (int)strlen(botRight) * 6 - 2;
    canvas.setCursor(brX, STATUS_Y);
    canvas.print(botRight);

    canvas.pushSprite(0, 0);
}

static void drawFormulaMenu() {
    canvas.fillScreen(TFT_BLACK);
    canvas.setTextColor(TFT_YELLOW);
    canvas.setTextSize(1);
    canvas.setCursor(2, 2);
    canvas.print("FORMULAS | fn+; / fn+. = Nav | Del=Back");
    canvas.drawLine(0, 11, SCR_W, 11, TFT_DARKGREY);

    int start = std::max(0, formulaIdx - 2);
    int end = std::min(FORMULA_COUNT, start + 5);
    for (int i = start; i < end; i++) {
        int y = 14 + (i - start) * 21;
        if (i == formulaIdx) {
            canvas.fillRect(0, y - 1, SCR_W, 19, TFT_NAVY);
            canvas.setTextColor(TFT_WHITE);
        } else {
            canvas.setTextColor(TFT_LIGHTGREY);
        }
        canvas.setCursor(4, y);
        canvas.print(formulas[i].name);
        canvas.setTextColor(TFT_DARKGREY);
        canvas.setCursor(4, y + 10);
        canvas.print(formulas[i].desc);
    }
    canvas.pushSprite(0, 0);
}

static void drawFraction() {
    canvas.fillScreen(TFT_BLACK);
    canvas.setTextColor(TFT_YELLOW);
    canvas.setTextSize(1);
    canvas.setCursor(2, 2);
    canvas.print("FRACTION INPUT | Enter=Next Del=Back");
    canvas.drawLine(0, 11, SCR_W, 11, TFT_DARKGREY);

    std::string top = fracNum + (fracStep == 0 ? "_" : "");
    std::string bot = fracDen + (fracStep == 1 ? "_" : "");
    if (top.size() > 15) top = "..." + top.substr(top.size() - 12);
    if (bot.size() > 15) bot = "..." + bot.substr(bot.size() - 12);

    canvas.setTextSize(2);
    canvas.setTextColor(fracStep == 0 ? TFT_WHITE : TFT_LIGHTGREY);
    canvas.setCursor(10, 35);
    canvas.print(top.c_str());

    canvas.drawLine(10, 62, 230, 62, TFT_WHITE);

    canvas.setTextColor(fracStep == 1 ? TFT_WHITE : TFT_LIGHTGREY);
    canvas.setCursor(10, 75);
    canvas.print(bot.c_str());

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_DARKGREY);
    canvas.setCursor(2, STATUS_Y);
    canvas.print("Enter: Next/Insert   fn+C: Cancel");

    canvas.pushSprite(0, 0);
}

static void drawFormulaVars() {
    canvas.fillScreen(TFT_BLACK);
    canvas.setTextColor(TFT_YELLOW);
    canvas.setTextSize(1);
    canvas.setCursor(2, 2);
    canvas.print(formulas[formulaIdx].name);
    canvas.drawLine(0, 12, SCR_W, 12, TFT_DARKGREY);

    for (int i = 0; i < fvStep && i < formulas[formulaIdx].varCount; i++) {
        canvas.setTextColor(TFT_GREEN);
        canvas.setCursor(2, 16 + i * 14);
        char buf[40];
        snprintf(buf, sizeof(buf), "%s = %s", formulas[formulaIdx].vars[i], fmtNum(fvValues[i]).c_str());
        canvas.print(buf);
    }

    if (fvStep < formulas[formulaIdx].varCount) {
        canvas.setTextColor(TFT_WHITE);
        canvas.setTextSize(2);
        canvas.setCursor(2, 75);
        std::string prompt = std::string(formulas[formulaIdx].vars[fvStep]) + " = " + fvInput + "_";
        canvas.print(prompt.c_str());
    }

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_DARKGREY);
    canvas.setCursor(2, STATUS_Y);
    canvas.print("Enter: OK   Del: Back/Undo   fn+C: Cancel");

    canvas.pushSprite(0, 0);
}

static void drawHelp() {
    canvas.fillScreen(TFT_BLACK);
    canvas.setTextColor(TFT_CYAN);
    canvas.setTextSize(1);
    canvas.setCursor(2, 2);
    canvas.print("SHORTCUTS HELP | Press any key to exit");
    canvas.drawLine(0, 11, SCR_W, 11, TFT_DARKGREY);

    canvas.setTextColor(TFT_WHITE);
    int y = 14;
    canvas.setCursor(2, y);      canvas.print("fn+M: Formulas   fn+F: Fraction");
    canvas.setCursor(2, y + 11); canvas.print("fn+D: DEG / RAD  fn+C: Clear / Cancel");
    canvas.setCursor(2, y + 22); canvas.print("fn+R: sqrt()     fn+P: pi");
    canvas.setCursor(2, y + 33); canvas.print("fn+S: sin()      fn+O: cos()");
    canvas.setCursor(2, y + 44); canvas.print("fn+T: tan()      fn+L: ln()");
    canvas.setCursor(2, y + 55); canvas.print("fn+^: power      fn+H: Help");
    canvas.setCursor(2, y + 66); canvas.print("fn+;: Up (Menu)  fn+.: Down (Menu)");
    canvas.setCursor(2, y + 77); canvas.print("Enter: Eval/OK   Del: Backspace/Back");

    canvas.setTextColor(TFT_ORANGE);
    canvas.setCursor(2, y + 92); canvas.print("Extra: sinh, cosh, exp, cbrt, abs...");

    canvas.pushSprite(0, 0);
}

// --- Keyboard Input Handlers ---

static void handleCalcKey(Keyboard_Class::KeysState& s) {
    if (s.fn) {
        for (char c : s.word) {
            switch(c) {
                case 'c': case 'C': expression = ""; resultStr = ""; hasError = false; break;
                case 'f': case 'F': fracStep = 0; fracNum = ""; fracDen = ""; appState = STATE_FRACTION_INPUT; break;
                case 'r': case 'R': expression += "sqrt("; break;
                case 'p': case 'P': expression += "pi"; break;
                case 'm': case 'M': formulaIdx = 0; appState = STATE_FORMULA_MENU; break;
                case 'l': case 'L': expression += "ln("; break;
                case 's': case 'S': expression += "sin("; break;
                case 'o': case 'O': expression += "cos("; break;
                case 't': case 'T': expression += "tan("; break;
                case 'h': case 'H': appState = STATE_HELP; break;
                case 'd': case 'D': useDegrees = !useDegrees; break;
                case '^':           expression += "^"; break;
            }
        }
        return;
    }
    if (s.enter) {
        if (expression.empty()) return;
        std::string err;
        double result = evaluate(expression, err);
        if (!err.empty()) {
            resultStr = err;
            hasError = true;
        } else {
            resultStr = "= " + fmtNum(result);
            hasError = false;
        }
        return;
    }
    if (s.del) {
        if (!expression.empty()) expression.pop_back();
        resultStr = "";
        hasError = false;
        return;
    }
    for (char c : s.word) {
        if (c >= 32 && c < 127) {
            expression += c;
            resultStr = "";
            hasError = false;
        }
    }
}

static void handleFractionKey(Keyboard_Class::KeysState& s) {
    std::string& cur = (fracStep == 0) ? fracNum : fracDen;
    if (s.fn) {
        for (char c : s.word) {
            switch(c) {
                case 'r': case 'R': cur += "sqrt("; break;
                case 'p': case 'P': cur += "pi"; break;
                case 'l': case 'L': cur += "ln("; break;
                case 's': case 'S': cur += "sin("; break;
                case 'o': case 'O': cur += "cos("; break;
                case 't': case 'T': cur += "tan("; break;
                case '^':           cur += "^"; break;
                case 'c': case 'C':
                    fracNum = ""; fracDen = ""; fracStep = 0;
                    appState = STATE_CALC;
                    return;
            }
        }
        return;
    }
    if (s.del) {
        if (!cur.empty()) {
            cur.pop_back();
            return;
        }
        if (fracStep == 1) {
            fracStep = 0;
            return;
        }
        appState = STATE_CALC;
        return;
    }
    if (s.enter) {
        if (fracStep == 0 && !fracNum.empty()) {
            fracStep = 1;
            return;
        }
        if (fracStep == 1 && !fracDen.empty()) {
            // Implicit multiplication with existing expression if needed
            if (!expression.empty()) {
                char last = expression.back();
                if (isdigit((unsigned char)last) || last == ')' || isalpha((unsigned char)last)) {
                    expression += "*";
                }
            }
            expression += "(" + fracNum + ")/(" + fracDen + ")";
            resultStr = "";
            hasError = false;
            appState = STATE_CALC;
        }
        return;
    }
    for (char c : s.word) {
        if (c >= 32 && c < 127) cur += c;
    }
}

static void handleFormulaMenuKey(Keyboard_Class::KeysState& s) {
    if (s.del) {
        appState = STATE_CALC;
        return;
    }
    if (s.fn) {
        for (char c : s.word) {
            if (c == ';') formulaIdx = std::max(0, formulaIdx - 1);
            if (c == '.') formulaIdx = std::min(FORMULA_COUNT - 1, formulaIdx + 1);
            if (c == 'c' || c == 'C') { appState = STATE_CALC; return; }
        }
        return;
    }
    if (s.enter) {
        fvStep = 0;
        fvInput = "";
        memset(fvValues, 0, sizeof(fvValues));
        appState = STATE_FORMULA_VARS;
    }
}

static void handleFormulaVarsKey(Keyboard_Class::KeysState& s) {
    if (s.fn) {
        for (char c : s.word) {
            if (c == 'c' || c == 'C') {
                appState = STATE_CALC;
                return;
            }
        }
    }
    if (s.del) {
        if (!fvInput.empty()) {
            fvInput.pop_back();
        } else if (fvStep > 0) {
            fvStep--;
            fvInput = fmtNum(fvValues[fvStep]);
        } else {
            appState = STATE_FORMULA_MENU;
        }
        return;
    }
    if (s.enter) {
        if (fvInput.empty()) return;
        fvValues[fvStep] = atof(fvInput.c_str());
        fvInput = "";
        fvStep++;
        if (fvStep >= formulas[formulaIdx].varCount) {
            std::string err;
            double res = computeFormula(formulaIdx, fvValues, err);
            if (!err.empty()) {
                resultStr = err;
                hasError = true;
            } else {
                resultStr = "= " + fmtNum(res);
                hasError = false;
                expression = fmtNum(res);
            }
            appState = STATE_CALC;
        }
        return;
    }
    for (char c : s.word) {
        bool okDot = (c == '.' && fvInput.find('.') == std::string::npos);
        bool okNeg = (c == '-' && fvInput.empty());
        if ((isdigit((unsigned char)c) || okDot || okNeg) && fvInput.size() < 12) {
            fvInput += c;
        }
    }
}

// --- Arduino Lifecycle ---

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);

    canvas.createSprite(SCR_W, SCR_H);
    canvas.setTextDatum(TL_DATUM);

    canvas.fillScreen(TFT_BLACK);
    canvas.setTextColor(TFT_CYAN);
    canvas.setTextSize(2);
    canvas.setCursor(20, 35);
    canvas.print("CalcPuter v" VERSION);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(20, 65);
    canvas.print("Fast, Precise & Flicker-Free");
    canvas.pushSprite(0, 0);

    delay(800);
    drawCalc();
}

void loop() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();

    switch (appState) {
        case STATE_CALC:
            handleCalcKey(s);
            break;
        case STATE_FORMULA_MENU:
            handleFormulaMenuKey(s);
            break;
        case STATE_FRACTION_INPUT:
            handleFractionKey(s);
            break;
        case STATE_FORMULA_VARS:
            handleFormulaVarsKey(s);
            break;
        case STATE_HELP:
            // Any key pressed in HELP returns to CALC
            if (s.word.size() > 0 || s.enter || s.del || s.fn) {
                appState = STATE_CALC;
            }
            break;
    }

    // Always render the current state smoothly via double-buffered sprite
    switch (appState) {
        case STATE_CALC:           drawCalc(); break;
        case STATE_FORMULA_MENU:   drawFormulaMenu(); break;
        case STATE_FRACTION_INPUT: drawFraction(); break;
        case STATE_FORMULA_VARS:   drawFormulaVars(); break;
        case STATE_HELP:           drawHelp(); break;
    }

    delay(10);
}
