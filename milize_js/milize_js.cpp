// milize_js.cpp — MilizeBeatmap JS 谱面渲染引擎实现
// 内嵌 quickjs 执行官方 MilizeBeatmap JS 谱面 → easy-phi DevJson
// 架构与 libMilize.so 同构：注册 MilizeBeatmap 全局对象，t()/n()/a()/line()/s()/p() 回调收集数据
#include "milize_js.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

extern "C" {
#include "quickjs.h"
}

namespace milize_js {

// ============ Builder 实现 ============
void Builder::setMeta(const std::string& key, const std::string& val) {
    if (key == "Title") meta["Title"] = val;
    else if (key == "Composer") meta["Composer"] = val;
    else if (key == "Illustrator" || key == "IllustrationFile") meta["Illustrator"] = val;
    else if (key == "Beatmapper") meta["Beatmapper"] = val;
    else if (key == "Difficulty") meta["Difficulty"] = val;
    else if (key == "DifficultyValue") { meta["DifficultyValue"] = val; hasDifficultyValue = true; }
    else meta[key] = val;  // 其余原样保留（easy-phi 忽略未知键）
}

namespace {
std::string esc(const std::string& in) {
    std::string o;
    for (char c : in) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}
std::string num(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}
}  // namespace

std::string Builder::toDevJson() const {
    std::string s;
    s += "{\"meta\":{";
    auto kv = [&](const std::string& k, const std::string& v) {
        s += "\"" + k + "\":\"" + esc(v) + "\",";
    };
    auto get = [&](const std::string& k) -> const std::string& {
        static const std::string empty;
        auto it = meta.find(k);
        return it == meta.end() ? empty : it->second;
    };
    kv("Title", get("Title"));
    kv("Composer", get("Composer"));
    kv("Illustrator", get("Illustrator"));
    kv("Beatmapper", get("Beatmapper"));
    kv("Difficulty", get("Difficulty"));
    kv("DifficultyValue", hasDifficultyValue ? get("DifficultyValue") : "0");
    if (s.back() == ',') s.pop_back();
    s += "},\"bpms\":[";
    for (auto& b : bpms) { s += "{\"start\":" + num(b.start) + ",\"bpm\":" + num(b.bpm) + "},"; }
    if (!bpms.empty()) s.pop_back();
    s += "],\"lines\":[";
    for (auto& ln : lines) {
        s += "{\"notes\":[";
        for (auto& n : ln) {
            s += "{\"bpm\":" + std::to_string(n.bpm) +
                 ",\"startTime\":" + num(n.startTime) +
                 ",\"endTime\":" + num(n.endTime) +
                 ",\"type\":" + std::to_string(n.type) +
                 ",\"isFake\":" + (n.isFake ? "true" : "false") +
                 ",\"isAlwaysPerfect\":" + (n.isAlwaysPerfect ? "true" : "false") +
                 ",\"index\":" + std::to_string(n.index) + "},";
        }
        if (!ln.empty()) s.pop_back();
        s += "]},";
    }
    if (!lines.empty()) s.pop_back();
    s += "],\"storyboardObjects\":[";
    for (auto& o : storyboard) {
        s += "{\"type\":" + std::to_string(o.type) +
             ",\"data\":\"" + esc(o.data) +
             "\",\"layer\":" + std::to_string(o.layer) + "},";
    }
    if (!storyboard.empty()) s.pop_back();
    s += "],\"animations\":[";
    for (auto& a : anims) {
        s += "{\"bpmId\":" + std::to_string(a.bpmId) +
             ",\"fromBeat\":" + num(a.fromBeat) +
             ",\"toBeat\":" + num(a.toBeat) +
             ",\"key\":" + std::to_string(a.key) +
             ",\"fv\":" + num(a.fv) +
             ",\"tv\":" + num(a.tv) +
             ",\"data\":" + std::to_string(a.data) +
             ",\"i1\":" + std::to_string(a.i1) +
             ",\"ease\":" + std::to_string(a.ease) +
             ",\"press\":" + std::to_string(a.press) + "},";
    }
    if (!anims.empty()) s.pop_back();
    s += "]}";
    return s;
}

// ============ quickjs 回调（无捕获普通函数，符合 JS_CFunc） ============
static Builder g_builder;

static double argToNum(JSContext* ctx, JSValueConst v, bool has) {
    if (!has) return 0;
    if (JS_IsString(v)) {
        const char* s = JS_ToCString(ctx, v);
        double d = s ? atof(s) : 0;
        if (s) JS_FreeCString(ctx, s);
        return d;
    }
    double d = 0;
    JS_ToFloat64(ctx, &d, v);
    return d;
}

static void js_property(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return;
    const char* k = JS_ToCString(ctx, argv[0]);
    const char* v = JS_ToCString(ctx, argv[1]);
    if (k && v) g_builder.setMeta(k, v);
    if (k) JS_FreeCString(ctx, k);
    if (v) JS_FreeCString(ctx, v);
}

// ============ 时间参数解包(数字=秒 / 数组=拍时间戳) ============
// 本家结构(libMilize.so 反汇编 + dump.cs 交叉验证):
//   MilizeBeatmapTimestampWrapper* { +0x04,+0x08,+0x0C = int[3]; +0x18 = double absTime; +0x20 = kind }
//   kind==0 -> "timestamp"(IntPtr 走 BeatmapTimestampWrapperUnwrapTimestamp, 得 int[3])
//   kind==1 -> "absolute time"(UnwrapAbsoluteTime, 得 double 秒)
//   C# BeatmapModel.NoteData { int[] From; int[] To; float FromTime; float ToTime; }
//   => JS 传数组 = 拍时间戳 [整拍, 分子, 分母], 值 = 整拍 + 分子/分母 (拍);

// 拍 -> 秒: BeatmapModel.BPMData { float Start(秒); float BPM; int BeatsPerBar; }
// 拍 0 锚定在 bpms[0].Start; 段内线性, 段间按 BeatsPerBar 换算(此处只用 Start/BPM 即可推进)
static double beatToSeconds(double beat) {
    const auto& B = g_builder.bpms;
    if (B.empty()) return beat;                       // 无 BPM 表: 退回原值(视为秒)
    auto bpmOf = [](double v) { return v > 1e-6 ? v : 120.0; };
    double segBeat = 0.0;                             // 当前段起始拍(相对拍 0)
    for (size_t i = 0; i < B.size(); i++) {
        double nextSegBeat = 1e300;
        if (i + 1 < B.size())
            nextSegBeat = segBeat + (B[i + 1].start - B[i].start) * bpmOf(B[i].bpm) / 60.0;
        if (beat < nextSegBeat) return B[i].start + (beat - segBeat) * 60.0 / bpmOf(B[i].bpm);
        segBeat = nextSegBeat;
    }
    const auto& last = B.back();
    return last.start + (beat - segBeat) * 60.0 / bpmOf(last.bpm);
}

// 时间参数: JS 数字 = 绝对秒; JS 数组 = 拍时间戳 -> 经 BPM 表换算成秒
static double timeArg(JSContext* ctx, JSValueConst v) {
    if (JS_IsArray(ctx, v)) {
        JSValue e = JS_GetPropertyUint32(ctx, v, 0);
        JSValue n = JS_GetPropertyUint32(ctx, v, 1);
        JSValue d = JS_GetPropertyUint32(ctx, v, 2);
        double ve = 0, vn = 0, vd = 0;
        JS_ToFloat64(ctx, &ve, e);
        JS_ToFloat64(ctx, &vn, n);
        JS_ToFloat64(ctx, &vd, d);
        JS_FreeValue(ctx, e); JS_FreeValue(ctx, n); JS_FreeValue(ctx, d);
        double den = (vd != 0.0 && vd == vd) ? vd : 1.0;   // 分母 0/NaN -> 1
        double beat = ve + vn / den;
        if (beat != beat) return 0.0;                       // NaN 防御
        return beatToSeconds(beat);
    }
    double t = 0;
    JS_ToFloat64(ctx, &t, v);
    return t;
}

static JSValue js_timing(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    double sec = 0, bpm = 120;
    if (argc > 0) JS_ToFloat64(ctx, &sec, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &bpm, argv[1]);
    g_builder.bpms.push_back({ sec, bpm });
    // 本家 BeatmapAddBpm(ctx,start,bpm,beatsPerBar) -> ulong id; 谱面 var tr=x.timing(...) 把该 id
    // 当 bpmId 传回 animation()/note({Qr(x),this.qr,...}) —— 必须返回索引而非 undefined
    return JS_NewInt64(ctx, (int64_t)(g_builder.bpms.size() - 1));
}

static JSValue js_note(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    // 参数顺序: (line, bpm, fromTime, toTime, type, fake, ap)
    double line = 0, bpm = 0, from = 0, to = 0, type = 0;
    if (argc > 0) JS_ToFloat64(ctx, &line, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &bpm, argv[1]);
    if (argc > 2) from = timeArg(ctx, argv[2]);   // 数字=秒 / 数组=拍时间戳
    if (argc > 3) to   = timeArg(ctx, argv[3]);
    if (argc > 4) JS_ToFloat64(ctx, &type, argv[4]);
    // 防爆：非法超大 line 索引（谱面数据异常）→ 丢弃该音符，避免 lines 疯狂扩容 OOM
    if (line < 0 || (uint64_t)line >= 100000) return JS_UNDEFINED;
    Builder::Note n;
    n.bpm = (uint64_t)bpm; n.startTime = from; n.endTime = to; n.type = (uint64_t)type;
    n.index = g_builder.noteGlobalIndex++;
    if (argc > 5) n.isFake = JS_ToBool(ctx, argv[5]) == 1;
    if (argc > 6) n.isAlwaysPerfect = JS_ToBool(ctx, argv[6]) == 1;
    g_builder.ensureLine((uint64_t)line);
    g_builder.lines[(uint64_t)line].push_back(n);
    return JS_UNDEFINED;
}

static JSValue js_animation(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    // 参数顺序: (bpmId, fromTime, toTime, key, fv, tv, data, i1, press, ease, valueExpression, customEase)
    Builder::Anim a;
    double v[10] = { 0,0,0,0,0,0,0,0,0,0 };
    for (int i = 0; i < argc && i < 10; i++) JS_ToFloat64(ctx, &v[i], argv[i]);
    // 防爆：NaN/负数/超大 float 转 uint64 会溢出成 2^63（如 Algebra 谱面全部 anims i1=2^63）→ 归 0
    auto san = [](double d) -> uint64_t { return (d >= 0.0 && d < 100000.0) ? (uint64_t)d : 0; };
    a.bpmId = san(v[0]);
    // 本家 BeatmapAddAnimation 的 fromBeat/toBeat 同为 IntPtr 时间戳 -> 与 note 同一套解包
    a.fromBeat = (argc > 1) ? timeArg(ctx, argv[1]) : 0.0;
    a.toBeat   = (argc > 2) ? timeArg(ctx, argv[2]) : 0.0;
    a.key = san(v[3]);
    a.data = san(v[6]); a.i1 = san(v[7]);
    a.press = san(v[8]); a.ease = san(v[9]);
    a.fv = argToNum(ctx, argv[4], argc > 4);
    a.tv = argToNum(ctx, argv[5], argc > 5);
    // Color 动画（key=22）：保留原始字符串（数字/#hex/oklch），供引擎侧转 Color
    if (a.key == 22) {
        if (argc > 4 && JS_IsString(argv[4])) { const char* s = JS_ToCString(ctx, argv[4]); a.fvStr = s ? s : ""; if (s) JS_FreeCString(ctx, s); }
        if (argc > 5 && JS_IsString(argv[5])) { const char* s = JS_ToCString(ctx, argv[5]); a.tvStr = s ? s : ""; if (s) JS_FreeCString(ctx, s); }
    }
    if (a.data == 0 && a.i1 < 100000) g_builder.ensureLine(a.i1);  // 线动画 → 保证线存在（防爆）
    g_builder.anims.push_back(a);
    return JS_UNDEFINED;
}

static JSValue js_line(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    g_builder.ensureLine(g_builder.lines.size());  // 追加一条空线
    // 本家 BeatmapAddLine(ctx) -> ulong 线 id; 谱面(line 对象池)Aa.ut(17) 取出的 id 会作为
    // animation 的 i1 参数回传, 若返回 undefined 则池内元素全为 undefined → ut() 死循环
    return JS_NewInt64(ctx, (int64_t)(g_builder.lines.size() - 1));
}

static JSValue js_storyboard(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Builder::SB sb;
    double type = 0, layer = 0;
    if (argc > 0) JS_ToFloat64(ctx, &type, argv[0]);
    if (argc > 1) {
        const char* d = JS_ToCString(ctx, argv[1]);
        sb.data = d ? d : "";
        if (d) JS_FreeCString(ctx, d);
    }
    if (argc > 2) JS_ToFloat64(ctx, &layer, argv[2]);
    sb.type = (uint64_t)type; sb.layer = (uint64_t)layer;
    g_builder.storyboard.push_back(sb);
    return JS_UNDEFINED;
}

// p/withProperty 都返回带 .p 的新对象，支持无限链式
static JSValue js_p(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    js_property(ctx, this_val, argc, argv);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "p", JS_NewCFunction(ctx, js_p, "p", 2));
    return obj;
}

