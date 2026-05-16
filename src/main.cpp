#include <M5Cardputer.h>
#include <math.h>
#include <string>
#include <vector>
#include <stack>

#define VERSION  "1.6r"

#define SCR_W    240
#define SCR_H    135
#define EXPR_Y   22
#define EXPR_SIZE 2
#define RES_Y    70
#define STATUS_Y 122

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

Formula formulas[] = {
    { "Pythagoras",    "c=sqrt(a^2+b^2)",           {"a", "b", "", ""},        2 },
    { "Discriminant",  "D=b^2-4ac",                 {"a", "b", "c", ""},       3 },
    { "Vieta sum",     "x1+x2=-b/a",                {"a", "b", "", ""},        2 },
    { "Vieta product", "x1*x2=c/a",                 {"a", "c", "", ""},        2 },
    { "Circle area",   "S=pi*r^2",                  {"r", "", "", ""},          1 },
    { "Circumference", "C=2*pi*r",                  {"r", "", "", ""},          1 },
    { "Law of cos",    "c=sqrt(a^2+b^2-2ab*cosC)",  {"a", "b", "C_deg", ""},   3 },
    { "Law of sin",    "b=a*sin(B)/sin(A)",          {"a", "A_deg", "B_deg", ""}, 3 },
    { "Sphere volume", "V=(4/3)*pi*r^3",             {"r", "", "", ""},          1 },
    { "Trapezoid area","S=(a+b)/2*h",                {"a", "b", "h", ""},        3 },
    { "Log base b",    "log_b(x)=ln(x)/ln(b)",       {"x", "b", "", ""},        2 },
    { "Nth root",      "x^(1/n)",                    {"x", "n", "", ""},         2 },
};
const int FORMULA_COUNT = 12;

AppState    appState   = STATE_CALC;
std::string expression = "";
std::string resultStr  = "";
bool        hasError   = false;
int         formulaIdx = 0;

int         fracStep   = 0;
std::string fracNum    = "";
std::string fracDen    = "";

int         fvStep     = 0;
double      fvValues[4]= {0,0,0,0};
std::string fvInput    = "";

struct Token {
    enum Type { NUM, OP, FUNC, LPAREN, RPAREN } type;
    double      num;
    char        op;
    std::string func;
};

// --- Core Math Logic ---

static std::vector<Token> tokenize(const std::string& expr, std::string& err) {
    std::vector<Token> toks;
    int i=0, n=expr.size();
    while (i<n) {
        if (isspace(expr[i])) { i++; continue; }
        if (isalpha(expr[i])) {
            std::string w;
            while (i<n && (isalpha(expr[i])||isdigit(expr[i])||expr[i]=='_')) w+=expr[i++];
            if (w=="pi"||w=="PI") { Token t; t.type=Token::NUM; t.num=M_PI; toks.push_back(t); }
            else                  { Token t; t.type=Token::FUNC; t.func=w;  toks.push_back(t); }
            continue;
        }
        if (isdigit(expr[i])||expr[i]=='.') {
            std::string ns;
            while (i<n&&(isdigit(expr[i])||expr[i]=='.')) ns+=expr[i++];
            Token t; t.type=Token::NUM; t.num=atof(ns.c_str()); toks.push_back(t);
            continue;
        }
        if (expr[i]=='(') { Token t; t.type=Token::LPAREN; toks.push_back(t); i++; continue; }
        if (expr[i]==')') { Token t; t.type=Token::RPAREN; toks.push_back(t); i++; continue; }
        if (strchr("+-*/^",expr[i])) {
            if (expr[i]=='-') {
                bool unary = toks.empty()
                ||toks.back().type==Token::OP
                ||toks.back().type==Token::LPAREN
                ||toks.back().type==Token::FUNC;
                if (unary) {
                    Token a; a.type=Token::NUM; a.num=-1; toks.push_back(a);
                    Token b; b.type=Token::OP;  b.op='*'; toks.push_back(b);
                    i++; continue;
                }
            }
            Token t; t.type=Token::OP; t.op=expr[i]; toks.push_back(t); i++; continue;
        }
        char msg[32]; snprintf(msg,sizeof(msg),"Unknown: %c",expr[i]);
        err=msg; return {};
    }
    return toks;
}

