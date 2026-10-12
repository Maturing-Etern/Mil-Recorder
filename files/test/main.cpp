// [CLI zip] miniz (public domain) 用于谱面 zip 包解压(external/miniz/)
#include "miniz.h"
namespace test_main {
    // 内嵌 Milize JS 引擎（milize_js.hpp 由 build.bat 以 -include 注入）：官方 JS 谱面 → Builder → MilChart

    using gframerate_meter::FramerateMeter;
    using gregapi::RegAPI;
    using gdata::Data;
    using gsp::gsp;
    using namespace gnumeric::types;
    using namespace gopengl::GL;
    using namespace geasy_phi;

    // [dbg] 最小 BMP 写出（BGR24，无压缩）；仅供 --screenshot 调试
    namespace BMPWriter {
        void writeBMP(const std::string& path, int w, int h, const uint8_t* rgba) {
            FILE* fp = fopen(path.c_str(), "wb");
            if (!fp) { std::fprintf(stderr, "[bmp] open fail: %s\n", path.c_str()); return; }
            int rowStride = w * 3;
            int padBytes = (4 - (rowStride % 4)) % 4;
            int dataSize = (rowStride + padBytes) * h;
            uint32_t fileSize = 54 + dataSize;

            uint8_t hdr[54] = {};
            hdr[0] = 'B'; hdr[1] = 'M';
            std::memcpy(hdr + 2, &fileSize, 4);
            uint32_t offBits = 54;
            std::memcpy(hdr + 10, &offBits, 4);
            uint32_t dibSize = 40;
            std::memcpy(hdr + 14, &dibSize, 4);
            int32_t iw = w, ih = -h;  // flip Y
            std::memcpy(hdr + 18, &iw, 4);
            std::memcpy(hdr + 22, &ih, 4);
            uint16_t planes = 1, bpp = 24;
            std::memcpy(hdr + 26, &planes, 2);
            std::memcpy(hdr + 28, &bpp, 2);
            std::memcpy(hdr + 34, &dataSize, 4);

            fwrite(hdr, 1, 54, fp);
            std::vector<uint8_t> rowBuf(rowStride + padBytes);
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int idx = (y * w + x) * 4;
                    rowBuf[x*3]     = rgba[idx + 2]; // B
                    rowBuf[x*3 + 1] = rgba[idx + 1]; // G
                    rowBuf[x*3 + 2] = rgba[idx + 0]; // R
                }
                fwrite(rowBuf.data(), 1, rowStride + padBytes, fp);
            }
            fclose(fp);
            std::fprintf(stderr, "[bmp] wrote %s (%dx%d, %u bytes)\n", path.c_str(), w, h, fileSize);
        }
    }
    using namespace gwin32ui;
    using namespace gtext_renderer;

    TextManager::Renderer createTextRendererFromData(const Data& data) {
        auto tr = TextRenderer::Make();
        tr->loadFont(data);
        return [tr](const std::string& text, uint64 fontSize) {
            return tr->render(text, fontSize);
        };
    }

    struct WindowBase {
        gsp<gglfw3::Window> window;
        float64 frameBusyWaitPercentage = 0.8;
        bool fullscreen, isPortrait;
        std::pair<uint64, uint64> surfaceSize;
        std::string chartDir;

        gsp<GL33Context> glCtx;
        FramerateMeter framerateMeter;

        TakeOvererComponents::AudioManager* audioManagerRef;

        void busyWait(float64 frameSt, bool printInfo) {
            if (!window->getSwapInterval()) return;

            float64 waitSt = gtime::steady();
            auto refreshRate = gglfw3::Monitor::MakePrimary().refreshRate();

            volatile uint8* dummy = nullptr;
            while ((gtime::steady() - frameSt) < frameBusyWaitPercentage / refreshRate) {
                dummy++;
            }

            if (printInfo) {
                std::cout << "wait took " << ((gtime::steady() - waitSt) * 1000) << " ms" << '\n';
            }
        }

        struct MainloopConfigBase {
            std::optional<float64> time;
            bool isRenderingVideo;
        };

        template <typename TakeOverer>
        bool mainloopFrame(
            const MainloopConfigBase& config,
            const TakeOverer& renderer,
            std::function<typename TakeOverer::element_type::RenderConfig(const typename TakeOverer::element_type::RenderConfig&)> renderConfigurer = [](auto c) { return c; },
            std::function<void(typename TakeOverer::element_type::RenderResultInfo&)> callback = [](auto i) {}
        ) {
            auto frameSt = gtime::steady();

            if (window->shouldClose()) return false;

            auto windowSize = window->getSize();

            if (!config.isRenderingVideo) {
                surfaceSize.first = windowSize.first;
                surfaceSize.second = windowSize.second;
            }

            renderer->calcConfig.screenSize = { (float64)surfaceSize.first, (float64)surfaceSize.second };
            // [dbg] 白块排查:每帧首 + 改动时打印 size,确认 surfaceSize 是窗口真实尺寸 (否则 viewport 会偏)
            static int64 lastDbgSizeW = -1, lastDbgSizeH = -1;
            if (surfaceSize.first != lastDbgSizeW || surfaceSize.second != lastDbgSizeH) {
                lastDbgSizeW = surfaceSize.first;
                lastDbgSizeH = surfaceSize.second;
                std::fprintf(stderr, "[dbg viewport] surfaceSize=(%d,%d)\n", surfaceSize.first, surfaceSize.second);
            }
            // [fix] 强制按 surfaceSize 重置 viewport,补救 GL state 错乱或 scale 缩放不一致
            renderer->glCtx->setViewport(surfaceSize.first, surfaceSize.second);
            
            auto& resultInfo = renderer->render(renderConfigurer({
                .base = {
                    .time = config.time,
                    .disableHitsound = config.isRenderingVideo
                }
            }));

            callback(resultInfo);

            if (!config.isRenderingVideo) {
                std::cout << "calculate took: " << (resultInfo.base.calculatedTook * 1000) << " ms" << '\n';
                std::cout << "gl operations took: " << (resultInfo.base.glOperationsTook * 1000) << " ms" << '\n';

                gglfw3::pollEvents();
                busyWait(frameSt, !config.isRenderingVideo);
                window->swapBuffers();
            }

            if (!config.isRenderingVideo) {
                std::cout << "frame took " << ((gtime::steady() - frameSt) * 1000) << " ms" << '\n';
                std::cout << "draw calls count: " << glCtx->drawCallsCount << '\n';
                std::cout << "framerate: " << framerateMeter.get() << '\n';
                std::cout << std::string(80, '-') << '\n';
                std::cout << std::flush;
            }

            glCtx->frameEnded();
            framerateMeter.frame();

            return true;
        }
    };

    // hidden=true: 在 create() 之前就把窗口标记为不可见(GLFW_VISIBLE=false),
    // 避免 CLI(--screenshot/--record/--dumpmil/zip 播放)逐个弹窗。
    void createGLfwWindow(WindowBase& wbase, bool hidden = false) {
        wbase.window = gglfw3::Window::Make();
        std::fprintf(stderr, "[probe-eg] AFTER Make\n");
        if (hidden) wbase.window->setHidden(true);
        wbase.window->hint330Core()->hintMsaa(4);
        std::fprintf(stderr, "[probe-eg] BEFORE setSizeOfMonitor\n");
        if (wbase.fullscreen) wbase.window->setFullscreen();
        else {
            wbase.window->setSizeOfMonitor(0.6);

            if (wbase.isPortrait) {
                auto height = wbase.window->getSize().second;
                wbase.window->setSize(height * 9 / 16, height);
            }
        }

        std::fprintf(stderr, "[probe-eg] BEFORE create\n");
        wbase.window->create();
        std::fprintf(stderr, "[probe-eg] AFTER create\n");
        std::fprintf(stderr, "[probe-eg] BEFORE GL33Make\n");
        wbase.glCtx = GL33Context::Make(MakeGL33CoreInterface(gglfw3::getProcAddress));
        std::fprintf(stderr, "[probe-eg] AFTER GL33Make\n");
        
        wbase.window->mouseButtonCallback = [wbase = &wbase](int button, int action, int mods) {
            if (action == GLFW_PRESS) {
                if (button == GLFW_MOUSE_BUTTON_RIGHT) {
                    auto& audioManager = *wbase->audioManagerRef;
                    float64 mouseX = wbase->window->getMousePos().first;
                    auto progress = mouseX / wbase->window->getSize().first;
                    audioManager.seekBgm(audioManager.getBgmLength() * progress);
                }
            }
        };

            std::fprintf(stderr, "[probe-eg] FUNC-TAIL\n");
    }

    struct PhiWindow {
        WindowBase base;

        gsp<PhiTakeOverer> renderer;

        void init() {
            createGLfwWindow(base);

            renderer = PhiTakeOverer::Make();
            renderer->noteTextureDataLoader = PhiStaticResourceHelpers::noteTextureDataLoader;
            renderer->hitEffectDataLoader = PhiStaticResourceHelpers::hitEffectDataLoader;
            renderer->hitsoundDataLoader = PhiStaticResourceHelpers::hitsoundDataLoader;
            renderer->shaderDataLoader = PhiStaticResourceHelpers::createShaderDataLoaderFromChartDir([this] { return base.chartDir; });
            renderer->storyboardDataLoader = PhiStaticResourceHelpers::createStoryboardDataLoaderFromChartDir([this] { return base.chartDir; });

            renderer->glCtx = base.glCtx;
            renderer->sharedComp.textureDecoder = gimage::decode;
            renderer->textManager.renderer = createTextRendererFromData(PhiStaticResourceHelpers::getFontData());
            renderer->audioManager.decoder = gminiaudio::decode;
            renderer->audioManager.engine = gminiaudio::makeAudioEngine();
            renderer->init();

            base.audioManagerRef = &renderer->audioManager;
        }

        auto loadChart(const std::string& path, const std::string& chartDir) {
            base.chartDir = chartDir;

            auto resultInfo = renderer->loadChart({
                .data = Data::MakeFromFile(path),
                .extraData = Data::MakeFromFileOptional(PhiStoryboardHelpers::nameToPath(base.chartDir, "extra.json"))
            });

            resultInfo.checkAndThrow();
            return resultInfo;
        }

        struct MainloopConfig {
            WindowBase::MainloopConfigBase base;
        };

        bool mainloopFrame(const MainloopConfig& config) {
            return base.mainloopFrame<decltype(renderer)>(config.base, renderer);
        }
    };

    // ============ Milize JS → MilChart 直接构造（免 JSON/文件往返） ============
    // oklch(L C H) → sRGB 0-255（CSS Color 4 标准公式）
    static void oklchToRgb(const std::string& s, double& r, double& g, double& b) {
        r = g = b = 0;
        size_t lp = s.find('('), rp = s.find(')');
        if (lp == std::string::npos || rp == std::string::npos) return;
        std::string body = s.substr(lp + 1, rp - lp - 1);
        std::string parts[3]; int pi = 0;
        std::string cur;
        for (char c : body) {
            if (c == ' ' || c == ',' || c == '/') {
                if (!cur.empty() && pi < 3) { parts[pi++] = cur; cur.clear(); }
            } else cur += c;
        }
        if (!cur.empty() && pi < 3) parts[pi++] = cur;
        if (pi < 3) return;
        double L = atof(parts[0].c_str()), C = atof(parts[1].c_str()), H = atof(parts[2].c_str());
        const double PI = 3.14159265358979323846;
        double a_ = C * std::cos(H * PI / 180.0), b_ = C * std::sin(H * PI / 180.0);
        double l_ = L + 0.3963377774 * a_ + 0.2158037573 * b_;
        double m_ = L - 0.1055613458 * a_ - 0.0638541728 * b_;
        double s_ = L - 0.0894841775 * a_ - 1.2914855480 * b_;
        double l3 = l_ * l_ * l_, m3 = m_ * m_ * m_, s3 = s_ * s_ * s_;
        double rr = 4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3;
        double gg = -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3;
        double bb = -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3;
        auto gamma = [](double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; };
        r = gamma(rr) * 255.0; g = gamma(gg) * 255.0; b = gamma(bb) * 255.0;
    }

    static Color parseMilColor(const std::string& s) {
        if (s.empty()) return Color::White();
        // 数字: r<<24 | g<<16 | b<<8 | a
        bool allDigits = !s.empty();
        for (char c : s) if (c < '0' || c > '9') { allDigits = false; break; }
        if (allDigits) {
            try {
                uint64 v = std::stoull(s);
                double r = ((v >> 24) & 0xFF), g = ((v >> 16) & 0xFF), b = ((v >> 8) & 0xFF), a = (v & 0xFF);
                return Color{ r / 255.0, g / 255.0, b / 255.0, a / 255.0 };
            } catch (...) {}
        }
        // oklch(...)
        if (s.rfind("oklch", 0) == 0) {
            double r, g, b;
            oklchToRgb(s, r, g, b);
            return Color{ r / 255.0, g / 255.0, b / 255.0, 1.0 };
        }
        // CSS (#hex / rgb / hsl)
        try { return Color::FromCss(s); } catch (...) {}
        return Color::White();
    }

    geasy_phi::MilChart buildMilChartFromBuilder(const milize_js::Builder& b) {
        using namespace geasy_phi;
        MilChart chart {};
        chart.meta.noteFlowSpeedBehavior = MilMeta::NoteFlowSpeedBehavior::Override;
        chart.meta.worldOrigin = { (float64)-1920 / 2, (float64)1080 / 2 };
        chart.meta.worldViewport = { 1920, -1080 };
        chart.meta.speedUnit = 108.0;
        auto mget = [&](const char* k) -> std::string {
            auto it = b.meta.find(k);
            return it == b.meta.end() ? "" : it->second;
        };
        chart.meta.title = mget("Title");
        chart.meta.composer = mget("Composer");
        chart.meta.artist = mget("Illustrator");
        chart.meta.charter = mget("Beatmapper");
        chart.meta.difficultyName = mget("Difficulty");
        chart.meta.difficultyValue = b.hasDifficultyValue ? atof(mget("DifficultyValue").c_str()) : 0.0;

        // lines（含 m.line() 空线占位）
        uint64 lineIdx = 0;
        uint64 droppedNotes = 0;
        for (auto& bn : b.lines) {
            auto& line = chart.lines.emplace_back();
            line.indexer.set(chart.animator.indexGen.get({ EnumMilObjectType::Line, lineIdx++ }));
            for (auto& n : bn) {
                // [防御] 音符时间必须有限: NaN 会让 isHold()总为真(NaN != NaN),
                // 进而把 initHitEffects 的 while 变成死循环→内存爆炸(2026-09-15)。
                if (!std::isfinite(n.startTime) || !std::isfinite(n.endTime)) { droppedNotes++; continue; }
                auto& note = line.notes.emplace_back();
                note.indexer.set(chart.animator.indexGen.get({ EnumMilObjectType::Note, n.index }));
                note.type = MilNoteTypeHelper::FromInt(n.type);
                note.timeZone = { n.startTime, n.endTime };
                note.isFake = n.isFake;
                note.isAlwaysPerfect = n.isAlwaysPerfect;
            }
        }

        // storyboard
        uint64 sbIdx = 0;
        for (auto& s : b.storyboard) {
            auto& sb = chart.storyboardObjects.emplace_back();
            sb.indexer.set(chart.animator.indexGen.get({ EnumMilObjectType::Storyboard, sbIdx++ }));
            sb.type = MilStoryboardTypeHelper::FromInt(s.type);
            sb.data = s.data;
            sb.layer = MilStoryboardLayerHelper::FromInt(s.layer);
        }

        // animations → animator（与 loadMilChartFromDevJson 同构）
        uint64 eventIdx = 0;
        uint64 droppedAnims = 0;
        for (auto& a : b.anims) {
            if (!std::isfinite(a.fromBeat) || !std::isfinite(a.toBeat)) { droppedAnims++; continue; }
            MilEvent e {};
            e.index = eventIdx++;
            e.type = MilEventTypeHelper::FromInt(a.key);
            e.timeZone = { a.fromBeat, a.toBeat };
            if (e.type == EnumMilEventType::Color) {
                e.valueZone = chart.storyboardAssets.requestColorPair(parseMilColor(a.fvStr), parseMilColor(a.tvStr));
            } else {
                e.valueZone = { a.fv, a.tv };
            }
            if (a.press != 0) {
                e.easingFuncContext = (void*)EaseSet::Milthm::pack(a.ease, a.press);
                e.easingFunc = [](void* c, float64 p) { auto [x, y] = EaseSet::Milthm::unpack((uint64)c); return EaseSet::Milthm::easing(x, y, p); };
                e.easingIntFunc = [](void* c, float64 p) { auto [x, y] = EaseSet::Milthm::unpack((uint64)c); return EaseSet::Milthm::easing_int(x, y, p); };
            }
            chart.animator.addEvent({ MilObjectTypeHelper::FromInt(a.data), a.i1 }, e);
        }
        if (droppedNotes || droppedAnims) {
            std::fprintf(stderr, "[MilChart] 丢弃时间非有限的音符 %llu 个 / 动画 %llu 个\n",
                (unsigned long long)droppedNotes, (unsigned long long)droppedAnims);
        }
        return chart;
    }

    struct MilWindow {
        WindowBase base;

        gsp<MilTakeOverer> renderer;

        void init(bool hidden = false) {
            std::fprintf(stderr, "[dbg init] createGLfwWindow...\n");
            createGLfwWindow(base, hidden);
            std::fprintf(stderr, "[probe-eg] RET-createGLfwWindow\n");
            std::fprintf(stderr, "[dbg init] window ok, MilTakeOverer::Make...\n");

            renderer = MilTakeOverer::Make();
            renderer->lineHeadTextureLoader = MilStaticResourceHelpers::lineHeadTextureLoader;
            renderer->noteTextureDataLoader = MilStaticResourceHelpers::noteTextureDataLoader;
            renderer->hitsoundDataLoader = MilStaticResourceHelpers::hitsoundDataLoader;
            renderer->pauseButtonTextureDataLoader = MilStaticResourceHelpers::pauseButtonTextureDataLoader;

            renderer->glCtx = base.glCtx;
            renderer->sharedComp.textureDecoder = gimage::decode;
            std::fprintf(stderr, "[dbg init] font renderer...\n");
            renderer->textManager.renderer = createTextRendererFromData(MilStaticResourceHelpers::getFontData());
            std::fprintf(stderr, "[dbg init] audio engine...\n");
            renderer->audioManager.decoder = gminiaudio::decode;
            renderer->audioManager.engine = gminiaudio::makeAudioEngine();
            std::fprintf(stderr, "[dbg init] renderer->init()...\n");
            renderer->init();
            std::fprintf(stderr, "[probe-eg] RET-renderer-init\n");
            std::fprintf(stderr, "[dbg init] done\n");

            base.audioManagerRef = &renderer->audioManager;
        }

        auto loadChart(const std::string& path, const std::string& chartDir) {
            base.chartDir = chartDir;
            // [Storyboard 根目录自动寻找] 传入谱面文件路径, 贴图搜索根以"谱面实际所在目录"为基准(解压根可能在上一层)
            renderer->setStoryboardAssetDir(chartDir, path);

            // 统一用 UTF-8 路径读取(fopen 是 ANSI,中文路径必失败;Data::MakeFromFile 走 filesystem char8_t 可正常读)
            auto fileData = Data::MakeFromFile(path);
            std::string text = fileData.toString();

            // [Milize JS] 检测官方 JS 谱面(MilizeBeatmap)→ 内嵌 quickjs 执行 → 直接构造 MilChart
            if (text.find("MilizeBeatmap") != std::string::npos) {
                std::string err;
                auto builder = milize_js::ExecuteToBuilder(text, &err);
                if (err.empty() && !builder.bpms.empty()) {
                    std::printf("[Milize] 官方 JS 谱面由内嵌引擎直接解析: %s\n", path.c_str());
                    auto initer = [](MilChart& c) { c.init(); };
                    return renderer->loadChartFromMilChart(buildMilChartFromBuilder(builder), initer);
                }
                // JS 谱面 quickjs 执行失败:不要把 JS 文本塞给 JSON parser(必失败且误导)
                throw std::runtime_error(
                    "JS 谱面解析失败: " + (err.empty() ? "执行未报错但 builder.bpms 为空(可能是空谱面)" : err)
                    + "\n文件: " + path
                );
            }

            // [REMJSON] REM Editor 原生谱面(chart.json, 根含 chartStructure) -> 零转换直接构造 MilChart
            if (text.find("chartStructure") != std::string::npos) {
                std::printf("[REMJSON] REM 谱面由原生解析器直接读取: %s\n", path.c_str());
                auto initer = [](MilChart& c) { c.init(); };
                return renderer->loadChartFromMilChart(loadMilChartFromRemJson(fileData), initer);
            }

            auto resultInfo = renderer->loadChart({
                .data = std::move(fileData)
            });
            resultInfo.checkAndThrow();

            return resultInfo;
        }

        struct MainloopConfig {
            WindowBase::MainloopConfigBase base;
        };
        
        bool mainloopFrame(const MainloopConfig& config) {
            return base.mainloopFrame<decltype(renderer)>(config.base, renderer);
        }
    };

    struct RizWindow {
        WindowBase base;

        gsp<RizTakeOverer> renderer;

        void init() {
            createGLfwWindow(base);

            renderer = RizTakeOverer::Make();
            renderer->hitsoundDataLoader = RizStaticResourceHelpers::hitsoundDataLoader;
            
            renderer->glCtx = base.glCtx;
            renderer->sharedComp.textureDecoder = gimage::decode;
            renderer->textManager.renderer = createTextRendererFromData(MilStaticResourceHelpers::getFontData());
            renderer->audioManager.decoder = gminiaudio::decode;
            renderer->audioManager.engine = gminiaudio::makeAudioEngine();
            renderer->init();

            base.audioManagerRef = &renderer->audioManager;
        }

        auto loadChart(const std::string& path, const std::string& chartDir) {
            base.chartDir = chartDir;
            
            auto resultInfo = renderer->loadChart({
                .data = Data::MakeFromFile(path)
            });
            resultInfo.checkAndThrow();

            return resultInfo;
        }

        struct MainloopConfig {
            WindowBase::MainloopConfigBase base;
        };
        
        bool mainloopFrame(const MainloopConfig& config) {
            return base.mainloopFrame<decltype(renderer)>(config.base, renderer);
        }
    };

    std::string getDirectory(const std::string& path) {
        auto pos = path.find_last_of("\\/");
        if (pos == std::string::npos) return {};
        return path.substr(0, pos);
    }

    #if defined(APP_TYPE_OPEN_RPE_RECORDER)
    struct Settings {
        static constexpr const wchar_t* appKey = L"Open-RPE-Recorder";
        static constexpr DWORD currentVersion = 0;

        float64 musicVol = 1.0, sfxVol = 1.0;
        float64 noteScaling = 1.0;

        int32 recordWidth = 1920, recordHeight = 1080;
        float64 recordFPS = 60.0;
        bool recordSfxRandshake = false;

        bool disableH264QSV = false;

        void fromRegistry() {
            RegAPI api(appKey);
            
            DWORD lastVersion;
            if (api.readDword(L"version", lastVersion)) {
                if (lastVersion != currentVersion) {
                    showWarnMsg(nullptr, L"配置文件版本过新, 可能会导致部分配置丢失");
                }
            }
            api.writeDword(L"version", currentVersion);

            api.readDouble(L"musicVol", musicVol);
            api.readDouble(L"sfxVol", sfxVol);
            api.readDouble(L"noteScaling", noteScaling);

            api.readDword(L"recordWidth", recordWidth);
            api.readDword(L"recordHeight", recordHeight);
            api.readDouble(L"recordFPS", recordFPS);
            api.readBool(L"recordSfxRandshake", recordSfxRandshake);

            clampValues();
        }

        void saveToRegistry() {
            RegAPI api(appKey);

            api.writeDouble(L"musicVol", musicVol);
            api.writeDouble(L"sfxVol", sfxVol);
            api.writeDouble(L"noteScaling", noteScaling);

            api.writeDword(L"recordWidth", recordWidth);
            api.writeDword(L"recordHeight", recordHeight);
            api.writeDouble(L"recordFPS", recordFPS);
            api.writeBool(L"recordSfxRandshake", recordSfxRandshake);
        }

        void clampValues() {
            musicVol = std::clamp<float64>(musicVol, 0.0, 1.0);
            sfxVol = std::clamp<float64>(sfxVol, 0.0, 1.0);

            recordWidth = std::clamp<int32>(recordWidth, 1, 65536);
            recordHeight = std::clamp<int32>(recordHeight, 1, 65536);
            recordFPS = std::clamp<float64>(recordFPS, 0.01, 65536.0);
        }

        void onChanged() {
            clampValues();
            saveToRegistry();
        }

        bool getIsFirstRun() {
            RegAPI api(appKey);

            bool isFirstRun = true;
            api.readBool(L"isFirstRunFlag", isFirstRun);
            api.writeBool(L"isFirstRunFlag", false);
            return isFirstRun;
        }
    };

    void entrypoint() {
        int argc; char** argv;
        grain::get_args(&argc, &argv);
        
        std::vector<std::string> args(argv, argv + argc);
        auto hasArg = [&](const std::string& arg) {
            return std::find(args.begin(), args.end(), arg) != args.end();
        };

        PhiWindow backendWin {};
        backendWin.init();
        backendWin.base.window->setHidden(true);

        std::string chartRoot = "", chartPath = "", imagePath = "", audioPath = "";
        std::optional<ParsedRPEChartInfo> chartInfo;

        int32 chartFileInput, chartRootInput, bgFileInput, audioFileInput;
        int32 musicVolInput, sfxVolInput;
        int32 noteScalingInput;
        int32 recordWidthInput, recordHeightInput, recordFPSInput;
        int32 recordSfxRandshakeCheckBox;

        Settings settings {};
        settings.fromRegistry();

        auto win = Win32Window::Make({
            .title = L"Open RPE Recorder"
        });

        win->registerWidget(Widgets::Button({ .text = L"↗Github", .onClick = [&]() {
            ShellExecute(nullptr, "open", "https://github.com/qaqFei/easy-phi", nullptr, nullptr, SW_SHOWNORMAL);
        } }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"谱面选择" }));
        win->nextRow();

        win->registerWidget(Widgets::Button({ .text = L"从 info.txt ...", .onClick = [&]() {
            auto infoPathW = selectOpenFile(win.get(),  L"Info File (info.txt)\0info.txt\0All Files (*.*)\0*.*\0", L"打开 info.txt");
            auto infoPath = Win32Utils::wstringToString(infoPathW);
            if (infoPath.empty()) return;

            auto chartRoot = getDirectory(infoPath) + "/";
            WidgetStatics::TextInput::setText(win->refWidget(chartRootInput), Win32Utils::stringToWstring(chartRoot));

            Data infoData;
            if (!Data::MakeFromFile(infoData, infoPath)) {
                showErrorMsg(win.get(), L"无法打开 info.txt");
                return;
            }

            auto infos = ParsedRPEChartInfo::parse(infoData);
            if (infos.empty()) {
                showErrorMsg(win.get(), L"info.txt 中找不到谱面信息");
            }

            auto& info = infos[0];
            WidgetStatics::TextInput::setText(win->refWidget(chartFileInput), Win32Utils::stringToWstring(chartRoot + info.chart));
            WidgetStatics::TextInput::setText(win->refWidget(bgFileInput), Win32Utils::stringToWstring(chartRoot + info.picture));
            WidgetStatics::TextInput::setText(win->refWidget(audioFileInput), Win32Utils::stringToWstring(chartRoot + info.song));
            chartInfo = info;
        } }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"谱面根目录: " }));
        chartRootInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            chartRoot = Win32Utils::wstringToString(ws);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"浏览", .onClick = [&]() {
            auto folderW = selectFolder(win.get(), L"打开谱面根目录");
            auto folder = Win32Utils::wstringToString(folderW);
            if (folder.empty()) return;
            WidgetStatics::TextInput::setText(win->refWidget(chartRootInput), Win32Utils::stringToWstring(folder));
        } }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"谱面文件: " }));
        chartFileInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            chartPath = Win32Utils::wstringToString(ws);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"浏览", .onClick = [&]() {
            auto pathW = selectOpenFile(win.get(),  L"Chart File (*.json)\0*.json\0All Files (*.*)\0*.*\0", L"打开谱面文件");
            auto path = Win32Utils::wstringToString(pathW);
            if (path.empty()) return;
            WidgetStatics::TextInput::setText(win->refWidget(chartFileInput), Win32Utils::stringToWstring(path));
        } }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"曲绘文件: " }));
        bgFileInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            imagePath = Win32Utils::wstringToString(ws);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"浏览", .onClick = [&]() {
            auto pathW = selectOpenFile(win.get(),  L"Image File (*.png, *.jpg, *.jpeg)\0*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0", L"打开曲绘文件");
            auto path = Win32Utils::wstringToString(pathW);
            if (path.empty()) return;
            WidgetStatics::TextInput::setText(win->refWidget(bgFileInput), Win32Utils::stringToWstring(path));
        } }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"音频文件: " }));
        audioFileInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            audioPath = Win32Utils::wstringToString(ws);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"浏览", .onClick = [&]() {
            auto pathW = selectOpenFile(win.get(),  L"Audio File (*.mp3, *.wav, *.ogg, *.flac, *.m4a, *.aac)\0*.mp3;*.wav;*.ogg;*.flac;*.m4a;*.aac\0All Files (*.*)\0*.*\0", L"打开音频文件");
            auto path = Win32Utils::wstringToString(pathW);
            if (path.empty()) return;
            WidgetStatics::TextInput::setText(win->refWidget(audioFileInput), Win32Utils::stringToWstring(path));
        } }));
        win->nextRow();

        win->nextRow();

        bool isSyncingSettings = true;

        auto syncSettingsToUI = [&](bool allowEmpty = false) {
            isSyncingSettings = true;

            auto checkbox = [&](int id, bool checked) {
                WidgetStatics::CheckBox::toggle(win->refWidget(id), checked);
            };

            auto float64Input = [&](int id, float64 value) {
                auto& text = win->refWidget(id).store.textInput.text;

                try {
                    if ((allowEmpty && text.empty()) || std::stold(text) == value) {
                        return;
                    }
                } catch (...) { }

                WidgetStatics::TextInput::setText(win->refWidget(id), Win32Utils::stringToWstring(std::format("{:.10g}", value)));
            };

            auto intInput = [&](int id, int value) {
                auto& text = win->refWidget(id).store.textInput.text;

                try {
                    if ((allowEmpty && text.empty()) || std::stoi(text) == value) {
                        return;
                    }
                } catch (...) { }

                WidgetStatics::TextInput::setText(win->refWidget(id), std::to_wstring(value));
            };

            float64Input(musicVolInput, settings.musicVol);
            float64Input(sfxVolInput, settings.sfxVol);
            float64Input(noteScalingInput, settings.noteScaling);

            intInput(recordWidthInput, settings.recordWidth);
            intInput(recordHeightInput, settings.recordHeight);
            float64Input(recordFPSInput, settings.recordFPS);
            checkbox(recordSfxRandshakeCheckBox, settings.recordSfxRandshake);

            isSyncingSettings = false;
        };

        auto settingsChanged = [&]() {
            settings.onChanged();
            syncSettingsToUI(true);
        };

        win->registerWidget(Widgets::Label({ .text = L"设置 (通用)" }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"音乐音量: " }));
        musicVolInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.musicVol = std::stold(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"音效音量: " }));
        sfxVolInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.sfxVol = std::stold(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"音符缩放: " }));
        noteScalingInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.noteScaling = std::stold(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->nextRow();

        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"设置 (视频参数)" }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"分辨率: " }));
        recordWidthInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.recordWidth = std::stoi(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->registerWidget(Widgets::Label({ .text = L"x" }));
        recordHeightInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.recordHeight = std::stoi(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"帧率: " }));
        recordFPSInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.recordFPS = std::stold(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->nextRow();

        recordSfxRandshakeCheckBox = win->registerWidget(Widgets::CheckBox({ .text = L"打击音随机抖动", .onChange = [&](bool checked) {
            if (isSyncingSettings) return;
            settings.recordSfxRandshake = checked;
            settingsChanged();
        } }));
        win->registerWidget(Widgets::Button({ .text = L"?", .onClick = [&]() {
            showInfoMsg(win.get(), L"由于本家即使同时打击音符, 打击音效也并不是在同一时刻播放, 启用该选项后, 打击音效会在一定范围内随机延迟播放, 以模拟本家多押的神秘听感。");
        } }));
        win->nextRow();

        win->nextRow();

        float64 loadingChartTook;

        auto load = [&]() {
            try {
                auto loadResult = backendWin.loadChart(chartPath, chartRoot);
                loadingChartTook = loadResult.totalTook();

                if (chartInfo.has_value()) {
                    auto& info = chartInfo.value();
                    backendWin.renderer->chart.meta.title = info.name;
                    backendWin.renderer->chart.meta.difficulty = info.level;
                }

                backendWin.renderer->chart.options.noteScaling *= settings.noteScaling;
                backendWin.renderer->loadIllustion(imagePath);
                backendWin.renderer->audioManager.load(audioPath);
            } catch (const std::exception& e) {
                auto msg = Win32Utils::stringToWstring(e.what());
                showErrorMsg(win.get(), msg.c_str());
                return false;
            }

            return true;
        };

        struct WinHiddenGuard {
            Win32Window* win;
            WinHiddenGuard(Win32Window* win) : win(win) { win->setHidden(true); }
            ~WinHiddenGuard() { win->setHidden(false); }
        };

        win->registerWidget(Widgets::Button({ .text = L"预览", .onClick = [&]() {
            if (!load()) return;
            WinHiddenGuard whguard(win.get());

            backendWin.base.window->setHidden(false);
            backendWin.base.window->setSwapInterval(hasArg("--disable-vsync") ? 0 : 1);
            backendWin.renderer->audioManager.startBgm();

            backendWin.renderer->audioManager.setBgmVolume(settings.musicVol);
            backendWin.renderer->audioManager.setSfxVolume(settings.sfxVol);

            while (!backendWin.renderer->audioManager.getBpmIsEnded()) {
                if (!backendWin.mainloopFrame({})) {
                    backendWin.renderer->audioManager.stopBgm();
                    break;
                }
            }

            backendWin.base.window->setHidden(true);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"渲染视频", .onClick = [&]() {
            auto videoPathW = selectSaveFile(win.get(),  L"视频文件 (*.mp4)\0*.mp4\0All Files (*.*)\0*.*\0", L"保存视频文件");
            auto videoPath = Win32Utils::wstringToString(videoPathW);
            if (videoPath.empty()) return;

            gwin_progress_dialog::ProgressDialog pd {};
            pd.setTitle(L"Open RPE Record -- 渲染视频");
            pd.start();

            pd.setLine(1, L"初始化...");
            WinHiddenGuard whguard(win.get());

            gvideoenc::VideoCap cap {};
            cap.init(videoPath, settings.recordWidth, settings.recordHeight, settings.recordFPS);
            
            using FrameType = std::optional<uint64>;
            gthread_safe_queue::ThreadSafeQueue<FrameType> frameQueue;

            auto videoRecorder = VideoRecorder::Make(
                backendWin.base.glCtx,
                settings.recordWidth, settings.recordHeight,
                [&](uint64 slotIndex) { frameQueue.enqueue(slotIndex); },
                {
                    .callbackIsThreadSafe = true
                }
            );

            auto frameWriter = [&]() {
                FrameType frame;

                while (true) {
                    frameQueue.wait_dequeue(frame);
                    if (!frame.has_value()) break;

                    auto* yuv = videoRecorder->referYUVFrame(frame.value());
                    cap.writeVideoFrame(yuv->dataPtrs().data(), yuv->rowBytes().data());
                    videoRecorder->returnYUVFrame(frame.value());
                }
            };

            backendWin.base.surfaceSize = { settings.recordWidth, settings.recordHeight };
            backendWin.base.window->setSwapInterval(0);

            pd.setLine(1, L"加载...");
            if (!load()) return;
            
            pd.setLine(1, L"渲染音频...");
            auto mixedAudio = backendWin.renderer->mixFinalBgm(backendWin.renderer->chart, {
                .musicVol = settings.musicVol,
                .sfxVol = settings.sfxVol,
                .sfxRandshake = settings.recordSfxRandshake
            });

            pd.setLine(1, L"写入音频...");
            cap.writeAudio(mixedAudio);

            pd.setLine(1, L"渲染视频...");
            uint64 frameCut = 0;
            std::thread frameWriterThread(frameWriter);
            FramerateMeter fpsMeter {};
            
            while (true) {
                float64 t = frameCut / settings.recordFPS;
                if (t > backendWin.renderer->calcConfig.songLength) break;

                auto reGuard = videoRecorder->useFrame();
                backendWin.mainloopFrame({
                    .base = {
                        .time = t,
                        .isRenderingVideo = true
                    }
                });

                frameCut++;
                fpsMeter.frame();

                uint64 totalFrames = backendWin.renderer->calcConfig.songLength * settings.recordFPS;
                pd.setProgress(frameCut, totalFrames);

                {
                    std::wstring msg = L"渲染视频... ";
                    msg += std::to_wstring(frameCut) + L"/" + std::to_wstring(totalFrames) + L" (" + std::to_wstring((uint64)fpsMeter.get()) + L" fps)";
                    pd.setLine(1, msg.c_str());
                }
            }

            videoRecorder->finish();

            frameQueue.enqueue(std::nullopt);
            frameWriterThread.join();

            pd.setLine(1, L"释放资源...");
            std::wstring msg = L"渲染完成, 已保存到 " + Win32Utils::stringToWstring(videoPath);
            showInfoMsg(win.get(), msg.c_str());
        } }));

        win->createWidgets();
        win->doGrid();
        win->resizeToGridBounds();
        syncSettingsToUI();

        win->mainloop();
    }
    #elif defined(APP_TYPE_TEST_MIL)
    void entrypoint() {
        int argc; char** argv;
        grain::get_args(&argc, &argv);
        
        std::vector<std::string> args(argv, argv + argc);
        auto hasArg = [&](const std::string& arg) {
            return std::find(args.begin(), args.end(), arg) != args.end();
        };

        std::vector<std::string> chartNames = {
            "Dum! Dum!! Dum!!! - Tatsunoshin",
            "Fly To Meteor (Milthm Edit) - ShooTinGStaR + xzadudu179 + Cyberspace",
            "FULi AUTO SHOOTER - MYUKKE",
            "INFP.mp3 - TsukiP",
            "Moonflutter - Reku Mochizuki",
            "oiiaioooooiai - 黑瘦的鱼头",
            "Regnaissance - AiSS vs. Abit",
            "WATER - A-39 & 沙包P",
            "Welcome to Milthm - TsuKiP",
            "☹ - Gray Planet",
            "サイクルの欠片 - TsukiP",
            "粗线条的雨 - 有棵里里",
            "雨之城 - CsLrisEto",
            "靈 - MYTK",
            "驟雨の狭間 - Silentroom"
        };
        
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        for (uint64 i = 0; i < (uint64)chartNames.size(); ++i) {
            std::cout << (i + 1) << ". " << chartNames[i] << std::endl;
        }

        uint64 choice;
        std::cout << ">> ";
        std::cin >> choice;
        choice -= 1;

        if (choice < 0 || choice >= (uint64)chartNames.size()) {
            std::cout << "Invalid choice" << std::endl;
            return;
        }

        std::string prefix = "./milcharts/";
        std::string chartPath = prefix + chartNames[choice] + "/.json"
                  , imagePath = prefix + chartNames[choice] + "/.png"
                  , audioPath = prefix + chartNames[choice] + "/.wav"
                  , storyboardAssetsPath = prefix + chartNames[choice] + "/";

        MilWindow window {};
        window.base.fullscreen = hasArg("--fullscreen");
        window.init();
        window.base.window->setSwapInterval(hasArg("--disable-vsync") ? 0 : 1);

        window.loadChart(chartPath, storyboardAssetsPath);
        window.renderer->loadIllustion(imagePath);
        window.renderer->audioManager.load(audioPath);

        window.renderer->audioManager.startBgm();

        while (!window.renderer->audioManager.getBpmIsEnded()) {
            if (!window.mainloopFrame({})) {
                break;
            }
        }
    }
    #elif defined(APP_TYPE_TEST_RIZ)
    void entrypoint() {
        int argc; char** argv;
        grain::get_args(&argc, &argv);
        
        std::vector<std::string> args(argv, argv + argc);
        auto hasArg = [&](const std::string& arg) {
            return std::find(args.begin(), args.end(), arg) != args.end();
        };

        std::vector<std::string> chartNames = {
            "PastelLines",
            "LAIGN"
        };
        
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        for (uint64 i = 0; i < (uint64)chartNames.size(); ++i) {
            std::cout << (i + 1) << ". " << chartNames[i] << std::endl;
        }

        uint64 choice;
        std::cout << ">> ";
        std::cin >> choice;
        choice -= 1;

        if (choice < 0 || choice >= (uint64)chartNames.size()) {
            std::cout << "Invalid choice" << std::endl;
            return;
        }

        std::string prefix = "./rizcharts/";
        std::string chartPath = prefix + chartNames[choice] + "/.json"
                  , imagePath = prefix + chartNames[choice] + "/.png"
                  , audioPath = prefix + chartNames[choice] + "/.wav"
                  , chartDir = prefix + chartNames[choice] + "/";

        RizWindow window {};

        window.base.isPortrait = true;
        window.base.fullscreen = hasArg("--fullscreen");
        window.init();
        window.base.window->setSwapInterval(hasArg("--disable-vsync") ? 0 : 1);

        window.loadChart(chartPath, chartDir);
        window.renderer->loadIllustion(imagePath);
        window.renderer->audioManager.load(audioPath);

        window.renderer->audioManager.startBgm();

        while (!window.renderer->audioManager.getBpmIsEnded()) {
            if (!window.mainloopFrame({})) {
                break;
            }
        }
    }
                #elif defined(APP_TYPE_MIL_PLAYER)
    struct Settings {
        static constexpr const wchar_t* appKey = L"Mil-Recorder";
        static constexpr DWORD currentVersion = 0;

        float64 musicVol = 1.0, sfxVol = 1.0;
        float64 noteScaling = 1.0;

        int32 recordWidth = 1920, recordHeight = 1080;
        float64 recordFPS = 60.0;
        bool recordSfxRandshake = false;
        bool introEnabled = true;   // [Intro] 开场动画渲染开关

        void fromRegistry() {
            RegAPI api(appKey);
            
            DWORD lastVersion;
            if (api.readDword(L"version", lastVersion)) {
                if (lastVersion != currentVersion) {
                    showWarnMsg(nullptr, L"配置文件版本过新, 可能会导致部分配置丢失");
                }
            }
            api.writeDword(L"version", currentVersion);

            api.readDouble(L"musicVol", musicVol);
            api.readDouble(L"sfxVol", sfxVol);
            api.readDouble(L"noteScaling", noteScaling);

            api.readDword(L"recordWidth", recordWidth);
            api.readDword(L"recordHeight", recordHeight);
            api.readDouble(L"recordFPS", recordFPS);
            api.readBool(L"recordSfxRandshake", recordSfxRandshake);
            api.readBool(L"introEnabled", introEnabled);

            clampValues();
        }

        void saveToRegistry() {
            RegAPI api(appKey);

            api.writeDouble(L"musicVol", musicVol);
            api.writeDouble(L"sfxVol", sfxVol);
            api.writeDouble(L"noteScaling", noteScaling);

            api.writeDword(L"recordWidth", recordWidth);
            api.writeDword(L"recordHeight", recordHeight);
            api.writeDouble(L"recordFPS", recordFPS);
            api.writeBool(L"recordSfxRandshake", recordSfxRandshake);
            api.writeBool(L"introEnabled", introEnabled);
        }

        void clampValues() {
            musicVol = std::clamp<float64>(musicVol, 0.0, 1.0);
            sfxVol = std::clamp<float64>(sfxVol, 0.0, 1.0);

            recordWidth = std::clamp<int32>(recordWidth, 1, 65536);
            recordHeight = std::clamp<int32>(recordHeight, 1, 65536);
            recordFPS = std::clamp<float64>(recordFPS, 0.01, 65536.0);
        }

        void onChanged() {
            clampValues();
            saveToRegistry();
        }
    };

    // [dbg] 白块排查：单帧对象转储 —— 走与渲染完全相同的 calculateMilFrame 链路，
    // 把该帧提交的全部绘制对象（类型/位置/尺寸/旋转/颜色）打到 stderr，
    // 直接定位"白块"由哪个对象、以什么几何画出来。
    void dumpMilObjects(MilWindow& win, double t) {
        auto* renderer = win.renderer.get();
        renderer->calcConfig.screenSize = { (float64)win.base.surfaceSize.first, (float64)win.base.surfaceSize.second };

        win.mainloopFrame({ .base = { .time = t, .isRenderingVideo = true } });

        auto& chart = renderer->chart;
        auto& fr = renderer->calculatedFrame;

        auto P = [](const Vec2& v) {
            char buf[64]; std::snprintf(buf, sizeof buf, "(%.0f,%.0f)", v.x, v.y); return std::string(buf);
        };
        auto C = [](const Color& c) {
            char buf[96]; std::snprintf(buf, sizeof buf, "(%.2f,%.2f,%.2f,%.2f)", c.r, c.g, c.b, c.a); return std::string(buf);
        };
        auto isWhite = [](const Color& c) { return c.r > 0.85 && c.g > 0.85 && c.b > 0.85 && c.a > 0.05; };

        std::fprintf(stderr, "\n========== [dump @ t=%.3fs] screen=(%d,%d) ==========\n",
            t, (int)win.base.surfaceSize.first, (int)win.base.surfaceSize.second);

        // A) 线级状态：统计 t 时刻真正会画的线（head/body alpha>0），与官方激活门控对比
        int visHead = 0, visBody = 0, whiteLines = 0;
        int printed = 0;
        for (uint64 i = 0; i < chart.lines.size(); i++) {
            auto& line = chart.lines[i];
            auto la = chart.animator.get(line, t, EnumMilEventType::Transparency);
            auto ha = chart.animator.get(line, t, EnumMilEventType::LineHeadTransparency) * la;
            auto ba = chart.animator.get(line, t, EnumMilEventType::LineBodyTransparency) * la;
            if (ha <= 0.0 && ba <= 0.0) continue;
            auto pos = chart.getObjectPosition(t, line, renderer->calcConfig.screenSize);
            auto rot = chart.animator.get(line, t, EnumMilEventType::Rotation);
            auto sz = chart.animator.get(line, t, EnumMilEventType::Size);
            auto cIdx = chart.animator.get(line, t, EnumMilEventType::Color);
            auto zone = chart.animator.get_zone(line, t, EnumMilEventType::Color);
            auto col = chart.storyboardAssets.getColor(cIdx, zone);
            if (ha > 0.0) visHead++;
            if (ba > 0.0) visBody++;
            bool wh = isWhite(col);
            if (wh) whiteLines++;
            if (printed++ < 40)
                std::fprintf(stderr, "[A] line#%zu pos=%s rot=%.1f size=%.2f col=%s headA=%.3f bodyA=%.3f%s\n",
                    i, P(pos).c_str(), rot, sz, C(col).c_str(), ha, ba, wh ? "  <WHITE>" : "");
        }
        std::fprintf(stderr, "[A] visible: head>0 = %d, body>0 = %d, whiteVisible = %d (of %zu lines)\n",
            visHead, visBody, whiteLines, chart.lines.size());

        // A2) 故事板对象：逐个打印 type/layer/data 与 t 时刻求值(alpha>0 才是真正提交的)
        {
            int nSbPic = 0, nSbVisible = 0;
            for (uint64 i = 0; i < chart.storyboardObjects.size(); i++) {
                auto& sb = chart.storyboardObjects[i];
                auto alpha = chart.animator.get(sb, t, EnumMilEventType::Transparency);
                auto sz = chart.animator.get(sb, t, EnumMilEventType::Size);
                auto px = chart.animator.get(sb, t, EnumMilEventType::PositionX);
                auto py = chart.animator.get(sb, t, EnumMilEventType::PositionY);
                auto rot = chart.animator.get(sb, t, EnumMilEventType::Rotation);
                bool isPic = sb.type == EnumMilStoryboardType::Picture;
                if (isPic) nSbPic++;
                if (alpha > 0.0) nSbVisible++;
                std::fprintf(stderr, "[S] #%02zu %-7s layer=%d idx=%llu a=%.3f size=%.2f pos=(%.0f,%.0f) rot=%.1f '%s'\n",
                    i, isPic ? "Picture" : "Text", (int)sb.layer, (unsigned long long)sb.indexer.get(),
                    alpha, sz, px, py, rot, sb.data.c_str());
            }
            std::fprintf(stderr, "[S] storyboard total = %zu, picture = %d, visible(alpha>0) = %d\n",
                chart.storyboardObjects.size(), nSbPic, nSbVisible);
        }

        // B) 帧对象：逐个打印全部已提交对象
        using namespace geasy_phi;
        int nObj = 0, nWhiteObj = 0;
        auto describe = [&](const char* kind, const Vec2* pos, const Vec2* size, float64 rot, const Color& col, const char* extra) {
            nObj++;
            bool wh = isWhite(col);
            if (wh) nWhiteObj++;
            char pbuf[128] = "-", sbuf[128] = "-";
            if (pos) std::snprintf(pbuf, sizeof pbuf, "%s", P(*pos).c_str());
            if (size) std::snprintf(sbuf, sizeof sbuf, "%s", P(*size).c_str());
            if (nObj <= 120)
                std::fprintf(stderr, "[B] #%03d %-14s pos=%s size=%s rot=%.1f col=%s%s%s\n",
                    nObj, kind, pbuf, sbuf, rot, C(col).c_str(), wh ? "  <WHITE>" : "", extra ? extra : "");
        };

        for (auto& obj : fr.objects) {
            if (std::holds_alternative<SharedCalculatedObjects::CalculatedText>(obj)) {
                auto& o = std::get<SharedCalculatedObjects::CalculatedText>(obj);
                describe("Text", &o.position, &o.scale, o.rotation, o.color, (" '" + o.text + "'").c_str());
            } else if (std::holds_alternative<SharedCalculatedObjects::CalculatedRect>(obj)) {
                auto& o = std::get<SharedCalculatedObjects::CalculatedRect>(obj);
                describe("Rect", &o.position, &o.size, o.rotation, o.color, nullptr);
            } else if (std::holds_alternative<SharedCalculatedObjects::CalculatedPoly>(obj)) {
                auto& o = std::get<SharedCalculatedObjects::CalculatedPoly>(obj);
                describe("Poly", nullptr, nullptr, 0.0, o.color, nullptr);
            } else if (std::holds_alternative<SharedCalculatedObjects::CalculatedCapRoundedLineSet>(obj)) {
                auto& o = std::get<SharedCalculatedObjects::CalculatedCapRoundedLineSet>(obj);
                char buf[64]; std::snprintf(buf, sizeof buf, " count=%zu w=%.0f", o.count, o.width);
                describe("CapLineSet", nullptr, nullptr, 0.0, o.items && o.count ? o.items[0].c1 : Color::White(), buf);
            } else if (std::holds_alternative<MilCalculatedFrame::CalculatedLineHead>(obj)) {
                auto& o = std::get<MilCalculatedFrame::CalculatedLineHead>(obj);
                describe("LineHead", &o.position, &o.scale, o.rotation, o.color, nullptr);
            } else if (std::holds_alternative<MilCalculatedFrame::CalculatedLineBody>(obj)) {
                auto& o = std::get<MilCalculatedFrame::CalculatedLineBody>(obj);
                describe("LineBody", &o.origin, &o.size, o.rotation, o.color, nullptr);
            } else if (std::holds_alternative<MilCalculatedFrame::CalculatedNote>(obj)) {
                auto& o = std::get<MilCalculatedFrame::CalculatedNote>(obj);
                bool isAct = false;
                try { isAct = std::get<4>(o.textureDesc); } catch (...) {}
                char dbuf[48]; std::snprintf(dbuf, sizeof dbuf, " %s", isAct ? "<ACTIVE>" : "<BASE>");
                describe("Note", &o.position, nullptr, o.rotation, o.color, dbuf);
            } else if (std::holds_alternative<MilCalculatedFrame::CalculatedPauseButton>(obj)) {
                auto& o = std::get<MilCalculatedFrame::CalculatedPauseButton>(obj);
                describe("PauseBtn", &o.position, nullptr, o.rotation, o.color, nullptr);
            } else if (std::holds_alternative<MilCalculatedFrame::CalculatedParticles>(obj)) {
                auto& o = std::get<MilCalculatedFrame::CalculatedParticles>(obj);
                describe("Particles", nullptr, nullptr, 0.0, Color::White(), (" count=" + std::to_string(o.count)).c_str());
            } else if (std::holds_alternative<MilCalculatedFrame::CalculatedHitEffectTexture>(obj)) {
                auto& o = std::get<MilCalculatedFrame::CalculatedHitEffectTexture>(obj);
                describe("HitEffect", &o.position, &o.size, o.rotation, o.color, nullptr);
            }
        }
        std::fprintf(stderr, "[B] total submitted objects = %d, white candidates = %d\n", nObj, nWhiteObj);
        std::fprintf(stderr, "========== [dump end @ t=%.3fs] ==========\n\n", t);
    }

    // [dbg] CLI 帧转储调试：mil-recorder.exe --dumpmil <chart_path> <W> <H> [t1 t2 ...]
    // [CLI zip] 解压谱面 zip 并识别三件套: 谱面(.js/.json, 排除 meta.json) + 音频(排除 hit/drag/tap 命中音, 取最大) + 曲绘(取最大)
    struct MilZipResolved {
        std::string dir, chart, audio, image;
    };
    // [Intro] 起 BGM 前播载入信息页(t<0), 时长 = introLeadTotal(开场 4.2 + 收场后停留 3.0)
    // 计时用 steady_clock: 此时 BGM 尚未开始, 不能用音频时间
    inline void playMilIntroLead(MilWindow& win) {
        if (!MilTakeOverer::introEnabled) return;   // --no-intro: 不播载入页前导
        using clock = std::chrono::steady_clock;
        const float64 intro = geasy_phi::introLeadTotal;
        const auto t0 = clock::now();
        while (true) {
            const float64 el = std::chrono::duration<float64>(clock::now() - t0).count();
            if (el >= intro) break;
            if (!win.mainloopFrame({ .base = { .time = el - intro } })) break;
        }
    }

    MilZipResolved resolveMilZip(const std::string& zipPath) {
        namespace fs = std::filesystem;
        fs::path zipFs(zipPath);
        fs::path extractDir = fs::temp_directory_path() / ("mil_recorder_" + zipFs.stem().string());
        std::error_code ec;
        fs::remove_all(extractDir, ec);
        fs::create_directories(extractDir, ec);

        mz_zip_archive zip {};
        mz_zip_zero_struct(&zip);
        if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0))
            throw std::runtime_error("failed to open zip: " + zipPath);
        int n = (int)mz_zip_reader_get_num_files(&zip);
        std::fprintf(stderr, "[dbg zip] %d entries\n", n);
        for (int i = 0; i < n; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
            if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
            fs::path rel = fs::path(st.m_filename).lexically_normal();
            if (rel.is_absolute() || rel.string().find("..") != std::string::npos) continue;
            fs::path out = extractDir / rel;
            fs::create_directories(out.parent_path(), ec);
            if (!mz_zip_reader_extract_to_file(&zip, i, out.string().c_str(), 0))
                std::fprintf(stderr, "[dbg zip] extract fail: %s\n", st.m_filename);
        }
        mz_zip_reader_end(&zip);
        std::fprintf(stderr, "[dbg zip] extracted to %s\n", extractDir.string().c_str());

        MilZipResolved r;
        r.dir = extractDir.string();
        uint64 audioSize = 0, imgSize = 0;
        for (auto& e : fs::recursive_directory_iterator(extractDir)) {
            if (!e.is_regular_file()) continue;
            std::string lower = e.path().filename().string();
            for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
            std::string ext = e.path().extension().string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (lower == "meta.json" || lower == "shaders.json") continue;
            if (lower == "chart.json") { r.chart = e.path().string(); continue; }   // [REMJSON] prefer chart.json
            if (ext == ".js" || ext == ".json") {
                if (r.chart.empty()) r.chart = e.path().string();
            } else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac" || ext == ".m4a" || ext == ".aac") {
                if (lower.rfind("hit", 0) == 0 || lower.rfind("drag", 0) == 0 || lower.rfind("tap", 0) == 0) continue;
                if (e.file_size() > audioSize) { audioSize = e.file_size(); r.audio = e.path().string(); }
            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                if (e.file_size() > imgSize) { imgSize = e.file_size(); r.image = e.path().string(); }
            }
        }
        std::fprintf(stderr, "[dbg zip] chart='%s' audio='%s' image='%s'\n",
            r.chart.c_str(), r.audio.c_str(), r.image.c_str());
        if (r.chart.empty()) throw std::runtime_error("zip 中找不到谱面文件(.js/.json)");
        if (r.audio.empty()) throw std::runtime_error("zip 中找不到音频文件(.wav/.mp3/.ogg/...)");
        // [Storyboard 根目录自动寻找] 贴图/图片根 = 谱面文件实际所在目录(解压根可能在上一层, 如 charts/<name>/)
        {
            fs::path chartParent = fs::path(r.chart).parent_path();
            if (!chartParent.empty()) r.dir = chartParent.lexically_normal().string();
            std::fprintf(stderr, "[dbg zip] assetDir(自动定位) = %s\n", r.dir.c_str());
        }
        return r;
    }

    // [CLI zip] 谱面 zip 直接播放: 解压到临时目录, 找谱面+音频+曲绘, 预览播放
    void runMilZipPlay(const std::string& zipPath) {
        try {
            auto r = resolveMilZip(zipPath);
            MilWindow backendWin {};
            backendWin.init(true);
            backendWin.loadChart(r.chart, r.dir);
            std::fprintf(stderr, "[dbg zipplay] loadChart ok\n");
            if (!r.image.empty()) backendWin.renderer->loadIllustion(r.image);
            backendWin.renderer->audioManager.load(r.audio);
            backendWin.base.window->setHidden(false);
            backendWin.base.window->setSwapInterval(1);
            // [Intro] 起 BGM 前先播 2.05s 载入信息页(t<0), 时序与 --record 一致
            playMilIntroLead(backendWin);
            backendWin.renderer->audioManager.startBgm();
            while (!backendWin.renderer->audioManager.getBpmIsEnded()) {
                if (!backendWin.mainloopFrame({})) { backendWin.renderer->audioManager.stopBgm(); break; }
            }
            backendWin.base.window->setHidden(true);
            std::fprintf(stderr, "[dbg zipplay] DONE\n");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[dbg zipplay] EXCEPTION: %s\n", e.what());
        }
    }

    // [CLI] 渲染视频: --record <chart_or_zip> <W> <H> <fps> <out.mp4> [audio]
    // 复用 GUI"渲染视频"链路(VideoCap + VideoRecorder + frameWriter + mixFinalBgm); zip 自动找音频, 裸谱面可传第 7 参音频
    void runMilRecord(const std::string& chartOrZip, int W, int H, double fps, const std::string& outPath, const std::string& audioArg = "") {
        try {
            std::string chartPath, chartDir, audioPath, imagePath;
            std::string lower = chartOrZip;
            for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
            if (lower.size() >= 4 && lower.rfind(".zip") == lower.size() - 4) {
                auto r = resolveMilZip(chartOrZip);
                chartPath = r.chart; chartDir = r.dir; audioPath = r.audio; imagePath = r.image;
            } else {
                chartPath = chartOrZip;
                auto slash = chartOrZip.find_last_of("\\/");
                if (slash != std::string::npos) chartDir = chartOrZip.substr(0, slash);
                audioPath = audioArg;
            }
            if (audioPath.empty()) throw std::runtime_error("缺少音频(裸谱面请传第 7 参音频路径; zip 会自动查找)");

            Settings settings {};
            settings.fromRegistry();

            MilWindow backendWin {};
            backendWin.init(true);
            backendWin.base.window->setHidden(true);
            backendWin.base.window->setSwapInterval(0);
            backendWin.base.surfaceSize = { (uint64)W, (uint64)H };
            backendWin.loadChart(chartPath, chartDir);
            if (!imagePath.empty()) {
                try { backendWin.renderer->loadIllustion(imagePath); }
                catch (const std::exception& e) { std::fprintf(stderr, "[dbg record] jacket decode failed (%s), fallback to mask\n", e.what()); }
                catch (...) { std::fprintf(stderr, "[dbg record] jacket decode failed, fallback to mask\n"); }
            }
            backendWin.renderer->audioManager.load(audioPath);
            std::fprintf(stderr, "[dbg record] loadChart ok, songLength=%.2fs\n", backendWin.renderer->audioManager.getBgmLength());

            gvideoenc::VideoCap cap {};
            cap.init(outPath, W, H, fps);

            using FrameType = std::optional<uint64>;
            gthread_safe_queue::ThreadSafeQueue<FrameType> frameQueue;
            auto videoRecorder = VideoRecorder::Make(
                backendWin.base.glCtx, (uint64)W, (uint64)H,
                [&](uint64 slotIndex) { frameQueue.enqueue(slotIndex); },
                { .callbackIsThreadSafe = true }
            );
            auto frameWriter = [&]() {
                FrameType frame;
                while (true) {
                    frameQueue.wait_dequeue(frame);
                    if (!frame.has_value()) break;
                    auto* yuv = videoRecorder->referYUVFrame(frame.value());
                    cap.writeVideoFrame(yuv->dataPtrs().data(), yuv->rowBytes().data());
                    videoRecorder->returnYUVFrame(frame.value());
                }
            };

            std::fprintf(stderr, "[dbg record] mixing audio...\n");
            auto mixedAudio = backendWin.renderer->mixFinalBgm(backendWin.renderer->chart, {
                .musicVol = settings.musicVol,
                .sfxVol = settings.sfxVol,
                .sfxRandshake = settings.recordSfxRandshake
            });
            // [Intro] 视频从 -introLead 起渲染(载入信息页), 音频前插等长静音以保持音画同步
            const float64 introLead = MilTakeOverer::introEnabled ? geasy_phi::introLeadTotal : 0.0;   // --no-intro 时为 0: 无开场帧/无静音前插
            {
                auto lead = mixedAudio->copy();   // 复用类型, 避免 gdecoded_audio 别名在此作用域已 undef
                lead->channels = mixedAudio->channels;
                lead->sampleRate = mixedAudio->sampleRate;
                lead->data.assign((size_t)((float64)lead->sampleRate * introLead) * lead->channels, 0);
                lead->data.insert(lead->data.end(), mixedAudio->data.begin(), mixedAudio->data.end());
                cap.writeAudio(lead);
            }
            // ⚠ calcConfig.songLength 首次 render() 才赋值, 这里必须用 audioManager 的长度
            const float64 recSongLength = backendWin.renderer->audioManager.getBgmLength();
            std::fprintf(stderr, "[dbg record] intro lead = %.2fs, song = %.2fs, total = %.2fs\n",
                introLead, recSongLength, introLead + recSongLength);

            std::fprintf(stderr, "[dbg record] rendering frames...\n");
            std::thread frameWriterThread(frameWriter);
            uint64 frameCut = 0;
            while (true) {
                float64 t = frameCut / fps - introLead;   // t < 0 = 载入信息页
                if (t > recSongLength) break;
                auto reGuard = videoRecorder->useFrame();
                backendWin.mainloopFrame({
                    .base = {
                        .time = t,
                        .isRenderingVideo = true
                    }
                });
                frameCut++;
                if (frameCut % 300 == 0)
                    std::fprintf(stderr, "[dbg record] frame %llu / ~%llu\n",
                        (unsigned long long)frameCut,
                        (unsigned long long)((recSongLength + introLead) * fps));
            }
            videoRecorder->finish();
            frameQueue.enqueue(std::nullopt);
            frameWriterThread.join();
            std::fprintf(stderr, "[dbg record] DONE: %llu frames -> %s\n",
                (unsigned long long)frameCut, outPath.c_str());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[dbg record] EXCEPTION: %s\n", e.what());
        }
    }

    void runMilDump(const std::string& chartPath, int W, int H, const std::vector<double>& times) {
        std::fprintf(stderr, "[dbg dumpmil] chart='%s' size=%dx%d\n", chartPath.c_str(), W, H);
        try {
            std::string chartDir;
            auto slash = chartPath.find_last_of("\\/");
            if (slash != std::string::npos) chartDir = chartPath.substr(0, slash);

            MilWindow backendWin {};
            backendWin.init(true);
            backendWin.base.window->setHidden(true);
            backendWin.base.window->setSwapInterval(0);
            backendWin.base.surfaceSize = { (uint64)W, (uint64)H };

            auto info = backendWin.loadChart(chartPath, chartDir);
            std::fprintf(stderr, "[dbg dumpmil] loadChart ok: %d ms, lines=%zu\n",
                (int)(info.createObjectTook * 1000), backendWin.renderer->chart.lines.size());

            std::vector<double> ts = times.empty() ? std::vector<double> { 0.0, 0.5, 2.0 } : times;
            for (double t : ts) dumpMilObjects(backendWin, t);

            std::fprintf(stderr, "[dbg dumpmil] DONE\n");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[dbg dumpmil] EXCEPTION: %s\n", e.what());
        }
    }

    // [dbg] CLI 截图调试：mil-recorder.exe --screenshot <chart_path> <W> <H> <out.bmp>
    // 关键：走与"渲染视频"完全相同的链路（VideoRecorder MSAA FBO + mainloopFrame isRenderingVideo），
    // 且宽高由参数自定义 —— 复现自定义比例导出时的白块问题
    void runMilScreenshot(const std::string& chartPath, int W, int H, const std::string& outPath, double endT = 1.983) {
        std::fprintf(stderr, "[dbg screenshot] chart='%s' size=%dx%d out='%s' endT=%.3f\n", chartPath.c_str(), W, H, outPath.c_str(), endT);
        try {
            std::string chartDir;
            auto slash = chartPath.find_last_of("\\/");
            if (slash != std::string::npos) chartDir = chartPath.substr(0, slash);

            std::string imagePath;
            std::string effChart = chartPath;
            {
                std::string lower = effChart;
                for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
                if (lower.size() >= 4 && lower.rfind(".zip") == lower.size() - 4) {
                    auto r = resolveMilZip(effChart);
                    effChart = r.chart; chartDir = r.dir; imagePath = r.image;
                }
            }

            MilWindow backendWin {};
            std::fprintf(stderr, "[dbg screenshot] constructed\n");
            backendWin.init(true);
            std::fprintf(stderr, "[dbg screenshot] init done\n");
            backendWin.base.window->setHidden(true);
            backendWin.base.window->setSwapInterval(0);
            std::fprintf(stderr, "[dbg screenshot] window hidden, swapInterval=0\n");

            backendWin.base.surfaceSize = { (uint64)W, (uint64)H };

            auto info = backendWin.loadChart(effChart, chartDir);
            if (imagePath.empty()) {
                auto metaData = Data::MakeFromFileOptional(chartDir.empty() ? "meta.json" : chartDir + "/meta.json");
                if (metaData) {
                    try {
                        auto metaRoot = JsonNode::Parse(*metaData);
                        if (metaRoot.isObject() && metaRoot.hasKey("image_file") && metaRoot["image_file"].isString()) {
                            imagePath = metaRoot["image_file"].getString();
                            if (!chartDir.empty() && imagePath.find('/') == std::string::npos && imagePath.find('\\') == std::string::npos)
                                imagePath = chartDir + "/" + imagePath;
                        }
                    } catch (...) {}
                }
            }
            if (!imagePath.empty()) {
                try { backendWin.renderer->loadIllustion(imagePath); }
                catch (const std::exception& e) { std::fprintf(stderr, "[dbg screenshot] jacket decode failed (%s), fallback to mask\n", e.what()); }
                catch (...) { std::fprintf(stderr, "[dbg screenshot] jacket decode failed, fallback to mask\n"); }
            }
            std::fprintf(stderr, "[dbg screenshot] loadChart ok: %d ms\n", (int)(info.createObjectTook * 1000));

            using FrameType = std::optional<uint64>;
            gthread_safe_queue::ThreadSafeQueue<FrameType> frameQueue;
            auto videoRecorder = VideoRecorder::Make(
                backendWin.base.glCtx,
                (uint64)W, (uint64)H,
                [&](uint64 slotIndex) { frameQueue.enqueue(slotIndex); },
                { .callbackIsThreadSafe = true }
            );

            // 渲染 endT 时刻的画面。Milthm 每帧画面是 time 的纯函数(mainloopFrame 直接按
            // .time 计算全帧,不依赖前序帧累积,见 dumpMilObjects 可直接跳任意 t 验证),
            // 因此不必从 0 逐帧步进到 endT —— 旧实现 endT*60+1 帧会令 MSAA 读回缓冲/
            // YUV slot 无限累积,长时(如 ~17s,1000+帧)内存暴涨 + allocYUVFrameSlot 卡死。
            // 改为只渲小常数帧(全部贴近 endT),读回缓冲恒定有界。
            static constexpr uint64 kScreenshotWarmupFrames = 3;
            for (uint64 f = 0; f < kScreenshotWarmupFrames; f++) {
                auto reGuard = videoRecorder->useFrame();
                backendWin.mainloopFrame({
                    .base = {
                        .time = endT,
                        .isRenderingVideo = true
                    }
                });
            }

            videoRecorder->finish();
            frameQueue.enqueue(std::nullopt);

            // 收集全部就绪 slot，取最后一帧
            std::vector<uint64> slots;
            while (true) {
                FrameType fr;
                frameQueue.wait_dequeue(fr);
                if (!fr.has_value()) break;
                slots.push_back(fr.value());
            }
            if (slots.empty()) { std::fprintf(stderr, "[dbg screenshot] no frames read back\n"); return; }
            uint64 lastSlot = slots.back();
            std::fprintf(stderr, "[dbg screenshot] %zu frames readback, dumping slot %llu\n", slots.size(), (unsigned long long)lastSlot);

            auto* yuv = videoRecorder->referYUVFrame(lastSlot);
            const uint8* yP = yuv->dataPtrs()[0];
            const uint8* uP = yuv->dataPtrs()[1];
            const uint8* vP = yuv->dataPtrs()[2];

            // YUV420(BT.601 limited) → RGBA
            std::vector<uint8_t> rgba((size_t)W * H * 4);
            for (int yy = 0; yy < H; yy++) {
                for (int xx = 0; xx < W; xx++) {
                    double Y = yP[(size_t)yy * W + xx];
                    double U = uP[(size_t)(yy / 2) * (W / 2) + (xx / 2)];
                    double V = vP[(size_t)(yy / 2) * (W / 2) + (xx / 2)];
                    double r = 1.164 * (Y - 16) + 1.596 * (V - 128);
                    double g = 1.164 * (Y - 16) - 0.391 * (U - 128) - 0.813 * (V - 128);
                    double b = 1.164 * (Y - 16) + 2.018 * (U - 128);
                    auto cl = [](double v) { return (uint8_t)std::clamp(v, 0.0, 255.0); };
                    size_t idx = ((size_t)yy * W + xx) * 4;
                    rgba[idx] = cl(r); rgba[idx + 1] = cl(g); rgba[idx + 2] = cl(b); rgba[idx + 3] = 255;
                }
            }
            BMPWriter::writeBMP(outPath, W, H, rgba.data());
            videoRecorder->returnYUVFrame(lastSlot);
            std::fprintf(stderr, "[dbg screenshot] DONE\n");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[dbg screenshot] EXCEPTION: %s\n", e.what());
        }
    }


    void entrypoint() {
        int argc; char** argv;
        grain::get_args(&argc, &argv);
        
        std::vector<std::string> args(argv, argv + argc);
        auto hasArg = [&](const std::string& arg) {
            return std::find(args.begin(), args.end(), arg) != args.end();
        };

        MilWindow backendWin {};
        backendWin.init(true);
        backendWin.base.window->setHidden(true);

        std::string chartRoot = "", chartPath = "", imagePath = "", audioPath = "";

        int32 chartFileInput, chartRootInput, bgFileInput, audioFileInput;
        int32 musicVolInput, sfxVolInput;
        int32 noteScalingInput;
        int32 recordWidthInput, recordHeightInput, recordFPSInput;
        int32 recordSfxRandshakeCheckBox;
        int32 introEnabledCheckBox;

        Settings settings {};
        settings.fromRegistry();

        // [Intro] 应用持久化开场动画开关; --no-intro 命令行先行置 false, 此处只清除不恢复
        geasy_phi::MilTakeOverer::introEnabled = settings.introEnabled && geasy_phi::MilTakeOverer::introEnabled;

        auto win = Win32Window::Make({
            .title = L"Mil Recorder"
        });

        win->registerWidget(Widgets::Button({ .text = L"↗Github", .onClick = [&]() {
            ShellExecute(nullptr, "open", "https://github.com/qaqFei/easy-phi", nullptr, nullptr, SW_SHOWNORMAL);
        } }));
        win->registerWidget(Widgets::Label({ .text = L"谱面选择" }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"谱面根目录: " }));
        chartRootInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            chartRoot = Win32Utils::wstringToString(ws);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"浏览", .onClick = [&]() {
            auto folderW = selectFolder(win.get(), L"打开谱面根目录");
            auto folder = Win32Utils::wstringToString(folderW);
            if (folder.empty()) return;
            WidgetStatics::TextInput::setText(win->refWidget(chartRootInput), Win32Utils::stringToWstring(folder));
        } }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"谱面文件: " }));
        chartFileInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            chartPath = Win32Utils::wstringToString(ws);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"浏览", .onClick = [&]() {
            auto pathW = selectOpenFile(win.get(),  L"Chart File (*.json)\0*.json\0All Files (*.*)\0*.*\0", L"打开谱面文件");
            auto path = Win32Utils::wstringToString(pathW);
            if (path.empty()) return;
            WidgetStatics::TextInput::setText(win->refWidget(chartFileInput), Win32Utils::stringToWstring(path));
        } }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"曲绘文件: " }));
        bgFileInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            imagePath = Win32Utils::wstringToString(ws);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"浏览", .onClick = [&]() {
            auto pathW = selectOpenFile(win.get(),  L"Image File (*.png, *.jpg, *.jpeg)\0*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0", L"打开曲绘文件");
            auto path = Win32Utils::wstringToString(pathW);
            if (path.empty()) return;
            WidgetStatics::TextInput::setText(win->refWidget(bgFileInput), Win32Utils::stringToWstring(path));
        } }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"音频文件: " }));
        audioFileInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            audioPath = Win32Utils::wstringToString(ws);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"浏览", .onClick = [&]() {
            auto pathW = selectOpenFile(win.get(),  L"Audio File (*.mp3, *.wav, *.ogg, *.flac, *.m4a, *.aac)\0*.mp3;*.wav;*.ogg;*.flac;*.m4a;*.aac\0All Files (*.*)\0*.*\0", L"打开音频文件");
            auto path = Win32Utils::wstringToString(pathW);
            if (path.empty()) return;
            WidgetStatics::TextInput::setText(win->refWidget(audioFileInput), Win32Utils::stringToWstring(path));
        } }));
        win->nextRow();

        win->nextRow();

        bool isSyncingSettings = true;

        auto syncSettingsToUI = [&](bool allowEmpty = false) {
            isSyncingSettings = true;

            auto checkbox = [&](int id, bool checked) {
                WidgetStatics::CheckBox::toggle(win->refWidget(id), checked);
            };

            auto float64Input = [&](int id, float64 value) {
                auto& text = win->refWidget(id).store.textInput.text;

                try {
                    if ((allowEmpty && text.empty()) || std::stold(text) == value) {
                        return;
                    }
                } catch (...) { }

                WidgetStatics::TextInput::setText(win->refWidget(id), Win32Utils::stringToWstring(std::format("{:.10g}", value)));
            };

            auto intInput = [&](int id, int value) {
                auto& text = win->refWidget(id).store.textInput.text;

                try {
                    if ((allowEmpty && text.empty()) || std::stoi(text) == value) {
                        return;
                    }
                } catch (...) { }

                WidgetStatics::TextInput::setText(win->refWidget(id), std::to_wstring(value));
            };

            float64Input(musicVolInput, settings.musicVol);
            float64Input(sfxVolInput, settings.sfxVol);
            float64Input(noteScalingInput, settings.noteScaling);

            intInput(recordWidthInput, settings.recordWidth);
            intInput(recordHeightInput, settings.recordHeight);
            float64Input(recordFPSInput, settings.recordFPS);
            checkbox(recordSfxRandshakeCheckBox, settings.recordSfxRandshake);
            checkbox(introEnabledCheckBox, settings.introEnabled);

            isSyncingSettings = false;
        };

        auto settingsChanged = [&]() {
            settings.onChanged();
            syncSettingsToUI(true);
        };

        win->registerWidget(Widgets::Label({ .text = L"设置 (通用)" }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"音乐音量: " }));
        musicVolInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.musicVol = std::stold(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"音效音量: " }));
        sfxVolInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.sfxVol = std::stold(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"音符缩放: " }));
        noteScalingInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.noteScaling = std::stold(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"设置 (视频参数)" }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"宽度: " }));
        recordWidthInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.recordWidth = std::stoi(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"高度: " }));
        recordHeightInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.recordHeight = std::stoi(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        win->registerWidget(Widgets::Label({ .text = L"帧率: " }));
        recordFPSInput = win->registerWidget(Widgets::TextInput({ .text = L"", .onChange = [&](const std::wstring& ws) {
            if (isSyncingSettings) return;
            try { settings.recordFPS = std::stold(ws); }
            catch (...) { }
            settingsChanged();
        }, .onUnfocus = syncSettingsToUI }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        recordSfxRandshakeCheckBox = win->registerWidget(Widgets::CheckBox({ .text = L"打击音随机抖动", .onChange = [&](bool checked) {
            if (isSyncingSettings) return;
            settings.recordSfxRandshake = checked;
            settingsChanged();
        } }));
        win->registerWidget(Widgets::Button({ .text = L"?", .onClick = [&]() {
            showInfoMsg(win.get(), L"由于本家即使同时打击音符, 打击音效也并不是在同一时刻播放, 启用该选项后, 打击音效会在一定范围内随机延迟播放, 以模拟本家多押的神秘听感。");
        } }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();
        introEnabledCheckBox = win->registerWidget(Widgets::CheckBox({ .text = L"渲染开场动画", .onChange = [&](bool checked) {
            if (isSyncingSettings) return;
            settings.introEnabled = checked;
            geasy_phi::MilTakeOverer::introEnabled = checked;
            settingsChanged();
        } }));
        win->registerWidget(Widgets::Label({ .text = L"" }));
        win->nextRow();

        win->nextRow();

        auto load = [&]() {
            try {
                backendWin.loadChart(chartPath, chartRoot);
                backendWin.renderer->chart.options.noteScaling *= settings.noteScaling;
                backendWin.renderer->loadIllustion(imagePath);
                backendWin.renderer->audioManager.load(audioPath);
            } catch (const std::exception& e) {
                auto msg = Win32Utils::stringToWstring(e.what());
                showErrorMsg(win.get(), msg.c_str());
                return false;
            }

            return true;
        };

        struct WinHiddenGuard {
            Win32Window* win;
            WinHiddenGuard(Win32Window* win) : win(win) { win->setHidden(true); }
            ~WinHiddenGuard() { win->setHidden(false); }
        };

        win->registerWidget(Widgets::Button({ .text = L"预览", .onClick = [&]() {
            if (!load()) return;
            WinHiddenGuard whguard(win.get());

            backendWin.base.window->setHidden(false);
            backendWin.base.window->setSwapInterval(hasArg("--disable-vsync") ? 0 : 1);
            // [Intro] 起 BGM 前先播 2.05s 载入信息页(t<0)
            playMilIntroLead(backendWin);
            backendWin.renderer->audioManager.startBgm();

            backendWin.renderer->audioManager.setBgmVolume(settings.musicVol);
            backendWin.renderer->audioManager.setSfxVolume(settings.sfxVol);

            while (!backendWin.renderer->audioManager.getBpmIsEnded()) {
                if (!backendWin.mainloopFrame({})) {
                    backendWin.renderer->audioManager.stopBgm();
                    break;
                }
            }

            backendWin.base.window->setHidden(true);
        } }));
        win->registerWidget(Widgets::Button({ .text = L"渲染视频", .onClick = [&]() {
            auto videoPathW = selectSaveFile(win.get(),  L"视频文件 (*.mp4)\0*.mp4\0All Files (*.*)\0*.*\0", L"保存视频文件");
            auto videoPath = Win32Utils::wstringToString(videoPathW);
            if (videoPath.empty()) return;

            gwin_progress_dialog::ProgressDialog pd {};
            pd.setTitle(L"Mil Recorder -- 渲染视频");
            pd.start();

            pd.setLine(1, L"初始化...");
            WinHiddenGuard whguard(win.get());

            gvideoenc::VideoCap cap {};
            cap.init(videoPath, settings.recordWidth, settings.recordHeight, settings.recordFPS);
            
            using FrameType = std::optional<uint64>;
            gthread_safe_queue::ThreadSafeQueue<FrameType> frameQueue;

            auto videoRecorder = VideoRecorder::Make(
                backendWin.base.glCtx,
                settings.recordWidth, settings.recordHeight,
                [&](uint64 slotIndex) { frameQueue.enqueue(slotIndex); },
                {
                    .callbackIsThreadSafe = true
                }
            );

            auto frameWriter = [&]() {
                FrameType frame;

                while (true) {
                    frameQueue.wait_dequeue(frame);
                    if (!frame.has_value()) break;

                    auto* yuv = videoRecorder->referYUVFrame(frame.value());
                    cap.writeVideoFrame(yuv->dataPtrs().data(), yuv->rowBytes().data());
                    videoRecorder->returnYUVFrame(frame.value());
                }
            };

            backendWin.base.surfaceSize = { settings.recordWidth, settings.recordHeight };
            backendWin.base.window->setSwapInterval(0);

            pd.setLine(1, L"加载...");
            if (!load()) return;
            
            pd.setLine(1, L"渲染音频...");
            auto mixedAudio = backendWin.renderer->mixFinalBgm(backendWin.renderer->chart, {
                .musicVol = settings.musicVol,
                .sfxVol = settings.sfxVol,
                .sfxRandshake = settings.recordSfxRandshake
            });

            pd.setLine(1, L"写入音频...");
            // (原 cap.writeAudio(mixedAudio) 由下方带静音前导的版本替代)
            // [Intro] 视频从 -introLead 起渲染(载入信息页), 音频前插等长静音以保持音画同步
            const float64 introLead = MilTakeOverer::introEnabled ? geasy_phi::introLeadTotal : 0.0;   // --no-intro 时为 0: 无开场帧/无静音前插
            {
                auto lead = mixedAudio->copy();
                lead->channels = mixedAudio->channels;
                lead->sampleRate = mixedAudio->sampleRate;
                lead->data.assign((size_t)((float64)lead->sampleRate * introLead) * lead->channels, 0);
                lead->data.insert(lead->data.end(), mixedAudio->data.begin(), mixedAudio->data.end());
                cap.writeAudio(lead);
            }

            pd.setLine(1, L"渲染视频...");
            uint64 frameCut = 0;
            std::thread frameWriterThread(frameWriter);
            FramerateMeter fpsMeter {};
            
            while (true) {
                float64 t = frameCut / settings.recordFPS - introLead;   // t < 0 = 载入信息页
                if (t > backendWin.renderer->audioManager.getBgmLength()) break;

                auto reGuard = videoRecorder->useFrame();
                backendWin.mainloopFrame({
                    .base = {
                        .time = t,
                        .isRenderingVideo = true
                    }
                });

                frameCut++;
                fpsMeter.frame();

                uint64 totalFrames = (backendWin.renderer->audioManager.getBgmLength() + introLead) * settings.recordFPS;   // 含开场前导
                pd.setProgress(frameCut, totalFrames);

                {
                    std::wstring msg = L"渲染视频... ";
                    msg += std::to_wstring(frameCut) + L"/" + std::to_wstring(totalFrames) + L" (" + std::to_wstring((uint64)fpsMeter.get()) + L" fps)";
                    pd.setLine(1, msg.c_str());
                }
            }

            videoRecorder->finish();

            frameQueue.enqueue(std::nullopt);
            frameWriterThread.join();

            pd.setLine(1, L"释放资源...");
            std::wstring msg = L"渲染完成, 已保存到 " + Win32Utils::stringToWstring(videoPath);
            showInfoMsg(win.get(), msg.c_str());
        } }));

        win->createWidgets();
        win->doGrid();
        win->resizeToGridBounds();
        syncSettingsToUI();

        win->mainloop();
    }
#else
        #error "APP_TYPE is not defined"
        void entrypoint() {}
    #endif
}

void entrypoint() {
    int argc; char** argv;
    grain::get_args(&argc, &argv);
    // [CLI] Windows 的 argv 是本机 ANSI 代码页(中文 Win = GBK), 而 libstdc++ 的 std::filesystem
    // 把窄字符串按 UTF-8 解释 -> 含日文/中文歌名的路径会抛 "Cannot convert character sequence"
    // (Illegal byte sequence)。在入口统一转成 UTF-8, 下游 path(string)/string()/ifstream 全链路即可正常。
    // MinGW 13.1 实测: std::locale("") 会 throw(_S_create_c_locale name not valid), 不能靠切 locale。
    // 注意: 必须放在我们自己的 entrypoint() 里(而不是 grain 生成的 main), 否则 regen_source 会丢。
#ifdef _WIN32
    {
        static std::vector<std::string> argvUtf8;
        argvUtf8.reserve((size_t)(argc > 0 ? argc : 0));
        for (int ai = 0; ai < argc; ai++) {
            std::string s = argv[ai] ? argv[ai] : "";
            if (s.empty()) { argvUtf8.emplace_back(); continue; }
            int wn = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
            std::wstring ws((size_t)(wn > 0 ? wn : 0), L'\0');
            if (wn > 0) MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &ws[0], wn);
            int un = (wn > 0) ? WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr) : 0;
            std::string us((size_t)(un > 0 ? un : 0), '\0');
            if (un > 0) WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &us[0], un, nullptr, nullptr);
            argvUtf8.emplace_back(std::move(us));
        }
        for (int ai = 0; ai < argc; ai++) argv[ai] = const_cast<char*>(argvUtf8[(size_t)ai].c_str());
    }
#endif
    for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--no-intro") geasy_phi::MilTakeOverer::introEnabled = false;
    if (argc >= 6 && std::string(argv[1]) == "--screenshot") {
        double t = argc >= 7 ? std::atof(argv[6]) : 1.983;
        test_main::runMilScreenshot(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), argv[5], t);
        return;
    }
    if (argc >= 6 && std::string(argv[1]) == "--dumpmil") {
        std::vector<double> ts;
        for (int i = 5; i < argc; i++) ts.push_back(std::atof(argv[i]));
        test_main::runMilDump(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), ts);
        return;
    }
    // [CLI] 渲染视频: --record <chart_or_zip> <W> <H> <fps> <out.mp4> [audio]
    if (argc >= 7 && std::string(argv[1]) == "--record") {
        test_main::runMilRecord(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atof(argv[5]), argv[6], argc >= 8 ? argv[7] : "");
        return;
    }
    // [CLI] 谱面 zip 直接播放: mil-recorder.exe <xxx.zip>
    if (argc >= 2) {
        std::string a1 = argv[1];
        std::string lower = a1;
        for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
        if (lower.size() >= 4 && lower.rfind(".zip") == lower.size() - 4) {
            test_main::runMilZipPlay(a1);
            return;
        }
    }
    test_main::entrypoint();
}