static JSValue js_with_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    js_property(ctx, this_val, argc, argv);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "p", JS_NewCFunction(ctx, js_p, "p", 2));
    return obj;
}

// withoutProperty: 从 meta 删除键（与 withProperty 配对）
static JSValue js_without_property(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    const char* k = JS_ToCString(ctx, argv[0]);
    if (k) { g_builder.meta.erase(k); JS_FreeCString(ctx, k); }
    return JS_UNDEFINED;
}

// env: 谱面初始化上下文（noop；保留接口兼容）
static JSValue js_env(JSContext*, JSValueConst, int, JSValueConst*) {
    return JS_UNDEFINED;
}

// ============ 执行入口 ============
// 中断处理器：防止谱面 JS 死循环导致内存无界增长（OOM）
static int js_interrupt_handler(JSRuntime*, void* opaque) {
    auto* start = (std::chrono::steady_clock::time_point*)opaque;
    if (std::chrono::steady_clock::now() - *start > std::chrono::seconds(30)) return 1;
    return 0;
}

static Builder ExecuteToBuilderImpl(const std::string& js, std::string* err);

Builder ExecuteToBuilder(const std::string& js, std::string* err) {
    try {
        return ExecuteToBuilderImpl(js, err);
    } catch (const std::exception& e) {
        if (err) {
            size_t notes = 0;
            for (auto& ln : g_builder.lines) notes += ln.size();
            *err = std::string("C++ exception: ") + e.what() +
                   " [builder: bpms=" + std::to_string(g_builder.bpms.size()) +
                   " lines=" + std::to_string(g_builder.lines.size()) +
                   " notes=" + std::to_string(notes) +
                   " sb=" + std::to_string(g_builder.storyboard.size()) +
                   " anims=" + std::to_string(g_builder.anims.size()) + "]";
        }
        g_builder = Builder{};
        return g_builder;
    } catch (...) {
        if (err) *err = "C++ unknown exception";
        g_builder = Builder{};
        return g_builder;
    }
}