static int prec(char op) {
    if (op=='+'||op=='-') return 1;
    if (op=='*'||op=='/') return 2;
    if (op=='^')          return 3;
    return 0;
}

static double applyOp(double a,char op,double b,std::string& err) {
    if (op=='+') return a+b;
    if (op=='-') return a-b;
    if (op=='*') return a*b;
    if (op=='/') { if(b==0){err="Div by 0";return 0;} return a/b; }
    if (op=='^') return pow(a,b);
    err="Bad op"; return 0;
}

static double applyFunc(const std::string& f,double x,std::string& err) {
    const double D=M_PI/180.0;
    if (f=="sin")   return sin(x*D);
    if (f=="cos")   return cos(x*D);
    if (f=="tan")   return tan(x*D);
    if (f=="asin")  return asin(x)/D;
    if (f=="acos")  return acos(x)/D;
    if (f=="atan")  return atan(x)/D;
    if (f=="sqrt")  { if(x<0){err="sqrt<0";return 0;} return sqrt(x); }
    if (f=="ln")    { if(x<=0){err="ln<=0";return 0;} return log(x); }
    if (f=="log")   { if(x<=0){err="log<=0";return 0;} return log10(x); }
    if (f=="abs")   return fabs(x);
    if (f=="ceil")  return ceil(x);
    if (f=="floor") return floor(x);
    if (f=="cbrt")  return cbrt(x);
    err="Unknown fn:"+f; return 0;
}

static double evaluate(const std::string& rawExpr,std::string& err) {
    std::vector<Token> tokens=tokenize(rawExpr,err);
    if (!err.empty()) return 0;
    std::vector<Token> rpn;
    std::stack<Token>  ops;
    for (auto& tok:tokens) {
        if (tok.type==Token::NUM) { rpn.push_back(tok); }
        else if (tok.type==Token::FUNC) { ops.push(tok); }
        else if (tok.type==Token::OP) {
            while (!ops.empty()&&ops.top().type==Token::OP&&
                (prec(ops.top().op)>prec(tok.op)||
                (prec(ops.top().op)==prec(tok.op)&&tok.op!='^'))) {
                rpn.push_back(ops.top()); ops.pop();
                }
                ops.push(tok);
        } else if (tok.type==Token::LPAREN) { ops.push(tok); }
        else if (tok.type==Token::RPAREN) {
            while (!ops.empty()&&ops.top().type!=Token::LPAREN) { rpn.push_back(ops.top()); ops.pop(); }
            if (ops.empty()) { err="Bracket mismatch"; return 0; }
            ops.pop();
            if (!ops.empty()&&ops.top().type==Token::FUNC) { rpn.push_back(ops.top()); ops.pop(); }
        }
    }
    while (!ops.empty()) {
        if (ops.top().type==Token::LPAREN) { err="Bracket mismatch"; return 0; }
        rpn.push_back(ops.top()); ops.pop();
    }
    std::vector<double> st;
    for (auto& tok:rpn) {
        if (tok.type==Token::NUM) { st.push_back(tok.num); }
        else if (tok.type==Token::OP) {
            if (st.size()<2) { err="Syntax error"; return 0; }
            double b=st.back(); st.pop_back();
            double a=st.back(); st.pop_back();
            st.push_back(applyOp(a,tok.op,b,err));
            if (!err.empty()) return 0;
        } else if (tok.type==Token::FUNC) {
            if (st.empty()) { err="Missing arg"; return 0; }
            double a=st.back(); st.pop_back();
            st.push_back(applyFunc(tok.func,a,err));
            if (!err.empty()) return 0;
        }
    }
    if (st.size()!=1) { err="Syntax error"; return 0; }
    return st[0];
}

static std::string fmtNum(double v) {
    if (isnan(v)||isinf(v)) return "Error";
    char buf[32];
    if (v==floor(v)&&fabs(v)<1e15) snprintf(buf,sizeof(buf),"%.0f",v);
    else                            snprintf(buf,sizeof(buf),"%.8g",v);
    return buf;
}