static Builder ExecuteToBuilderImpl(const std::string& js, std::string* err) {
    g_builder = Builder{};
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) { if (err) *err = "JS_NewRuntime failed"; return g_builder; }
    auto start = std::chrono::steady_clock::now();
    JS_SetInterruptHandler(rt, js_interrupt_handler, &start);
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); if (err) *err = "JS_NewContext failed"; return g_builder; }

    // 注册全局对象 MilizeBeatmap
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue m = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, m, "timing", JS_NewCFunction(ctx, js_timing, "timing", 3));
    JS_SetPropertyStr(ctx, m, "note", JS_NewCFunction(ctx, js_note, "note", 7));
    JS_SetPropertyStr(ctx, m, "animation", JS_NewCFunction(ctx, js_animation, "animation", 12));
    JS_SetPropertyStr(ctx, m, "line", JS_NewCFunction(ctx, js_line, "line", 0));
    JS_SetPropertyStr(ctx, m, "storyboardObject", JS_NewCFunction(ctx, js_storyboard, "storyboardObject", 3));
    JS_SetPropertyStr(ctx, m, "withProperty", JS_NewCFunction(ctx, js_with_property, "withProperty", 2));
    JS_SetPropertyStr(ctx, m, "withoutProperty", JS_NewCFunction(ctx, js_without_property, "withoutProperty", 1));
    JS_SetPropertyStr(ctx, m, "p", JS_NewCFunction(ctx, js_p, "p", 2));
    JS_SetPropertyStr(ctx, m, "env", JS_NewCFunction(ctx, js_env, "env", 1));
    JS_SetPropertyStr(ctx, g, "MilizeBeatmap", m);
    JS_FreeValue(ctx, g);

    JSValue r = JS_Eval(ctx, js.c_str(), js.size(), "<chart>", 0);
    if (JS_IsException(r)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        if (err) *err = msg ? msg : "unknown JS error";
        if (msg) JS_FreeCString(ctx, msg);
        // 附带堆栈定位
        JSValue st = JS_GetPropertyStr(ctx, ex, "stack");
        if (!JS_IsUndefined(st)) {
            const char* sts = JS_ToCString(ctx, st);
            if (sts) { if (err) *err += std::string("\n") + sts; JS_FreeCString(ctx, sts); }
        }
        JS_FreeValue(ctx, st);
        JS_FreeValue(ctx, ex);
        g_builder = Builder{};  // 失败清空
    }
    JS_FreeValue(ctx, r);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return g_builder;
}

std::string ExecuteToDevJson(const std::string& js, std::string* err) {
    Builder b = ExecuteToBuilder(js, err);
    if (b.bpms.empty() && b.lines.empty() && b.anims.empty() && err && !err->empty()) return "";
    return b.toDevJson();
}

}  // namespace milize_js