static double computeFormula(int idx,double* v,std::string& err) {
    const double D=M_PI/180.0;
    switch(idx) {
        case 0: return sqrt(v[0]*v[0]+v[1]*v[1]);
        case 1: return v[1]*v[1]-4*v[0]*v[2];
        case 2: if(v[0]==0){err="a=0";return 0;} return -v[1]/v[0];
        case 3: if(v[0]==0){err="a=0";return 0;} return v[1]/v[0];
        case 4: return M_PI*v[0]*v[0];
        case 5: return 2*M_PI*v[0];
        case 6: return sqrt(v[0]*v[0]+v[1]*v[1]-2*v[0]*v[1]*cos(v[2]*D));
        case 7: { double sA=sin(v[1]*D); if(sA==0){err="sin(A)=0";return 0;} return v[0]*sin(v[2]*D)/sA; }
        case 8: return (4.0/3.0)*M_PI*v[0]*v[0]*v[0];
        case 9: return (v[0]+v[1])/2.0*v[2];
        case 10: if(v[0]<=0||v[1]<=0||v[1]==1){err="Invalid";return 0;} return log(v[0])/log(v[1]);
        case 11: if(v[1]==0){err="n=0";return 0;} return pow(v[0],1.0/v[1]);
    }
    err="Unknown formula"; return 0;
}

// --- Drawing Functions ---

static void drawCalc() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_CYAN);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(2,2);
    M5Cardputer.Display.print("fn+M=forms fn+F=frac fn+H=HELP");
    M5Cardputer.Display.drawLine(0,11,SCR_W,11,TFT_DARKGREY);

    M5Cardputer.Display.setTextColor(TFT_WHITE);
    M5Cardputer.Display.setTextSize(EXPR_SIZE);
    std::string disp=expression;
    if (disp.size()>19) disp="..."+disp.substr(disp.size()-16);
    M5Cardputer.Display.setCursor(2,EXPR_Y);
    M5Cardputer.Display.print(disp.c_str());

    M5Cardputer.Display.drawLine(0,RES_Y-2,SCR_W,RES_Y-2,TFT_DARKGREY);

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(hasError?TFT_RED:TFT_GREEN);
    std::string res=resultStr;
    if (res.size()>18) res=res.substr(0,18)+"..";
    M5Cardputer.Display.setCursor(2,RES_Y);
    M5Cardputer.Display.print(res.c_str());

    M5Cardputer.Display.setTextSize(1);
    int batPct = M5Cardputer.Power.getBatteryLevel();
    char botRight[32];
    snprintf(botRight,sizeof(botRight),"CalcPuter v" VERSION " %d%%",batPct);
    M5Cardputer.Display.setTextColor(TFT_DARKGREY);
    int brX = SCR_W - (int)strlen(botRight)*6 - 2;
    M5Cardputer.Display.setCursor(brX, STATUS_Y);
    M5Cardputer.Display.print(botRight);
}

static void drawFormulaMenu() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_YELLOW);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(2,2);
    M5Cardputer.Display.print("FORMULAS | fn+; / fn+. = Nav");
    M5Cardputer.Display.drawLine(0,11,SCR_W,11,TFT_DARKGREY);
    int start=max(0,formulaIdx-2);
    int end=min(FORMULA_COUNT,start+5);
    for (int i=start;i<end;i++) {
        int y=14+(i-start)*21;
        if (i==formulaIdx) {
            M5Cardputer.Display.fillRect(0,y-1,SCR_W,19,TFT_NAVY);
            M5Cardputer.Display.setTextColor(TFT_WHITE);
        } else {
            M5Cardputer.Display.setTextColor(TFT_LIGHTGREY);
        }
        M5Cardputer.Display.setCursor(4,y);
        M5Cardputer.Display.print(formulas[i].name);
        M5Cardputer.Display.setTextColor(TFT_DARKGREY);
        M5Cardputer.Display.setCursor(4,y+10);
        M5Cardputer.Display.print(formulas[i].desc);
    }
}

static void drawFraction() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_YELLOW);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(2,2);
    M5Cardputer.Display.print("FRACTION INPUT | Enter = Next");
    M5Cardputer.Display.drawLine(0,11,SCR_W,11,TFT_DARKGREY);

    std::string top = fracNum + (fracStep==0?"_":"");
    std::string bot = fracDen + (fracStep==1?"_":"");
    if(top.size() > 15) top = "..." + top.substr(top.size()-12);
    if(bot.size() > 15) bot = "..." + bot.substr(bot.size()-12);

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(fracStep==0?TFT_WHITE:TFT_LIGHTGREY);
    M5Cardputer.Display.setCursor(10,35);
    M5Cardputer.Display.print(top.c_str());
    M5Cardputer.Display.drawLine(10,62,230,62,TFT_WHITE);
    M5Cardputer.Display.setTextColor(fracStep==1?TFT_WHITE:TFT_LIGHTGREY);
    M5Cardputer.Display.setCursor(10,75);
    M5Cardputer.Display.print(bot.c_str());
}

static void drawFormulaVars() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_YELLOW);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(2,2);
    M5Cardputer.Display.print(formulas[formulaIdx].name);
    M5Cardputer.Display.drawLine(0,22,SCR_W,22,TFT_DARKGREY);
    for (int i=0;i<fvStep&&i<formulas[formulaIdx].varCount;i++) {
        M5Cardputer.Display.setTextColor(TFT_GREEN);
        M5Cardputer.Display.setCursor(2,26+i*14);
        char buf[40];
        snprintf(buf,sizeof(buf),"%s = %s",formulas[formulaIdx].vars[i],fmtNum(fvValues[i]).c_str());
        M5Cardputer.Display.print(buf);
    }
    if (fvStep<formulas[formulaIdx].varCount) {
        M5Cardputer.Display.setTextColor(TFT_WHITE);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(2,88);
        std::string prompt=std::string(formulas[formulaIdx].vars[fvStep])+" = "+fvInput+"_";
        M5Cardputer.Display.print(prompt.c_str());
    }
}

static void drawHelp() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_CYAN);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(2,2);
    M5Cardputer.Display.print("SHORTCUTS HELP | press any key");
    M5Cardputer.Display.drawLine(0,11,SCR_W,11,TFT_DARKGREY);
    M5Cardputer.Display.setTextColor(TFT_WHITE);
    int y=15;
    M5Cardputer.Display.setCursor(2,y);   M5Cardputer.Display.print("fn+M: Formulas  fn+F: Fraction");
    M5Cardputer.Display.setCursor(2,y+12);M5Cardputer.Display.print("fn+R: sqrt()    fn+P: PI");
    M5Cardputer.Display.setCursor(2,y+24);M5Cardputer.Display.print("fn+S: sin()     fn+O: cos()");
    M5Cardputer.Display.setCursor(2,y+36);M5Cardputer.Display.print("fn+T: tan()     fn+L: ln()");
    M5Cardputer.Display.setCursor(2,y+48);M5Cardputer.Display.print("fn+^: power     fn+C: CLEAR");
    M5Cardputer.Display.setCursor(2,y+60);M5Cardputer.Display.print("fn+H: Help      fn+;: Up / fn+.: Down");
    M5Cardputer.Display.setTextColor(TFT_ORANGE);
    M5Cardputer.Display.setCursor(2,y+80);M5Cardputer.Display.print("Works inside fractions too!");
}

// --- Input Handlers ---

static void handleCalcKey(Keyboard_Class::KeysState& s) {
    if (s.fn) {
        for (char c:s.word) {
            switch(c) {
                case 'c': case 'C': expression=""; resultStr=""; hasError=false; break;
                case 'f': case 'F': fracStep=0; fracNum=""; fracDen=""; appState=STATE_FRACTION_INPUT; break;
                case 'r': case 'R': expression+="sqrt("; break;
                case 'p': case 'P': expression+="pi"; break;
                case 'm': case 'M': formulaIdx=0; appState=STATE_FORMULA_MENU; break;
                case 'l': case 'L': expression+="ln("; break;
                case 's': case 'S': expression+="sin("; break;
                case 'o': case 'O': expression+="cos("; break;
                case 't': case 'T': expression+="tan("; break;
                case 'h': case 'H': appState=STATE_HELP; break;
                case '^':           expression+="^"; break;
            }
        }
        return;
    }
    if (s.enter) {
        if (expression.empty()) return;
        std::string err;
        double result=evaluate(expression,err);
        if (!err.empty()) { resultStr=err; hasError=true; }
        else              { resultStr="= "+fmtNum(result); hasError=false; }
        return;
    }
    if (s.del) {
        if (!expression.empty()) expression.pop_back();
        resultStr=""; hasError=false;
        return;
    }
    for (char c:s.word) if (c>=32&&c<127) { expression+=c; resultStr=""; hasError=false; }
}

static void handleFractionKey(Keyboard_Class::KeysState& s) {
    std::string& cur = (fracStep == 0) ? fracNum : fracDen;
    if (s.fn) {
        for (char c:s.word) {
            switch(c) {
                case 'r': case 'R': cur+="sqrt("; break;
                case 'p': case 'P': cur+="pi"; break;
                case 'l': case 'L': cur+="ln("; break;
                case 's': case 'S': cur+="sin("; break;
                case 'o': case 'O': cur+="cos("; break;
                case 't': case 'T': cur+="tan("; break;
                case '^':           cur+="^"; break;
                case 'c': case 'C': fracNum=""; fracDen=""; fracStep=0; break;
            }
        }
        return;
    }
    if (s.del) {
        if (!cur.empty()) { cur.pop_back(); return; }
        if (fracStep==1) { fracStep=0; return; }
        appState=STATE_CALC; return;
    }
    if (s.enter) {
        if (fracStep==0 && !fracNum.empty()) { fracStep=1; return; }
        if (fracStep==1 && !fracDen.empty()) {
            expression += "(" + fracNum + ")/(" + fracDen + ")";
            resultStr=""; hasError=false;
            appState=STATE_CALC;
        }
        return;
    }
    for (char c:s.word) if (c>=32&&c<127) cur+=c;
}

static void handleFormulaMenuKey(Keyboard_Class::KeysState& s) {
    if (s.del) { appState=STATE_CALC; return; }
    if (s.fn) {
        for (char c:s.word) {
            if (c==';') formulaIdx=max(0,formulaIdx-1);
            if (c=='.') formulaIdx=min(FORMULA_COUNT-1,formulaIdx+1);
        }
        return;
    }
    if (s.enter) {
        fvStep=0; fvInput="";
        memset(fvValues,0,sizeof(fvValues));
        appState=STATE_FORMULA_VARS;
    }
}

static void handleFormulaVarsKey(Keyboard_Class::KeysState& s) {
    if (s.fn) { for (char c:s.word) if (c=='c'||c=='C') { appState=STATE_CALC; return; } }
    if (s.del) { if (!fvInput.empty()) fvInput.pop_back(); return; }
    if (s.enter) {
        if (fvInput.empty()) return;
        fvValues[fvStep]=atof(fvInput.c_str());
        fvInput=""; fvStep++;
        if (fvStep>=formulas[formulaIdx].varCount) {
            std::string err;
            double res=computeFormula(formulaIdx,fvValues,err);
            if (!err.empty()) { resultStr=err; hasError=true; }
            else              { resultStr="= "+fmtNum(res); hasError=false; expression=fmtNum(res); }
            appState=STATE_CALC;
        }
        return;
    }
    for (char c:s.word) {
        bool okDot=(c=='.'&&fvInput.find('.')==std::string::npos);
        bool okNeg=(c=='-'&&fvInput.empty());
        if ((isdigit(c)||okDot||okNeg)&&fvInput.size()<12) fvInput+=c;
    }
}

void setup() {
    auto cfg=M5.config();
    M5Cardputer.begin(cfg,true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextDatum(TL_DATUM);
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_CYAN);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(20,40);
    M5Cardputer.Display.print("CalcPuter v" VERSION);
    delay(1000);
    drawCalc();
}

void loop() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    Keyboard_Class::KeysState s=M5Cardputer.Keyboard.keysState();

    switch (appState) {
        case STATE_CALC:           handleCalcKey(s); drawCalc(); break;
        case STATE_FORMULA_MENU:   handleFormulaMenuKey(s);
        if (appState==STATE_FORMULA_VARS) drawFormulaVars();
        else drawFormulaMenu(); break;
        case STATE_FRACTION_INPUT: handleFractionKey(s);
        if (appState==STATE_FRACTION_INPUT) drawFraction();
        else drawCalc(); break;
        case STATE_FORMULA_VARS:   handleFormulaVarsKey(s);
        if (appState==STATE_FORMULA_VARS) drawFormulaVars();
        else drawCalc(); break;
        case STATE_HELP:           if(s.word.size()>0 || s.enter || s.del || s.fn) appState=STATE_CALC;
        drawHelp(); break;
    }
    delay(5);
}
