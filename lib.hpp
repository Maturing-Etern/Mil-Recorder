#include "mil_fx_emitter.hpp"

namespace geasy_phi {
    using gdecoded_rgba_texture::DecodedRGBATexture;
    using gsp::gsp;
    using gdecoded_audio::DecodedAudio;
    using gdata::Data;
    using gaudio_engine::AudioEngine;
    using HashBucket = ghash_bucket::Bucket;
    namespace GL = gopengl::GL;
    using namespace gnumeric::types;
    using namespace ggeometry;
    using namespace gcolor;
    using namespace gobject_index;
    using namespace gstrutils;
    using namespace gjson;
    using namespace geasing;
    using namespace gskvcache;
    using namespace gtime_based_anim;

    namespace SharedCalculatedObjects {
        struct CalculatedText {
            std::string text;
            Vec2 position, scale = { 1.0, 1.0 }, anchor;
            float64 fontSize, rotation;
            Color color = Color::White();
        };

        struct CalculatedRect {
            Vec2 position, size;
            float64 rotation;
            Color color;
        };

        struct CalculatedPoly {
            Vec2 p1, p2, p3, p4;
            Color color;

            static CalculatedPoly Make(
                const Vec2& point,
                const Vec2& size,
                const Color& color,
                const Transform2D& transform = Transform2D()
            ) noexcept {
                return {
                    .p1 = transform.transformPoint(point),
                    .p2 = transform.transformPoint(point + Vec2 { size.x, 0.0 }),
                    .p3 = transform.transformPoint(point + size),
                    .p4 = transform.transformPoint(point + Vec2 { 0.0, size.y }),
                    .color = color
                };
            }
        };

        struct CalculatedCapRoundedLineSet {
            struct Item {
                Vec2 p1, p2;
                Color c1, c2;
            };

            Item* items;
            uint64 count;
            float64 width;
        };

        #define UsingSharedCalculatedObjects \
            using CalculatedText = SharedCalculatedObjects::CalculatedText; \
            using CalculatedRect = SharedCalculatedObjects::CalculatedRect; \
            using CalculatedPoly = SharedCalculatedObjects::CalculatedPoly; \
            using CalculatedCapRoundedLineSet = SharedCalculatedObjects::CalculatedCapRoundedLineSet; \
            static_assert(true, "")
        
        #define ListSharedCalculatedObjects \
            CalculatedText, \
            CalculatedRect, \
            CalculatedPoly, \
            CalculatedCapRoundedLineSet

        template <typename T>
        bool sharedCulling(std::vector<T>& objects, const Rect& screenRect) noexcept {
            auto& obj = objects.back();

            if (std::holds_alternative<CalculatedText>(obj)) { }
            else if (std::holds_alternative<CalculatedRect>(obj)) {
                auto& rect = std::get<CalculatedRect>(obj);
                if (!quadStrictlyIntersectRect(makeQuadFromRectInfo({
                    .position = rect.position,
                    .size = rect.size,
                    .rotation = rect.rotation
                }).data(), screenRect)) objects.pop_back();
            } else if (std::holds_alternative<CalculatedPoly>(obj)) {
                auto& poly = std::get<CalculatedPoly>(obj);
                {
                    Vec2 _q[4] = { poly.p1, poly.p2, poly.p3, poly.p4 };
                    if (!quadStrictlyIntersectRect(_q, screenRect)) objects.pop_back();
                }
            } else if (std::holds_alternative<CalculatedCapRoundedLineSet>(obj)) { }
            else return false;

            return true;
        }
    };

    namespace TakeOvererComponents {
        struct SharedComp {
            using TextureDeocder = std::function<DecodedRGBATexture(const Data&)>;
            TextureDeocder textureDecoder;

            gsp<GL::TextureInfo> illustionTexture;

            void check() {
                gassert::asrt(!!textureDecoder, "SharedComp: textureDecoder is not set");
            }
        };

        struct AudioManager {
            using Decoder = std::function<gsp<DecodedAudio>(const Data&)>;;

            Decoder decoder;
            gsp<AudioEngine> engine;
            uint64 maxSfxPlaying = 16;

            gsp<DecodedAudio> bgmAudio;

            void check() {
                gassert::asrt(!!decoder, "AudioManager: decoder is not set");
                gassert::asrt(!!engine, "AudioManager: engine is not set");
            }

            gsp<DecodedAudio> decodeAndCheck(const Data& data) {
                auto decoded = decoder(data);
                if (!decoded) throw std::runtime_error("failed to decode audio");
                return decoded;
            }

            void load(const Data& data) {
                bgmAudio = decodeAndCheck(data);
            }

            void load(const std::string& path) { load(Data::MakeFromFile(path)); }

            void startBgm() {
                if (!bgmAudio) throw std::runtime_error("bgm is not loaded");
                stopBgm();
                bgmAudioTask = engine->createTask(bgmAudio);
            }

            float64 getBgmTime() const {
                return bgmAudioTask ? engine->getTaskTime(bgmAudioTask) : 0.0;
            }

            bool getBpmIsEnded() {
                return bgmAudioTask && engine->getTaskEnded(bgmAudioTask);
            }

            void stopBgm() {
                if (bgmAudioTask) {
                    bgmAudioTask->stopped = true;
                    bgmAudioTask.reset();
                }
            }

            void setBgmVolume(float64 vol) {
                bgmVolume = vol;

                if (bgmAudioTask) {
                    bgmAudioTask->volume = bgmVolume;
                }
            }

            void setSfxVolume(float64 vol) {
                sfxVolume = vol;
            }

            void seekBgm(float64 t) {
                if (bgmAudioTask) {
                    engine->seekTask(bgmAudioTask, t);
                }
            }

            float64 getBgmLength() {
                return bgmAudio ? bgmAudio->getLengthInSeconds() : 0.0;
            }

            void playSfx(const gsp<DecodedAudio>& audio, uint64 count) {
                if (!maxSfxPlaying) return;

                while (playingSfxs.size() >= maxSfxPlaying) {
                    auto& task = playingSfxs.front();
                    task->stopped = true;
                    playingSfxs.erase(playingSfxs.begin());
                }

                auto task = engine->createTask(audio);
                task->volume = sfxVolume * count;
            }

            private:
            gsp<AudioEngine::Task> bgmAudioTask;
            float64 bgmVolume = 1.0, sfxVolume = 1.0;
            std::vector<gsp<AudioEngine::Task>> playingSfxs;
        };

        struct LoadChartResultInfo {
            bool success = true;
            std::string error;

            float64 createObjectTook;
            float64 initTook;

            void checkAndThrow() const {
                if (success) return;
                throw std::runtime_error(error);
            }

            float64 totalTook() const {
                return createObjectTook + initTook;
            }
        };

        struct RenderConfigBase {
            std::optional<float64> time;
            bool flushGl = true;
            bool disableHitsound = false;

            float64 getTime(const AudioManager& audioManager) const noexcept {
                return time.value_or(audioManager.getBgmTime());
            }
        };

        struct RenderResultInfoBase {
            float64 calculatedTook;
            float64 glOperationsTook;
        };

        template <typename T>
        bool renderSharedObject(
            const T& obj,
            const gsp<GL::GL33Context>& glCtx,
            GL::GL33Canvas& cvs,
            GL::TextManager& textManager
        ) noexcept {
            using namespace SharedCalculatedObjects;
            using namespace GL;

            if (std::holds_alternative<CalculatedText>(obj)) {
                auto& text = std::get<CalculatedText>(obj);

                TextManager::DrawTextConfig config {
                    .text = text.text,
                    .fontSize = text.fontSize,
                    .pos = text.position,
                    .anchor = text.anchor,
                    .rotation = text.rotation,
                    .scale = text.scale,
                    .color = text.color
                };

                textManager.drawText(cvs, config);
            } else if (std::holds_alternative<CalculatedRect>(obj)) {
                auto& rect = std::get<CalculatedRect>(obj);

                cvs.save();
                cvs.translate(rect.position);
                cvs.rotateDegrees(rect.rotation);
                cvs.drawRect({
                    .position = -rect.size / 2,
                    .size = rect.size,
                    .color = rect.color
                });
                cvs.restore();
            } else if (std::holds_alternative<CalculatedPoly>(obj)) {
                auto& poly = std::get<CalculatedPoly>(obj);

                auto mesh = glCtx->requestMesh(6);
                *mesh.vnext() = { poly.p1 }; *mesh.vnext() = { poly.p2 }; *mesh.vnext() = { poly.p4 };
                *mesh.vnext() = { poly.p4 }; *mesh.vnext() = { poly.p3 }; *mesh.vnext() = { poly.p2 };
                mesh.color = poly.color;
                cvs.drawMesh(mesh);
            } else return false;

            return true;
        }
    };

    static const float64 INF_TIME = 99999.0;
    static const Vec2 INF_TZ = { -INF_TIME, INF_TIME };
    static const float64 INF_EV = 1e9;

    enum class EnumPhiEventType : uint64 {
        PositionX, PositionY,
        SelfRotation, AxisRotation,
        MultiplyAlpha, AdditiveAlpha,
        Color,
        ScaleX, ScaleY,
        Speed, SpeedCoefficient,
        Text,
        PhiShaderUniform,
        MAX = PhiShaderUniform + 1
    };

    enum class EnumPhiNoteType {
        Tap, Drag, Flick, Hold
    };

    enum class EnumPhiLineAttachUI {
        Pause, Bar,
        ComboNumber, Combo, Score,
        Name, Level,
        None
    };

    bool phiEventTypeIsMultiply(EnumPhiEventType type) noexcept {
        return (
            type == EnumPhiEventType::MultiplyAlpha ||
            type == EnumPhiEventType::ScaleX ||
            type == EnumPhiEventType::ScaleY ||
            type == EnumPhiEventType::SpeedCoefficient
        );
    }

    struct PhiNoteTypeHelper {
        static EnumPhiNoteType FromOfficial(uint64 n) {
            if (n == 1) return EnumPhiNoteType::Tap;
            if (n == 2) return EnumPhiNoteType::Drag;
            if (n == 3) return EnumPhiNoteType::Hold;
            if (n == 4) return EnumPhiNoteType::Flick;
            return EnumPhiNoteType::Tap;
        }

        static EnumPhiNoteType FromRPE(uint64 n) {
            if (n == 1) return EnumPhiNoteType::Tap;
            if (n == 2) return EnumPhiNoteType::Hold;
            if (n == 3) return EnumPhiNoteType::Flick;
            if (n == 4) return EnumPhiNoteType::Drag;
            return EnumPhiNoteType::Tap;
        }

        static EnumPhiNoteType FromPEC(const std::string& s) {
            if (s == "n1") return EnumPhiNoteType::Tap;
            if (s == "n2") return EnumPhiNoteType::Hold;
            if (s == "n3") return EnumPhiNoteType::Flick;
            if (s == "n4") return EnumPhiNoteType::Drag;
            return EnumPhiNoteType::Tap;
        } 
    };

    struct PhiLineAttachUIHelper {
        static EnumPhiLineAttachUI FromString(const std::string& s) {
            if (s == "pause") return EnumPhiLineAttachUI::Pause;
            if (s == "bar") return EnumPhiLineAttachUI::Bar;
            if (s == "combo") return EnumPhiLineAttachUI::Combo;
            if (s == "combonumber") return EnumPhiLineAttachUI::ComboNumber;
            if (s == "score") return EnumPhiLineAttachUI::Score;
            if (s == "name") return EnumPhiLineAttachUI::Name;
            if (s == "level") return EnumPhiLineAttachUI::Level;
            return EnumPhiLineAttachUI::None;
        }
    };

    struct PhiMeta {
        float64 offset;
        std::string title;
        std::string composer;
        std::string artist;
        std::string charter;
        std::string difficulty;

        uint64 rpeVersion = 0;

        bool isHoldCoverAtHead;
        bool isZeroLengthHoldHidden;
        bool isHighNoteHidden;
        bool isRegLineAlphaNoteHidden;
        Vec2 lineWidthUnit, lineHeightUnit;

        float64 coverEllipsis = 1e-5;
        float64 maxViewRatio = (float64)16 / 9;
        Vec2 worldOrigin, worldViewport;
        float64 speedUnit = 1.0;
    };

    struct PhiBPMEvent {
        float64 time;
        float64 bpm;

        static void SortBpmEvents(std::vector<PhiBPMEvent>& events) {
            std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
                return a.time < b.time;
            });
        }
    };

    struct PhiEventLayerIndexs {
        static constexpr uint64 RPE_MAX = 5;
        static constexpr uint64 UNIT = 1000000;

        static constexpr uint64 NOTE_ATTRS = UNIT * 1;
        static constexpr uint64 NOTE_ATTRS_2 = UNIT * 2;
        static constexpr uint64 LINE_DEFAULT = UNIT * 3;
        static constexpr uint64 SHADER_UNIFORM_DEFAULT = UNIT * 4;
    };

    struct PhiEvent {
        Vec2 timeZone;
        Vec2 valueZone;
        EnumPhiEventType type;

        float64 (* easingFunc)(void*, float64);
        float64 (* easingIntFunc)(void*, float64);
        void* easingFuncContext;
        Vec2 easingZone = { 0.0, 1.0 };

        uint64 layerIndex;

        float64 cumulativeValueAtStart;

        float64 getProgressAtTime(float64 t) noexcept {
            // if (t < timeZone.x) return 0.0;
            return std::clamp((t - timeZone.x) / (timeZone.y - timeZone.x), 0.0, 1.0);
        }

        float64 valueAtTime(float64 t) noexcept {
            auto p = getProgressAtTime(t);
            
            if (hasValueEasing()) {
                if (easingZone == Vec2 { 0.0, 1.0 }) {
                    p = easingFunc(easingFuncContext, p);
                } else if (easingZone.x < easingZone.y) {
                    float64 s = easingFunc(easingFuncContext, easingZone.x);
                    float64 e = easingFunc(easingFuncContext, easingZone.y);

                    if (e != s) {
                        p = (easingZone.y - easingZone.x) * p + easingZone.x;
                        p = (easingFunc(easingFuncContext, p) - s) / (e - s);
                    }
                }
            }

            return valueZone.x + p * (valueZone.y - valueZone.x);
        }

        static float64 getDefaultValue(EnumPhiEventType type) noexcept {
            return phiEventTypeIsMultiply(type) ? 1.0 : 0.0;
        }

        float64 getIntegralValue(float64 t) noexcept {
            auto p = getProgressAtTime(t);
            float64 iv = p * p / 2.0;

            if (hasAllEasing()) {
                if (easingZone == Vec2 { 0.0, 1.0 } ) {
                    iv = easingIntFunc(easingFuncContext, p);
                } else if (easingZone.x < easingZone.y) {
                    float64 is = easingIntFunc(easingFuncContext, easingZone.x);
                    float64 tp = (easingZone.y - easingZone.x) * p + easingZone.x;
                    float64 tiv = easingIntFunc(easingFuncContext, tp);
                    float64 s = easingFunc(easingFuncContext, easingZone.x);
                    iv = (tiv - is - s * (tp - easingZone.x)) / (1.0 - s) / (1.0 - (easingZone.y - easingZone.x));
                }
            }

            float64 res = (timeZone.y - timeZone.x) * (valueZone.x * p + (valueZone.y - valueZone.x) * iv);
            if (t > timeZone.y) res += valueZone.y * (t - timeZone.y);
            if (t < timeZone.x) res -= valueZone.x * (timeZone.x - t);
            return res;
        }

        private:
        bool hasValueEasing() const noexcept { return easingFunc != nullptr; }
        bool hasAllEasing() const noexcept { return easingFunc != nullptr && easingIntFunc != nullptr; }
    };

    struct PhiAnimLayer {
        std::vector<PhiEvent> events[(uint64)EnumPhiEventType::MAX];

        void addEvent(const PhiEvent& e) { events[(uint64)e.type].push_back(e); }

        void init() {
            std::ranges::fill(lastUpdatedTimes, -std::numeric_limits<float64>::infinity());

            for (auto& typedEvents : events) {
                std::stable_sort(typedEvents.begin(), typedEvents.end(), [](const auto& a, const auto& b) {
                    return a.timeZone.x < b.timeZone.x;
                });
            }

            initSpeedCumul();
        }

        void updateType(uint64 type, float64 t) noexcept {
            auto& typedEvents = events[type];
            if (typedEvents.empty()) return;

            if (lastUpdatedTimes[type] == t) return;
            if (lastUpdatedTimes[type] > t) rewindTo(type, t);

            while (shouldAdvanceToNext(type, t)) currentIndexs[type]++;

            auto& e = typedEvents[currentIndexs[type]];

            if (type == (uint64)EnumPhiEventType::Speed) {
                currentValues[type] = e.cumulativeValueAtStart + e.getIntegralValue(t);
            } else {
                currentValues[type] = e.valueAtTime(t);
            }

            currentValueZones[type] = e.valueZone;
            lastUpdatedTimes[type] = t;
        }

        void updateType(EnumPhiEventType type, float64 t) noexcept {
            updateType((uint64)type, t);
        }

        void update(float64 t) noexcept {
            for (uint64 type = 0; type < (uint64)EnumPhiEventType::MAX; type++) {
                updateType(type, t);
            }
        }

        float64 get(EnumPhiEventType type) noexcept {
            if (events[(uint64)type].empty()) return PhiEvent::getDefaultValue(type);
            return currentValues[(uint64)type];
        }

        std::optional<float64> getAlwaysValue(EnumPhiEventType type) noexcept {
            auto& typedEvents = events[(uint64)type];
            if (typedEvents.empty()) return PhiEvent::getDefaultValue(type);

            if (type == EnumPhiEventType::Speed) {
                if (typedEvents.size() == 1 && typedEvents[0].valueZone.isZeroZone() && typedEvents[0].timeZone.x <= -INF_TIME / 2) {
                    return typedEvents[0].valueZone.x;
                }

                return std::nullopt;
            }

            float64 fixedValue = typedEvents[0].valueZone.x;
            for (auto& e : typedEvents) {
                if (!e.valueZone.isZeroZone() || fixedValue != e.valueZone.x) {
                    return std::nullopt;
                }
            }

            return fixedValue;
        }

        std::optional<Vec2> get_zone(EnumPhiEventType type) noexcept {
            return currentValueZones[(uint64)type];
        }

        private:
        float64 lastUpdatedTimes[(uint64)EnumPhiEventType::MAX];
        uint64 currentIndexs[(uint64)EnumPhiEventType::MAX];
        float64 currentValues[(uint64)EnumPhiEventType::MAX];
        std::optional<Vec2> currentValueZones[(uint64)EnumPhiEventType::MAX];

        void initSpeedCumul() {
            auto& speedEvents = events[(uint64)EnumPhiEventType::Speed];
            if (speedEvents.empty()) return;

            float64 cumulativeValue = 0.0;

            for (uint64 i = 0; i < speedEvents.size(); i++) {
                auto& e = speedEvents[i];
                e.cumulativeValueAtStart = cumulativeValue;

                if (i < speedEvents.size() - 1) {
                    cumulativeValue += e.getIntegralValue(speedEvents[i + 1].timeZone.x);
                }
            }
        }

        bool shouldAdvanceToNext(uint64 type, float64 t) const noexcept {
            return (
                currentIndexs[type] < events[type].size() - 1
                && events[type][currentIndexs[type] + 1].timeZone.x <= t
            );
        }

        void rewindTo(uint64 type, float64 t) noexcept {
            while (!shouldAdvanceToNext(type, t) && currentIndexs[type] > 0) {
                currentIndexs[type]--;
            }
        }
    };

    struct PhiAnimGroup {
        std::unordered_map<uint64, uint64> layerIndexMap;
        std::vector<PhiAnimLayer> layers;

        void addEvent(const PhiEvent& e) {
            if (!layerIndexMap.contains(e.layerIndex)) {
                layerIndexMap[e.layerIndex] = layers.size();
                layers.push_back({});
            }

            layers[layerIndexMap[e.layerIndex]].addEvent(e);
        }

        void init() {
            for (auto& layer : layers) {
                layer.init();
            }
        }

        void updateType(EnumPhiEventType type, float64 t) noexcept {
            for (auto& layer : layers) {
                layer.updateType(type, t);
            }
        }

        void update(float64 t) noexcept {
            for (auto& layer : layers) {
                layer.update(t);
            }
        }

        float64 get_based(EnumPhiEventType type, float64 baseValue) noexcept {
            float64 value = baseValue;

            for (auto& layer : layers) {
                if (phiEventTypeIsMultiply(type)) value *= layer.get(type);
                else value += layer.get(type);
            }

            return value;
        }

        Vec2 get_zone(EnumPhiEventType type) noexcept {
            for (auto& layer : layers) {
                auto z = layer.get_zone(type);
                if (z.has_value()) return z.value();
            }

            return {};
        }

        std::optional<float64> getAlwaysHashValue(EnumPhiEventType type) {
            float64 result = PhiEvent::getDefaultValue(type);

            for (auto& layer : layers) {
                auto v = layer.getAlwaysValue(type);
                if (!v.has_value()) return std::nullopt;
                if (phiEventTypeIsMultiply(type)) result *= v.value();
                else result += v.value();
            }

            return result;
        }
    };

    struct PhiAnimator {
        std::unordered_map<uint64, PhiAnimGroup> groups;

        PhiAnimGroup& requestGroup(uint64 index) {
            return groups.try_emplace(index, PhiAnimGroup {}).first->second;
        }

        template <typename T>
        PhiAnimGroup& requestGroup(T& obj) {
            return requestGroup(obj.indexer.get());
        }

        template <typename T>
        void addEvent(T& obj, const PhiEvent& e) {
            requestGroup(obj).addEvent(e);
        }

        void init() {
            for (auto& [_, group] : groups) {
                group.init();
            }
        }

        float64 get_based(uint64 index, float64 t, EnumPhiEventType type, float64 baseValue) noexcept {
            auto group_it = groups.find(index);
            if (group_it == groups.end()) return baseValue;

            auto& group = group_it->second;
            group.updateType(type, t);
            return group.get_based(type, baseValue);
        }

        template <typename T>
        float64 get_based(T& obj, float64 t, EnumPhiEventType type, float64 baseValue) noexcept {
            return get_based(obj.indexer.get(), t, type, baseValue);
        }

        float64 get(uint64 index, float64 t, EnumPhiEventType type) noexcept {
            return get_based(index, t, type, PhiEvent::getDefaultValue(type));
        }

        template <typename T>
        float64 get(T& obj, float64 t, EnumPhiEventType type) noexcept {
            return get(obj.indexer.get(), t, type);
        }

        Vec2 get_zone(uint64 index, float64 t, EnumPhiEventType type) noexcept {
            auto group_it = groups.find(index);
            if (group_it == groups.end()) return {};

            auto& group = group_it->second;
            group.updateType(type, t);
            return group.get_zone(type);
        }

        template <typename T>
        Vec2 get_zone(T& obj, float64 t, EnumPhiEventType type) noexcept {
            return get_zone(obj.indexer.get(), t, type);
        }

        template <typename T>
        std::optional<uint64> get_note_group_hash(T& note) {
            HashBucket hash;

            auto group_it = groups.find(note.indexer.get());

            for (const auto type : {
                EnumPhiEventType::PositionY,
                EnumPhiEventType::SelfRotation,
                EnumPhiEventType::AxisRotation,
                EnumPhiEventType::ScaleY,
                EnumPhiEventType::Speed,
                EnumPhiEventType::SpeedCoefficient
            }) {
                if (group_it == groups.end()) {
                    hash.submitNumber(PhiEvent::getDefaultValue(type));
                } else {
                    auto v = group_it->second.getAlwaysHashValue(type);
                    if (!v.has_value()) return std::nullopt;
                    hash.submitNumber(v.value());
                }
            }

            return hash.hash;
        }

        float64 get_alpha(uint64 index, float64 t, float64 additionalDefault) noexcept {
            return get(index, t, EnumPhiEventType::MultiplyAlpha) * get_based(index, t, EnumPhiEventType::AdditiveAlpha, additionalDefault);
        }

        template <typename T>
        float64 get_alpha(T& obj, float64 t, float64 additionalDefault) noexcept {
            return get_alpha(obj.indexer.get(), t, additionalDefault);
        }
    };

    struct PhiNote {
        ObjectIndexer indexer;

        struct State {
            float64 lastUpdateTime;
            bool playedHitsound;

            void timeUpdated(const PhiNote& note, float64 t) noexcept {
                if (lastUpdateTime > t) {
                    playedHitsound = note.time < t;
                }

                lastUpdateTime = t;
            }

            bool onPlayHitsound() noexcept {
                if (!playedHitsound) {
                    playedHitsound = true;
                    return true;
                }

                return false;
            }
        };

        EnumPhiNoteType type;
        float64 time, holdTime;
        bool isFake;

        uint64 lineIndex;
        Vec2 floorPosition;
        std::optional<float64> fixedHoldSpeed;
        bool isSimul;
        bool isReversedCover;

        State state;

        void init(PhiAnimator& animator) {
            floorPosition = { getFloorPositionAt(time, animator), getFloorPositionAt(time + holdTime, animator) };
        }

        float64 getFloorPositionAt(float64 t, PhiAnimator& animator) noexcept {
            if (t > time && fixedHoldSpeed.has_value()) {
                return getFloorPositionAt(time, animator) + (t - time) * fixedHoldSpeed.value();
            }

            return animator.get(lineIndex, t, EnumPhiEventType::Speed) + animator.get(*this, t, EnumPhiEventType::Speed);
        }

        bool isHold() noexcept {
            return holdTime > 0.0 || type == EnumPhiNoteType::Hold;
        }

        void reverseCover() {
            isReversedCover = !isReversedCover;
        }
    };

    struct PhiNoteGroup {
        struct State {
            float64 lastUpdateTime;
            uint64 firstNoteIndex;

            void timeUpdated(float64 t) noexcept {
                if (lastUpdateTime > t) {
                    firstNoteIndex = 0;
                }

                lastUpdateTime = t;
            }

            void passedNoteIndex(uint64 index) noexcept {
                if (firstNoteIndex == index) {
                    firstNoteIndex++;
                }
            }
        };
        
        std::vector<uint64> indexs;
        bool breakable = true;

        State state;
    };

    struct PhiLine {
        ObjectIndexer indexer;

        std::vector<PhiBPMEvent> bpms;
        std::vector<PhiNote> notes;

        std::optional<uint64> fatherLineIndex;
        float64 zOrder;
        bool enableCover;
        Vec2 anchor = { 0.5, 0.5 };

        std::optional<std::string> textureName;
        std::optional<EnumPhiLineAttachUI> attachUI;

        std::vector<PhiNoteGroup> noteGroups;

        void init(PhiAnimator& animator) {
            std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) {
                return a.time < b.time;
            });

            noteGroups.emplace_back().breakable = false;
            std::unordered_map<uint64, uint64> noteGroupMap;

            for (uint64 i = 0; i < notes.size(); i++) {
                auto& note = notes[i];
                note.lineIndex = indexer.get();
                note.init(animator);

                auto hash = animator.get_note_group_hash(note);
                if (hash.has_value()) {
                    if (!noteGroupMap.contains(hash.value())) {
                        noteGroups.emplace_back();
                        noteGroupMap[hash.value()] = noteGroups.size() - 1;
                    }

                    auto& group = noteGroups[noteGroupMap[hash.value()]];
                    group.indexs.push_back(i);
                } else noteGroups[0].indexs.push_back(i);
            }
        }

        float64 beat2sec(float64 beat) const {
            if (bpms.size() == 1) return beat * (60.0 / bpms[0].bpm);

            float64 t = 0.0;

            for (uint64 i = 0; i < bpms.size(); i++) {
                auto& e = bpms[i];
                float64 spb = 60.0 / e.bpm;

                if (i != bpms.size() - 1) {
                    float64 et_beat = bpms[i + 1].time - e.time;

                    if (beat >= et_beat) {
                        t += et_beat * spb;
                        beat -= et_beat;
                    } else {
                        t += beat * spb;
                        break;
                    }
                } else {
                    t += beat * spb;
                }
            }

            return t;
        }

        float64 sec2beat(float64 t) const {
            if (bpms.size() == 1) return t / (60.0 / bpms[0].bpm);

            float64 beat = 0.0;

            for (uint64 i = 0; i < bpms.size(); i++) {
                auto& e = bpms[i];
                float64 spb = 60.0 / e.bpm;

                if (i != bpms.size() - 1) {
                    float64 et_beat = bpms[i + 1].time - e.time;
                    float64 et_sec = et_beat * spb;

                    if (t >= et_sec) {
                        beat += et_beat;
                        t -= et_sec;
                    } else {
                        beat += t / spb;
                        break;
                    }
                } else {
                    beat += t / spb;
                }
            }

            return beat;
        }

        float64 getBpmAtSecond(float64 t) const noexcept {
            if (bpms.size() == 1) return bpms[0].bpm;

            for (uint64 i = 0; i < bpms.size(); i++) {
                auto& e = bpms[i];

                if (i != bpms.size() - 1) {
                    float64 et_beat = bpms[i + 1].time - e.time;
                    float64 et_sec = et_beat * (60.0 / e.bpm);

                    if (t >= et_sec) {
                        t -= et_sec;
                    } else {
                        return e.bpm;
                    }
                } else {
                    return e.bpm;
                }
            }

            return 120.0;
        }
    };

    struct PhiExtraEffectItem {
        Vec2 timeZone;
        std::optional<uint64> targetLine;
        uint64 order;
        bool isGlobal;
        std::string shaderName;
        uint64 shaderId;
        std::unordered_map<std::string, PhiAnimLayer> uniforms;
    };

    struct PhiExtra {
        std::vector<PhiExtraEffectItem> effects;
        std::vector<uint64> zOrderSortedEffects;

        void init() {
            initZOrderSortedEffects();
        }

        private:
        void initZOrderSortedEffects() {
            zOrderSortedEffects.clear();
            for (uint64 i = 0; i < effects.size(); i++) zOrderSortedEffects.push_back(i);

            std::stable_sort(zOrderSortedEffects.begin(), zOrderSortedEffects.end(), [&](uint64 a, uint64 b){
                return effects[a].order < effects[b].order;
            });
        }
    };

    struct PhiShaderUniform {
        uint8 used;
        float64 value[4];

        PhiShaderUniform(float64 v0, float64 v1, float64 v2, float64 v3) : used(4), value{ v0, v1, v2, v3 } {}
        PhiShaderUniform(float64 v0, float64 v1, float64 v2) : used(3), value{ v0, v1, v2, 0.0 } {}
        PhiShaderUniform(float64 v0, float64 v1) : used(2), value{ v0, v1, 0.0, 0.0 } {}
        PhiShaderUniform(float64 v0) : used(1), value{ v0, 0.0, 0.0, 0.0 } {}
        PhiShaderUniform() : used(0) {}

        PhiShaderUniform(const std::vector<float64>& v) : used(v.size()), value{} {
            for (uint8 i = 0; i < v.size(); i++) value[i] = v[i];
        }

        static PhiShaderUniform Interpolate(const PhiShaderUniform& a, const PhiShaderUniform& b, float64 t) noexcept {
            PhiShaderUniform result;
            result.used = std::max(a.used, b.used);
            for (uint8 i = 0; i < 4; i++) result.value[i] = a.value[i] + (b.value[i] - a.value[i]) * t;
            return result;
        }

        bool operator==(const PhiShaderUniform& other) const {
            if (used != other.used) return false;
            for (uint8 i = 0; i < 4; i++) if (value[i] != other.value[i]) return false;
            return true;
        }

        bool operator!=(const PhiShaderUniform& other) const { return !(*this == other); }

        void setToGlLocation(GL::ProgramInfo::Location loc) const noexcept {
            if (used == 1) loc.setf(value[0]);
            else if (used == 2) loc.setf(value[0], value[1]);
            else if (used == 3) loc.setf(value[0], value[1], value[2]);
            else if (used == 4) loc.setf(value[0], value[1], value[2], value[3]);
        }
    };

    struct PhiStoryboardAssets {
        // 用于区分是否到达了第一个
        static constexpr float64 kTextIndexOffset = 1;
        static constexpr float64 kColorIndexOffset = 1;
        static constexpr float64 kShaderUniformIndexOffset = 1;

        std::vector<std::string> texts;
        std::unordered_map<std::string, std::pair<uint64, Vec2>> textures; // name, (id, size)
        std::vector<Color> colors;
        std::vector<PhiShaderUniform> shaderUniforms;

        std::unordered_map<uint64, std::string> shaderNameMap;

        std::function<std::optional<std::pair<uint64, Vec2>>(std::string)> textureLoader;
        std::function<void(uint64)> textureDestroyer;
        std::function<void(std::string, uint64)> shaderPreloader;

        Vec2 requestTextPair(const std::string& start, const std::string& end) {
            Vec2 valueZone;
            if (texts.empty() || texts[texts.size() - 1] != start) texts.push_back(start);
            valueZone.x = texts.size() - 1;
            if (texts.empty() || texts[texts.size() - 1] != end) texts.push_back(end);
            valueZone.y = texts.size() - 1;
            return valueZone + kTextIndexOffset;
        }

        Vec2 requestColorPair(const Color& start, const Color& end) {
            Vec2 valueZone;
            if (colors.empty() || colors[colors.size() - 1] != start) colors.push_back(start);
            valueZone.x = colors.size() - 1;
            if (colors.empty() || colors[colors.size() - 1] != end) colors.push_back(end);
            valueZone.y = colors.size() - 1;
            return valueZone + kColorIndexOffset;
        }

        Vec2 requestShaderUniformPair(const PhiShaderUniform& start, const PhiShaderUniform& end) {
            Vec2 valueZone;
            if (shaderUniforms.empty() || shaderUniforms[shaderUniforms.size() - 1] != start) shaderUniforms.push_back(start);
            valueZone.x = shaderUniforms.size() - 1;
            if (shaderUniforms.empty() || shaderUniforms[shaderUniforms.size() - 1] != end) shaderUniforms.push_back(end);
            valueZone.y = shaderUniforms.size() - 1;
            return valueZone + kShaderUniformIndexOffset;
        }

        Vec2 requestShaderUniformPair(float64 start, float64 end) {
            return requestShaderUniformPair(PhiShaderUniform(start), PhiShaderUniform(end));
        }

        static std::string textInterplate(const std::string& s, const std::string& e, float64 p) {
            auto sps = s.find("%P%");
            auto eps = e.find("%P%");
            
            if (sps != std::string::npos && eps != std::string::npos) {
                float64 sv = 0.0, ev = 0.0;
                try { sv = std::stod(replaceStringWith(s, "%P%", "")); } catch (...) {}
                try { ev = std::stod(replaceStringWith(e, "%P%", "")); } catch (...) {}
                auto v = (ev - sv) * p + sv;

                if (std::fmod(sv, 1.0) == 0.0 && std::fmod(ev, 1.0) == 0.0) {
                    return std::format("{:.0f}", v);
                } else {
                    return std::format("{:.3f}", v);
                }
            } else if (s.empty() && e.empty()) return "";
            else if (e.empty()) return textInterplate(e, replaceStringWith(s, "%P%", ""), 1.0 - p);
            else if (s.empty()) return stringSliceProgress(e, p);
            else {
                int64 ml = std::min(s.size(), e.size());
                if (s.substr(0, ml) == e.substr(0, ml)) {
                    auto take = (int64)std::round((float64)((e.size() - s.size()) * p)) + s.size();
                    return s + stringSliceProgress(e.substr(ml, e.size() - ml), p);
                } else return replaceStringWith(s, "%P%", "");
            }
        }

        std::optional<std::string> getText(float64 index, const Vec2& valueZone) noexcept {
            if (valueZone.x < kTextIndexOffset) return std::nullopt;

            auto start = texts[(uint64)valueZone.x - kTextIndexOffset];
            auto end = texts[(uint64)valueZone.y - kTextIndexOffset];
            auto p = index - valueZone.x;

            return textInterplate(start, end, p);
        }

        Color getColor(float64 index, const Color& defaultValue, const Vec2& valueZone) noexcept {
            if (valueZone.x < kColorIndexOffset) return defaultValue;

            auto start = colors[(uint64)valueZone.x - kColorIndexOffset];
            auto end = colors[(uint64)valueZone.y - kColorIndexOffset];
            auto p = index - valueZone.x;
            return start * (1.0 - p) + end * p;
        }

        PhiShaderUniform getShaderUniform(float64 index, const PhiShaderUniform& defaultValue, const Vec2& valueZone) noexcept {
            if (valueZone.x < kShaderUniformIndexOffset) return defaultValue;

            auto start = shaderUniforms[(uint64)valueZone.x - kShaderUniformIndexOffset];
            auto end = shaderUniforms[(uint64)valueZone.y - kShaderUniformIndexOffset];
            auto p = index - valueZone.x;
            return PhiShaderUniform::Interpolate(start, end, p);
        }

        bool requestLoadTexture(const std::string& name) {
            if (textures.contains(name)) return true;
            if (!textureLoader) return false;

            auto id = textureLoader(name);

            if (id.has_value()) {
                textures[name] = id.value();
                return true;
            }

            return false;
        }

        bool isTextureLoaded(const std::string& name) noexcept {
            return textures.contains(name);
        }

        std::pair<uint64, Vec2>& getTexture(const std::string& name) noexcept {
            return textures[name];
        }

        void clearTextures() {
            for (auto& [_, texture] : textures) {
                if (textureDestroyer) {
                    textureDestroyer(texture.first);
                }
            }

            textures.clear();
        }

        uint64 requestShaderName(const std::string& name) {
            uint64 id = shaderNameMap.size();
            shaderNameMap[id] = name;
            return id;
        }

        std::string getShaderName(uint64 id) noexcept {
            return shaderNameMap[id];
        }

        ~PhiStoryboardAssets() {
            clearTextures();
        }
    };

    struct PhiHitEffectItem {
        struct Particle {
            float64 dt, rotation, size;
        };

        float64 time;
        uint64 lineIndex, noteIndex;
        std::vector<Particle> particles;
    };

    struct PhiChart {
        struct State {
            float64 lastUpdateTime;
            uint64 firstHitEffectIndex;

            void timeUpdated(float64 t) noexcept {
                if (lastUpdateTime > t) {
                    firstHitEffectIndex = 0;
                }

                lastUpdateTime = t;
            }

            void passedHitEffectIndex(uint64 index) noexcept {
                if (firstHitEffectIndex == index) {
                    firstHitEffectIndex++;
                }
            }
        };

        struct UserOptions {
            float64 noteScaling = 1.0;

            float64 unsafeBackgroundDim = 0.8;
            float64 backgroundDim = 0.6;
            float64 backgroundTextureBlurRadius = (float64)1 / 20;

            Color lineDefaultColor = { (float64)0xff / 0xff, (float64)0xec / 0xff, (float64)0x9f / 0xff, 1.0 };

            std::pair<Color, Color> progressBarDefaultColor = {
                { (float64)145 / 255, (float64)145 / 255, (float64)145 / 255, 0.85 },
                { 1.0, 1.0, 1.0, 0.9 }
            };

            Vec2 storyboardTextBaseSize = { 0.028125, 0.0 };

            enum class EnumStoryboardTextureSclaingBehavior {
                AboutWidth,
                AboutHeight,
                Stretch
            };
            EnumStoryboardTextureSclaingBehavior storyboardTextureSclaingBehavior = EnumStoryboardTextureSclaingBehavior::AboutWidth;
            Vec2 storyboardTextureScaling = { 1.0, 1.0 };

            float64 hitEffectDuration = 0.5;
            float64 hitEffectAlpha = (float64)0xe1 / 0xff;
            float64 hitEffectTextureScaling = 1.54;
            float64 hitEffectParticleSize = 1.0;
            float64 hitEffectParticleDistance = 1.0;
        };

        PhiMeta meta;
        std::vector<PhiLine> lines;
        PhiAnimator animator;
        PhiStoryboardAssets storyboardAssets;
        PhiExtra extra;

        std::vector<PhiHitEffectItem> hitEffects;
        std::vector<float64> comboTimes;
        std::vector<uint64> zOrderSortedLines;
        uint64 rawHash;

        UserOptions options;

        State state;

        void init() {
            animator.init();

            for (auto& line : lines) {
                if (line.textureName.has_value()) {
                    storyboardAssets.requestLoadTexture(line.textureName.value());
                }

                line.init(animator);
            }

            std::unordered_set<std::string> shaderNames;
            for (auto& effect : extra.effects) {
                shaderNames.insert(effect.shaderName);

                for (auto& [_, layer] : effect.uniforms) {
                    layer.init();
                }
            }
            
            if (storyboardAssets.shaderPreloader) {
                std::unordered_map<std::string, uint64> shaderNameMapInv;

                for (auto& name : shaderNames) {
                    auto id = storyboardAssets.requestShaderName(name);
                    storyboardAssets.shaderPreloader(name, id);
                    shaderNameMapInv[name] = id;
                }

                for (auto& effect : extra.effects) {
                    effect.shaderId = shaderNameMapInv[effect.shaderName];
                }
            }

            extra.init();
            
            initSimulNote();
            initHitEffects();
            initPlayemntInfo();
            initZOrderSortedLines();
        }

        Vec2 getLinePositionRaw(float64 t, PhiLine& line) noexcept {
            return {
                animator.get(line, t, EnumPhiEventType::PositionX),
                animator.get(line, t, EnumPhiEventType::PositionY)
            };
        }

        Vec2 getLinePositionRelOrigin(float64 t, PhiLine& line, const Vec2& screenSize) noexcept {
            Vec2 pos = getLinePositionRaw(t, line);
            pos = pos / meta.worldViewport * screenSize;

            if (line.fatherLineIndex.has_value()) {
                auto fatherLineIndex = line.fatherLineIndex.value();
                if (0 <= fatherLineIndex && fatherLineIndex < lines.size()) {
                    auto& fatherLine = lines[fatherLineIndex];
                    auto fatherLinePosition = getLinePositionRelOrigin(t, fatherLine, screenSize);
                    auto fatherLineRotation = animator.get(fatherLine, t, EnumPhiEventType::SelfRotation);

                    pos = fatherLinePosition.rotateDegrees(
                        fatherLineRotation + std::atan2(pos.y, pos.x) * 180.0 / std::numbers::pi,
                        pos.length()
                    );
                }
            }

            return pos;
        }

        Vec2 getLinePosition(float64 t, PhiLine& line, const Vec2& screenSize) noexcept {
            Vec2 ori = getLinePositionRelOrigin(t, line, screenSize);
            return ori - meta.worldOrigin / meta.worldViewport * screenSize;
        }

        struct NoteFrameInfo {
            Vec2 headPosition, tailPosition;
            bool isArrived;
            float64 lineRotation, textureRotation, speedVectorRotation;
            Color color;
            Vec2 scale;

            void setHidden() noexcept {
                color.a = 0.0;
            }
        };

        NoteFrameInfo getNoteFrameInfo(
            PhiLine& line, PhiNote& note,
            float64 time, const Vec2& screenSize
        ) noexcept {
            NoteFrameInfo info {};

            auto linePosition = getLinePosition(time, line, screenSize);
            auto lineRotation = animator.get(line, time, EnumPhiEventType::SelfRotation);
            auto lineSpeedCoefficient = animator.get(line, time, EnumPhiEventType::SpeedCoefficient);
            auto lineAlpha = animator.get_alpha(line, time, 0.0);
            auto noteRotation = animator.get(note, time, EnumPhiEventType::SelfRotation);
            auto noteAxisRotation = animator.get(note, time, EnumPhiEventType::AxisRotation);
            auto noteColorIndex = animator.get(note, time, EnumPhiEventType::Color);
            auto noteColorIndexZone = animator.get_zone(note, time, EnumPhiEventType::Color);
            auto noteColor = storyboardAssets.getColor(noteColorIndex, { 1.0, 1.0, 1.0, 1.0 }, noteColorIndexZone);
            auto noteAlpha = animator.get_alpha(note, time, 1.0);
            auto noteScaling = Vec2 {
                animator.get(note, time, EnumPhiEventType::ScaleX),
                animator.get(note, time, EnumPhiEventType::ScaleY)
            };
            
            Transform2D lineTransform {};
            lineTransform.translate(linePosition);
            lineTransform.rotateDegrees(lineRotation);
            lineTransform.rotateDegrees(noteAxisRotation);
            lineTransform.scale(screenSize / meta.worldViewport.abs());
            lineTransform.scale(1.0, -1.0);

            info.isArrived = time >= note.time;
            auto finalSpeedCoefficient = lineSpeedCoefficient * animator.get(note, time, EnumPhiEventType::SpeedCoefficient);
            auto noteFloorPosition = (note.floorPosition - note.getFloorPositionAt(time, animator)) * finalSpeedCoefficient * meta.speedUnit;
            Vec2 noteBasePosition = { animator.get(note, time, EnumPhiEventType::PositionX), animator.get(note, time, EnumPhiEventType::PositionY) };

            auto noteRelPositionHead = noteBasePosition + Vec2 { 0.0, info.isArrived ? 0.0 : noteFloorPosition.x },
                noteRelPositionTail = noteBasePosition + Vec2 { 0.0, noteFloorPosition.y };
            
            info.color = noteColor.applyAlpha(noteAlpha);

            if (line.enableCover && !info.isArrived) {
                if (meta.isHoldCoverAtHead && noteRelPositionHead.y * (note.isReversedCover ? -1.0 : 1.0) < -meta.coverEllipsis) info.setHidden();
                if (!meta.isHoldCoverAtHead && noteRelPositionTail.y * (note.isReversedCover ? -1.0 : 1.0) < -meta.coverEllipsis) info.setHidden();
            }

            if (note.isHold() && meta.isZeroLengthHoldHidden && note.floorPosition.xyDiff() == 0) info.setHidden();
            if (noteRelPositionHead.y > 2.0 && meta.isHighNoteHidden) info.setHidden();
            if (meta.isRegLineAlphaNoteHidden && lineAlpha < 0.0) info.setHidden();

            info.headPosition = lineTransform.transformPoint(noteRelPositionHead);
            info.tailPosition = lineTransform.transformPoint(noteRelPositionTail);
            info.lineRotation = lineRotation;
            info.textureRotation = lineRotation + noteRotation + noteAxisRotation;
            info.speedVectorRotation = lineRotation + noteAxisRotation - 90.0;
            if (finalSpeedCoefficient < 0) info.speedVectorRotation += 180.0;
            info.scale = noteScaling;

            return info;
        }

        uint64 getCombo(float64 t) const noexcept {
            if (comboTimes.empty() || comboTimes[0] > t) return 0;

            uint64 left = 0, right = comboTimes.size() - 1;
            uint64 ans = 1;

            while (left <= right) {
                uint64 mid = left + (right - left) / 2;
                if (comboTimes[mid] <= t) {
                    ans = mid + 1;
                    left = mid + 1;
                } else {
                    right = mid - 1;
                }
            }

            return ans;
        }

        private:
        void initSimulNote() {
            std::unordered_map<float64, uint64> noteTimes;

            for (auto& line : lines) {
                for (auto& note : line.notes) {
                    noteTimes[note.time]++;
                }
            }

            for (auto& line : lines) {
                for (auto& note : line.notes) {
                    note.isSimul = noteTimes[note.time] > 1;
                }
            }
        }

        void initHitEffects() {
            std::mt19937 rng { std::random_device {} () };
            std::uniform_real_distribution<float64> rng_dist { 0.0, 1.0 };
            auto uniform = [&](float64 a, float64 b) { return a + (b - a) * rng_dist(rng); };

            hitEffects.clear();

            for (auto& line : lines) {
                for (auto& note : line.notes) {
                    if (note.isFake) continue;

                    float64 t = note.time;
                    while (t <= note.time + note.holdTime) {
                        auto& item = hitEffects.emplace_back();
                        item.time = t;
                        item.lineIndex = &line - lines.data();
                        item.noteIndex = &note - line.notes.data();

                        for (uint64 i = 0; i < 4; i++) {
                            auto& particle = item.particles.emplace_back();
                            particle.rotation = uniform(0.0, 360.0);
                            particle.size = uniform(185.0, 265.0);
                        }

                        t += 30.0 / line.getBpmAtSecond(t);
                    }
                }
            }

            std::stable_sort(hitEffects.begin(), hitEffects.end(), [](const auto& a, const auto& b) {
                return a.time < b.time;
            });
        }

        void initPlayemntInfo() {
            for (auto& line : lines) {
                for (auto& note : line.notes) {
                    if (note.isFake) continue;
                    comboTimes.push_back(note.time + std::max(0.0, note.holdTime - 0.2));
                }
            }

            std::sort(comboTimes.begin(), comboTimes.end());
        }

        void initZOrderSortedLines() {
            zOrderSortedLines.clear();
            for (uint64 i = 0; i < lines.size(); i++) zOrderSortedLines.push_back(i);

            std::stable_sort(zOrderSortedLines.begin(), zOrderSortedLines.end(), [&](uint64 a, uint64 b){
                return lines[a].zOrder < lines[b].zOrder;
            });
        }
    };

    PhiChart loadPhiChartFromOfficialJson(const Data& data) {
        auto err = [](const std::string& msg) {
            throw std::runtime_error(std::format("official: {}", msg));
        };

        auto jsonRoot = JsonNode::Parse(data);

        PhiChart chart {};

        if (!jsonRoot.isObject()) err("root is not an object");

        if (!jsonRoot.hasKey("formatVersion")) err("missing formatVersion");
        if (!jsonRoot["formatVersion"].isNumber()) err("formatVersion is not a number");
        uint64 formatVersion = jsonRoot["formatVersion"].getNumber();

        chart.meta.isHoldCoverAtHead = true;
        chart.meta.isZeroLengthHoldHidden = true;
        chart.meta.isHighNoteHidden = true;
        chart.meta.isRegLineAlphaNoteHidden = false;
        chart.meta.lineWidthUnit = { 0.0, 5.76 };
        chart.meta.lineHeightUnit = { 0.0, 0.0075 };
        chart.meta.worldOrigin = { 0.0, 1.0 };
        chart.meta.worldViewport = { 1.0, -1.0 };

        if (1 <= formatVersion && formatVersion <= 3) {
            if (!jsonRoot.hasKey("offset")) err("missing offset");
            if (!jsonRoot["offset"].isNumber()) err("offset is not a number");
            chart.meta.offset = jsonRoot["offset"].getNumber();

            if (!jsonRoot.hasKey("judgeLineList")) err("missing judgeLineList");
            if (!jsonRoot["judgeLineList"].isArray()) err("judgeLineList is not an array");
            auto& judgeLineListNode = jsonRoot["judgeLineList"];

            for (auto& judgeLineNode : judgeLineListNode.getArray()) {
                if (!judgeLineNode.isObject()) err("judgeLineList item is not an object");
                
                auto& line = chart.lines.emplace_back();
                line.enableCover = true;

                if (!judgeLineNode.hasKey("bpm")) err("missing bpm");
                if (!judgeLineNode["bpm"].isNumber()) err("bpm is not a number");
                float64 bpm = judgeLineNode["bpm"].getNumber();
                float64 timeFactor = 1.875 / bpm;
                line.bpms = { { 0, bpm } };

                if (!judgeLineNode.hasKey("notesAbove")) err("missing notesAbove");
                if (!judgeLineNode["notesAbove"].isArray()) err("notesAbove is not an array");
                if (!judgeLineNode.hasKey("notesBelow")) err("missing notesBelow");
                if (!judgeLineNode["notesBelow"].isArray()) err("notesBelow is not an array");

                auto& notesAboveNode = judgeLineNode["notesAbove"];
                auto& notesBelowNode = judgeLineNode["notesBelow"];
                std::vector<std::pair<JsonNode*, bool>> noteGroups = {
                    { &notesAboveNode, true },
                    { &notesBelowNode, false }
                };

                for (auto& [ notesNodePtr, isAbove ] : noteGroups) {
                    auto& notesNode = *notesNodePtr;

                    for (auto& noteNode : notesNode.getArray()) {
                        if (!noteNode.isObject()) err("notesAbove/notesBelow item is not an object");

                        if (!noteNode.hasKey("type")) err("missing type");
                        if (!noteNode["type"].isNumber()) err("type is not a number");
                        auto type = PhiNoteTypeHelper::FromOfficial(noteNode["type"].getNumber());

                        if (!noteNode.hasKey("time")) err("missing time");
                        if (!noteNode["time"].isNumber()) err("time is not a number");
                        auto time = noteNode["time"].getNumber() * timeFactor;

                        if (!noteNode.hasKey("holdTime")) err("missing holdTime");
                        if (!noteNode["holdTime"].isNumber()) err("holdTime is not a number");
                        auto holdTime = noteNode["holdTime"].getNumber() * timeFactor;

                        if (!noteNode.hasKey("positionX")) err("missing positionX");
                        if (!noteNode["positionX"].isNumber()) err("positionX is not a number");
                        auto positionX = noteNode["positionX"].getNumber() * 0.05625;

                        std::string speedKey = "speed";
                        if (!noteNode.hasKey(speedKey)) err(std::string("missing ") + speedKey + "");
                        if (!noteNode[speedKey].isNumber()) err(speedKey + " is not a number");
                        auto speed = noteNode[speedKey].getNumber();

                        auto& note = line.notes.emplace_back();
                        note.type = type;
                        note.time = time;
                        note.holdTime = holdTime;
                        note.isFake = false;

                        chart.animator.addEvent(note, PhiEvent {
                            .timeZone = INF_TZ,
                            .valueZone = { positionX, positionX },
                            .type = EnumPhiEventType::PositionX,
                            .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                        });

                        if (type == EnumPhiNoteType::Hold) {
                            note.fixedHoldSpeed = speed * 0.6;
                        } else {
                            if (speed != 1.0) {
                                chart.animator.addEvent(note, PhiEvent {
                                    .timeZone = INF_TZ,
                                    .valueZone = { speed, speed },
                                    .type = EnumPhiEventType::SpeedCoefficient,
                                    .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                                });
                            }
                        }

                        if (!isAbove) {
                            chart.animator.addEvent(note, PhiEvent {
                                .timeZone = INF_TZ,
                                .valueZone = { -1.0, -1.0 },
                                .type = EnumPhiEventType::SpeedCoefficient,
                                .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS_2
                            });

                            chart.animator.addEvent(note, PhiEvent {
                                .timeZone = INF_TZ,
                                .valueZone = { 180.0, 180.0 },
                                .type = EnumPhiEventType::SelfRotation,
                                .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS_2
                            });

                            note.reverseCover();
                        }
                    }
                }

                if (!judgeLineNode.hasKey("speedEvents")) err("missing speedEvents");
                if (!judgeLineNode["speedEvents"].isArray()) err("speedEvents is not an array");
                if (!judgeLineNode.hasKey("judgeLineMoveEvents")) err("missing judgeLineMoveEvents");
                if (!judgeLineNode["judgeLineMoveEvents"].isArray()) err("judgeLineMoveEvents is not an array");
                if (!judgeLineNode.hasKey("judgeLineRotateEvents")) err("missing judgeLineRotateEvents");
                if (!judgeLineNode["judgeLineRotateEvents"].isArray()) err("judgeLineRotateEvents is not an array");
                if (!judgeLineNode.hasKey("judgeLineDisappearEvents")) err("missing judgeLineDisappearEvents");
                if (!judgeLineNode["judgeLineDisappearEvents"].isArray()) err("judgeLineDisappearEvents is not an array");

                auto& speedEventsNode = judgeLineNode["speedEvents"];
                auto& judgeLineMoveEventsNode = judgeLineNode["judgeLineMoveEvents"];
                auto& judgeLineRotateEventsNode = judgeLineNode["judgeLineRotateEvents"];
                auto& judgeLineDisappearEventsNode = judgeLineNode["judgeLineDisappearEvents"];

                // events, startKey, endKey, easeTypeKey, type, converter
                std::vector<std::tuple<JsonNode*, std::string, std::string, std::string, EnumPhiEventType, std::function<float64(float64)>>> eventGroups;
                
                if (formatVersion == 1) {
                    eventGroups = {
                        { &speedEventsNode, "value", "value", "", EnumPhiEventType::Speed, [](float64 v) { return v * 0.6; } },
                        { &judgeLineMoveEventsNode, "start", "end", "", EnumPhiEventType::PositionX, [](float64 v) { return std::floor(v / 1000.0) / 880.0; } },
                        { &judgeLineMoveEventsNode, "start", "end", "", EnumPhiEventType::PositionY, [](float64 v) { return std::fmod(v, 1000.0) / 520.0; } },
                        { &judgeLineRotateEventsNode, "start", "end", "", EnumPhiEventType::SelfRotation, [](float64 v) { return -v; } },
                        { &judgeLineDisappearEventsNode, "start", "end", "", EnumPhiEventType::AdditiveAlpha, [](float64 v) { return v; } }
                    };
                } else if (formatVersion == 3) {
                    eventGroups = {
                        { &speedEventsNode, "value", "value", "", EnumPhiEventType::Speed, [](float64 v) { return v * 0.6; } },
                        { &judgeLineMoveEventsNode, "start", "end", "", EnumPhiEventType::PositionX, [](float64 v) { return v; } },
                        { &judgeLineMoveEventsNode, "start2", "end2", "", EnumPhiEventType::PositionY, [](float64 v) { return v; } },
                        { &judgeLineRotateEventsNode, "start", "end", "", EnumPhiEventType::SelfRotation, [](float64 v) { return -v; } },
                        { &judgeLineDisappearEventsNode, "start", "end", "", EnumPhiEventType::AdditiveAlpha, [](float64 v) { return v; } }
                    };
                }

                for (auto& [eventsNode, startKey, endKey, easeTypeKey, type, converter] : eventGroups) {
                    if (!eventsNode->isArray()) err("XXXEvents is not an array");

                    for (auto& eventNode : eventsNode->getArray()) {
                        if (!eventNode.isObject()) err("XXXEvents item is not an object");

                        if (!eventNode.hasKey("startTime")) err("missing startTime");
                        if (!eventNode["startTime"].isNumber()) err("startTime is not a number");
                        auto startTime = eventNode["startTime"].getNumber() * timeFactor;

                        if (!eventNode.hasKey("endTime")) err("missing endTime");
                        if (!eventNode["endTime"].isNumber()) err("endTime is not a number");
                        auto endTime = eventNode["endTime"].getNumber() * timeFactor;

                        if (!eventNode.hasKey(startKey)) err(std::string("missing ") + startKey + "");
                        if (!eventNode[startKey].isNumber()) err(std::string(startKey) + " is not a number");
                        auto startValue = converter(eventNode[startKey].getNumber());

                        if (!eventNode.hasKey(endKey)) err(std::string("missing ") + endKey + "");
                        if (!eventNode[endKey].isNumber()) err(std::string(endKey) + " is not a number");
                        auto endValue = converter(eventNode[endKey].getNumber());

                        chart.animator.addEvent(line, PhiEvent {
                            .timeZone = { startTime, endTime },
                            .valueZone = { startValue, endValue },
                            .type = type,
                            .layerIndex = PhiEventLayerIndexs::LINE_DEFAULT
                        });
                    }
                }
            }
        } else {
            err(std::string("unsupported formatVersion: ") + std::to_string(formatVersion));
        }

        chart.rawHash = data.getHash();
        return chart;
    }

    PhiChart loadPhiChartFromRpeJson(const Data& data) {
        auto err = [](const std::string& msg) {
            throw std::runtime_error(std::format("rpe: {}", msg));
        };

        auto jsonRoot = JsonNode::Parse(data);
        
        PhiChart chart {};

        chart.meta.isHoldCoverAtHead = false;
        chart.meta.isZeroLengthHoldHidden = false;
        chart.meta.isHighNoteHidden = false;
        chart.meta.isRegLineAlphaNoteHidden = true;
        chart.meta.lineWidthUnit = { (float64)4000 / 1350, 0.0 };
        chart.meta.lineHeightUnit = { 0.0, (float64)1 / 180 };
        chart.meta.worldOrigin = { (float64)-1350 / 2, (float64)900 / 2 };
        chart.meta.worldViewport = { 1350, -900 };
        chart.meta.speedUnit = 120.0;

        if (!jsonRoot.isObject()) err("root is not an object");

        if (!jsonRoot.hasKey("META")) err("missing META");
        if (!jsonRoot["META"].isObject()) err("META is not an object");
        auto& metaNode = jsonRoot["META"];

        if (!metaNode.hasKey("RPEVersion")) err("missing RPEVersion");
        if (!metaNode["RPEVersion"].isNumber()) err("RPEVersion is not a number");
        chart.meta.rpeVersion = metaNode["RPEVersion"].getNumber();

        if (!metaNode.hasKey("charter")) err("missing charter");
        if (!metaNode["charter"].isString()) err("charter is not a string");
        chart.meta.charter = metaNode["charter"].getString();

        if (!metaNode.hasKey("composer")) err("missing composer");
        if (!metaNode["composer"].isString()) err("composer is not a string");
        chart.meta.composer = metaNode["composer"].getString();

        if (!metaNode.hasKey("name")) err("missing name");
        if (!metaNode["name"].isString()) err("name is not a string");
        chart.meta.title = metaNode["name"].getString();

        if (!metaNode.hasKey("level")) err("missing level");
        if (!metaNode["level"].isString()) err("level is not a string");
        chart.meta.difficulty = metaNode["level"].getString();

        if (!metaNode.hasKey("offset")) err("missing offset");
        if (!metaNode["offset"].isNumber()) err("offset is not a number");
        chart.meta.offset = metaNode["offset"].getNumber() / 1000;

        auto parseTimeTuple = [](const JsonNode& node, float64* dst) {
            if (!node.isArray()) return false;
            if (node.getArray().size() != 3) return false;

            const auto& arr = node.getArray();
            if (!arr[0].isNumber()) return false;
            if (!arr[1].isNumber()) return false;
            if (!arr[2].isNumber()) return false;

            float64 n1 = arr[0].getNumber(),
                n2 = arr[1].getNumber(),
                n3 = arr[2].getNumber();

            *dst = n1 + n2 / n3;
            return true;
        };

        std::vector<PhiBPMEvent> sharedBpmEvents;
        
        if (!jsonRoot.hasKey("BPMList")) err("missing BPMList");
        if (!jsonRoot["BPMList"].isArray()) err("BPMList is not an array");
        auto& bpmListNode = jsonRoot["BPMList"];

        for (auto& bpmEventNode : bpmListNode.getArray()) {
            if (!bpmEventNode.isObject()) err("BPMList item is not an object");

            if (!bpmEventNode.hasKey("startTime")) err("missing startTime");
            float64 startTime;
            if (!parseTimeTuple(bpmEventNode["startTime"], &startTime)) err("startTime is not a valid time tuple");

            if (!bpmEventNode.hasKey("bpm")) err("missing bpm");
            if (!bpmEventNode["bpm"].isNumber()) err("bpm is not a number");
            float64 bpm = bpmEventNode["bpm"].getNumber();

            sharedBpmEvents.push_back({
                .time = startTime,
                .bpm = bpm
            });
        }

        PhiBPMEvent::SortBpmEvents(sharedBpmEvents);

        if (!jsonRoot.hasKey("judgeLineList")) err("missing judgeLineList");
        if (!jsonRoot["judgeLineList"].isArray()) err("judgeLineList is not an array");
        auto& judgeLineListNode = jsonRoot["judgeLineList"];

        for (auto& judgeLineNode : judgeLineListNode.getArray()) {
            if (!judgeLineNode.isObject()) err("judgeLineList item is not an object");

            auto& line = chart.lines.emplace_back();
            line.bpms = sharedBpmEvents;

            if (judgeLineNode.hasKey("bpmfactor")) {
                if (!judgeLineNode["bpmfactor"].isNumber()) err("bpmfactor is not a number");
                auto factor = judgeLineNode["bpmfactor"].getNumber();

                for (auto& e : line.bpms) {
                    e.bpm /= factor;
                }
            }

            if (!judgeLineNode.hasKey("eventLayers")) err("missing eventLayers");
            if (!judgeLineNode["eventLayers"].isArray()) err("eventLayers is not an array");
            auto& eventLayersNode = judgeLineNode["eventLayers"];

            uint64 eventLayerIndex = 0;
            // events, type, converter
            using EventGroupType = std::tuple<JsonNode*, EnumPhiEventType, std::function<float64(float64)>>;

            auto progressEventGroup = [&](EventGroupType group) -> std::pair<bool, std::string> {
                auto& [eventsNode, type, converter] = group;
                if (!eventsNode->isArray()) return { false, "XXXEvents is not an array" };
                
                auto& arr =  eventsNode->getArray();
                if (arr.empty()) return { true, "" };

                float64 earliestTime = INF_TIME;

                for (auto& eventNode : arr) {
                    if (!eventNode.isObject()) return { false, "XXXEvents item is not an object" };

                    if (!eventNode.hasKey("startTime")) return { false, "missing startTime" };
                    float64 startTime;
                    if (!parseTimeTuple(eventNode["startTime"], &startTime)) return { false, "startTime is not a valid time tuple" };

                    if (!eventNode.hasKey("endTime")) return { false, "missing endTime" };
                    float64 endTime;
                    if (!parseTimeTuple(eventNode["endTime"], &endTime)) return { false, "endTime is not a valid time tuple" };

                    float64 start, end;

                    if (!eventNode.hasKey("start")) return { false, "missing start" };
                    if (!eventNode.hasKey("end")) return { false, "missing end" };

                    if (type == EnumPhiEventType::Text) {
                        if (!eventNode["start"].isString()) return { false, "start is not a string" };
                        if (!eventNode["end"].isString()) return { false, "end is not a string" };

                        auto valueZone = chart.storyboardAssets.requestTextPair(eventNode["start"].getString(), eventNode["end"].getString());
                        start = valueZone.x;
                        end = valueZone.y;
                    } else if (type == EnumPhiEventType::Color) {
                        if (!eventNode["start"].isArray()) return { false, "start is not an array" };
                        if (!eventNode["end"].isArray()) return { false, "end is not an array" };

                        auto& startArr = eventNode["start"].getArray();
                        auto& endArr = eventNode["end"].getArray();

                        if (startArr.size() < 3) return { false, "start array size is less than 3" };
                        if (endArr.size() < 3) return { false, "end array size is less than 3" };

                        if (!startArr[0].isNumber()) return { false, "start array item is not a number" };
                        if (!startArr[1].isNumber()) return { false, "start array item is not a number" };
                        if (!startArr[2].isNumber()) return { false, "start array item is not a number" };

                        if (!endArr[0].isNumber()) return { false, "end array item is not a number" };
                        if (!endArr[1].isNumber()) return { false, "end array item is not a number" };
                        if (!endArr[2].isNumber()) return { false, "end array item is not a number" };

                        auto startColor = Color {
                            startArr[0].getNumber() / 255,
                            startArr[1].getNumber() / 255,
                            startArr[2].getNumber() / 255,
                            1.0
                        };

                        auto endColor = Color {
                            endArr[0].getNumber() / 255,
                            endArr[1].getNumber() / 255,
                            endArr[2].getNumber() / 255,
                            1.0
                        };

                        auto valueZone = chart.storyboardAssets.requestColorPair(startColor, endColor);
                        start = valueZone.x;
                        end = valueZone.y;
                    } else {
                        if (!eventNode["start"].isNumber()) return { false, "start is not a number" };
                        start = eventNode["start"].getNumber();

                        if (!eventNode["end"].isNumber()) return { false, "end is not a number" };
                        end = eventNode["end"].getNumber();
                    }

                    start = converter(start);
                    end = converter(end);

                    startTime = line.beat2sec(startTime);
                    endTime = line.beat2sec(endTime);
                    earliestTime = std::min(earliestTime, startTime);

                    PhiEvent e {};
                    e.timeZone = { startTime, endTime };
                    e.valueZone = { start, end };
                    e.type = type;
                    e.layerIndex = PhiEventLayerIndexs::LINE_DEFAULT + eventLayerIndex;

                    if (eventNode.hasKey("easingType") && eventNode["easingType"].isNumber()) {
                        e.easingFuncContext = (void*)(uint64)eventNode["easingType"].getNumber();
                    }

                    if ((uint64)e.easingFuncContext > 1) {
                        e.easingFunc = [](void* ctx, float64 p) { return EaseSet::Phigros::RePhiEdit::easing((uint64)ctx, p); };
                        e.easingIntFunc = [](void* ctx, float64 p) { return EaseSet::Phigros::RePhiEdit::easing_int((uint64)ctx, p); };
                    }

                    if (eventNode.hasKey("easingLeft")) {
                        if (!eventNode["easingLeft"].isNumber()) return { false, "easingLeft is not a number" };
                        e.easingZone.x = eventNode["easingLeft"].getNumber();
                    }

                    if (eventNode.hasKey("easingRight")) {
                        if (!eventNode["easingRight"].isNumber()) return { false, "easingRight is not a number" };
                        e.easingZone.y = eventNode["easingRight"].getNumber();
                    }

                    chart.animator.addEvent(line, e);

                    if (&eventNode == &arr.front()) {
                        if (type != EnumPhiEventType::Text && type != EnumPhiEventType::Color) {
                            if (start != end) {
                                PhiEvent e {};
                                e.timeZone = { -INF_TIME, startTime };
                                e.valueZone = { start - (end - start) * (startTime + INF_TIME), start };
                                e.type = type;
                                e.layerIndex = PhiEventLayerIndexs::LINE_DEFAULT + eventLayerIndex;
                                chart.animator.addEvent(line, e);
                            }
                        }
                    }
                }

                if (type == EnumPhiEventType::Text) {
                    PhiEvent e {};
                    e.timeZone = { -INF_TIME, earliestTime };
                    e.valueZone = chart.storyboardAssets.requestTextPair("", "");
                    e.type = type;
                    e.layerIndex = PhiEventLayerIndexs::LINE_DEFAULT + eventLayerIndex;
                    chart.animator.addEvent(line, e);
                }

                return { true, "" };
            };

            for (auto& eventLayerNode : eventLayersNode.getArray()) {
                if (eventLayerNode.isNull()) continue;
                if (!eventLayerNode.isObject()) err("eventLayers item is not an object");

                std::vector<EventGroupType> groups;

                if (eventLayerNode.hasKey("alphaEvents")) groups.push_back({ &eventLayerNode["alphaEvents"], EnumPhiEventType::AdditiveAlpha, [](float64 v) { return v / 255; } });
                if (eventLayerNode.hasKey("moveXEvents")) groups.push_back({ &eventLayerNode["moveXEvents"], EnumPhiEventType::PositionX, [](float64 v) { return v; } });
                if (eventLayerNode.hasKey("moveYEvents")) groups.push_back({ &eventLayerNode["moveYEvents"], EnumPhiEventType::PositionY, [](float64 v) { return v; } });
                if (eventLayerNode.hasKey("rotateEvents")) groups.push_back({ &eventLayerNode["rotateEvents"], EnumPhiEventType::SelfRotation, [](float64 v) { return v; } });
                if (eventLayerNode.hasKey("speedEvents")) groups.push_back({ &eventLayerNode["speedEvents"], EnumPhiEventType::Speed, [](float64 v) { return v; } });

                for (auto& group : groups) {
                    auto [success, msg] = progressEventGroup(group);
                    if (!success) err(msg);
                }

                eventLayerIndex++;
            }

            if (judgeLineNode.hasKey("extended")) {
                auto& extendedNode = judgeLineNode["extended"];
                if (!extendedNode.isObject()) err("extended is not an object");

                std::vector<EventGroupType> groups;

                if (extendedNode.hasKey("textEvents")) groups.push_back({ &extendedNode["textEvents"], EnumPhiEventType::Text, [](float64 v) { return v; } });
                if (extendedNode.hasKey("scaleXEvents")) groups.push_back({ &extendedNode["scaleXEvents"], EnumPhiEventType::ScaleX, [](float64 v) { return v; } });
                if (extendedNode.hasKey("scaleYEvents")) groups.push_back({ &extendedNode["scaleYEvents"], EnumPhiEventType::ScaleY, [](float64 v) { return v; } });
                if (extendedNode.hasKey("colorEvents")) groups.push_back({ &extendedNode["colorEvents"], EnumPhiEventType::Color, [](float64 v) { return v; } });

                for (auto& group : groups) {
                    auto [success, msg] = progressEventGroup(group);
                    if (!success) err(msg);
                }

                eventLayerIndex++;
            }

            if (judgeLineNode.hasKey("notes")) {
                auto& notesNode = judgeLineNode["notes"];
                if (!notesNode.isArray()) err("notes is not an array");

                for (auto& noteNode : notesNode.getArray()) {
                    if (!noteNode.isObject()) err("notes item is not an object");

                    auto& note = line.notes.emplace_back();

                    if (!noteNode.hasKey("startTime")) err("missing startTime");
                    float64 startTime;
                    if (!parseTimeTuple(noteNode["startTime"], &startTime)) err("startTime is not a valid time tuple");

                    if (!noteNode.hasKey("endTime")) err("missing endTime");
                    float64 endTime;
                    if (!parseTimeTuple(noteNode["endTime"], &endTime)) err("endTime is not a valid time tuple");

                    startTime = line.beat2sec(startTime);
                    endTime = line.beat2sec(endTime);

                    if (!noteNode.hasKey("above")) err("missing above");
                    bool isAbove;
                    if (noteNode["above"].isBool()) isAbove = noteNode["above"].getBool();
                    else if (noteNode["above"].isNumber()) isAbove = noteNode["above"].getNumber() == 1;
                    else err("above is not a boolean or number");

                    if (!noteNode.hasKey("type")) err("missing type");
                    if (!noteNode["type"].isNumber()) err("type is not a number");
                    auto type = PhiNoteTypeHelper::FromRPE(noteNode["type"].getNumber());

                    if (!noteNode.hasKey("speed")) err("missing speed");
                    if (!noteNode["speed"].isNumber()) err("speed is not a number");
                    float64 speed = noteNode["speed"].getNumber();

                    if (!noteNode.hasKey("isFake")) err("missing isFake");
                    bool isFake;
                    if (noteNode["isFake"].isBool()) isFake = noteNode["isFake"].getBool();
                    else if (noteNode["isFake"].isNumber()) isFake = noteNode["isFake"].getNumber() == 1;
                    else err("isFake is not a boolean or number");

                    if (!noteNode.hasKey("positionX")) err("missing positionX");
                    if (!noteNode["positionX"].isNumber()) err("positionX is not a number");
                    float64 positionX = noteNode["positionX"].getNumber();
                    
                    if (noteNode.hasKey("yOffset")) {
                        if (!noteNode["yOffset"].isNumber()) err("yOffset is not a number");
                        auto yOffset = noteNode["yOffset"].getNumber() * speed;
                        if (!isAbove) yOffset *= -1;
                        
                        if (yOffset != 0.0) {
                            chart.animator.addEvent(note, PhiEvent {
                                .timeZone = INF_TZ,
                                .valueZone = { yOffset, yOffset },
                                .type = EnumPhiEventType::PositionY,
                                .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                            });
                        }
                    }

                    if (noteNode.hasKey("visibleTime")) {
                        if (!noteNode["visibleTime"].isNumber()) err("visibleTime is not a number");
                        auto visibleTime = noteNode["visibleTime"].getNumber();

                        if (visibleTime < 999999.0) {
                            chart.animator.addEvent(note, PhiEvent {
                                .timeZone = { -INF_TIME, startTime - visibleTime },
                                .valueZone = { 0.0, 0.0 },
                                .type = EnumPhiEventType::MultiplyAlpha,
                                .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                            });

                            chart.animator.addEvent(note, PhiEvent {
                                .timeZone = { startTime - visibleTime, INF_TIME },
                                .valueZone = { 1.0, 1.0 },
                                .type = EnumPhiEventType::MultiplyAlpha,
                                .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                            });
                        }
                    }

                    if (noteNode.hasKey("size")) {
                        if (!noteNode["size"].isNumber()) err("size is not a number");
                        auto size = noteNode["size"].getNumber();

                        if (size != 1.0) {
                            chart.animator.addEvent(note, PhiEvent {
                                .timeZone = INF_TZ,
                                .valueZone = { size, size },
                                .type = EnumPhiEventType::ScaleX,
                                .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                            });
                        }
                    }

                    if (noteNode.hasKey("alpha")) {
                        if (!noteNode["alpha"].isNumber()) err("alpha is not a number");
                        auto alpha = noteNode["alpha"].getNumber() / 255;

                        if (alpha != 1.0) {
                            chart.animator.addEvent(note, PhiEvent {
                                .timeZone = INF_TZ,
                                .valueZone = { alpha, alpha },
                                .type = EnumPhiEventType::MultiplyAlpha,
                                .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS_2
                            });
                        }
                    }

                    if (noteNode.hasKey("tint")) {
                        if (!noteNode["tint"].isArray()) err("tint is not an array");

                        auto& arr = noteNode["tint"].getArray();
                        if (arr.size() < 3) err("tint array is too small");

                        auto& n1 = arr[0];
                        auto& n2 = arr[1];
                        auto& n3 = arr[2];

                        if (!n1.isNumber()) err("tint[0] is not a number");
                        if (!n2.isNumber()) err("tint[1] is not a number");
                        if (!n3.isNumber()) err("tint[2] is not a number");

                        auto color = Color {
                            n1.getNumber() / 255,
                            n2.getNumber() / 255,
                            n3.getNumber() / 255,
                            1.0
                        };

                        chart.animator.addEvent(note, PhiEvent {
                            .timeZone = INF_TZ,
                            .valueZone = chart.storyboardAssets.requestColorPair(color, color),
                            .type = EnumPhiEventType::Color,
                            .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                        });
                    }

                    note.type = type;
                    note.time = startTime;
                    note.holdTime = endTime - startTime;
                    note.isFake = isFake;

                    chart.animator.addEvent(note, PhiEvent {
                        .timeZone = INF_TZ,
                        .valueZone = { positionX, positionX },
                        .type = EnumPhiEventType::PositionX,
                        .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                    });

                    if (speed != 1.0) {
                        chart.animator.addEvent(note, PhiEvent {
                            .timeZone = INF_TZ,
                            .valueZone = { speed, speed },
                            .type = EnumPhiEventType::SpeedCoefficient,
                            .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                        });
                    }

                    if (!isAbove) {
                        chart.animator.addEvent(note, PhiEvent {
                            .timeZone = INF_TZ,
                            .valueZone = { -1.0, -1.0 },
                            .type = EnumPhiEventType::SpeedCoefficient,
                            .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS_2
                        });

                        chart.animator.addEvent(note, PhiEvent {
                            .timeZone = INF_TZ,
                            .valueZone = { 180.0, 180.0 },
                            .type = EnumPhiEventType::SelfRotation,
                            .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS_2
                        });

                        note.reverseCover();
                    }
                }
            }

            if (judgeLineNode.hasKey("attachUI")) {
                if (!judgeLineNode["attachUI"].isString()) err("attachUI is not a string");
                line.attachUI = PhiLineAttachUIHelper::FromString(judgeLineNode["attachUI"].getString());
            }

            if (judgeLineNode.hasKey("Texture")) {
                if (!judgeLineNode["Texture"].isString()) err("Texture is not a string");
                auto textureName = judgeLineNode["Texture"].getString();

                if (textureName != "line.png") {
                    line.textureName = textureName;
                }
            }

            if (judgeLineNode.hasKey("father")) {
                if (!judgeLineNode["father"].isNumber()) err("father is not a number");
                int64 fatherLineIndex = judgeLineNode["father"].getNumber();
                if (fatherLineIndex >= 0) {
                    line.fatherLineIndex = fatherLineIndex;
                }
            }

            bool enableCover = true;
            if (judgeLineNode.hasKey("isCover")) {
                if (judgeLineNode["isCover"].isNumber()) enableCover = judgeLineNode["isCover"].getNumber() == 1;
                else if (judgeLineNode["isCover"].isBool()) enableCover = judgeLineNode["isCover"].getBool();
                else err("isCover is not a boolean or number");
            }
            line.enableCover = enableCover;

            if (judgeLineNode.hasKey("zOrder")) {
                if (!judgeLineNode["zOrder"].isNumber()) err("zOrder is not a number");
                line.zOrder = judgeLineNode["zOrder"].getNumber();
            }

            if (judgeLineNode.hasKey("anchor")) {
                if (!judgeLineNode["anchor"].isArray()) err("anchor is not an array");

                auto& anchorArr = judgeLineNode["anchor"].getArray();
                if (anchorArr.size() < 2) err("anchor array size is less than 2");

                if (!anchorArr[0].isNumber() || !anchorArr[1].isNumber()) err("anchor array element is not a number");
                line.anchor = { anchorArr[0].getNumber(), anchorArr[1].getNumber() };
            }
        }

        chart.rawHash = data.getHash();
        return chart;
    }

    PhiChart loadPhiChartFromPec(const Data& data) {
        auto err = [](const std::string& msg) {
            throw std::runtime_error(std::format("pec: {}", msg));
        };

        struct TokenReader {
            std::string str;
            uint64 pos = 0;

            TokenReader(const std::string& str) : str(str) {}

            bool nextToken(std::string& dst) {
                jumpToNextNonWhiteSpace();
                if (pos >= str.size()) return false;
                uint64 start = pos;
                jumpToNextWhiteSpace();
                if (pos == start) return false;
                dst = str.substr(start, pos - start);
                jumpToNextNonWhiteSpace();
                return true;
            }

            private:
            bool currentIsWhiteSpace() const {
                return str[pos] == ' ' || str[pos] == '\t' || str[pos] == '\n' || str[pos] == '\r' || str[pos] == '\f' || str[pos] == '\v';
            }

            void jumpToNextWhiteSpace() {
                while (pos < str.size() && !currentIsWhiteSpace()) pos++;
            }

            void jumpToNextNonWhiteSpace() {
                while (pos < str.size() && currentIsWhiteSpace()) pos++;
            }
        };

        TokenReader reader(std::string((char*)data.data.data(), data.data.size()));
        std::string token;

        auto readNumber = [&](float64* dst) {
            if (!reader.nextToken(token)) return false;
            char* end;
            *dst = std::strtod(token.c_str(), &end);
            return end != token.c_str();
        };

        auto readBool = [&](bool* dst) {
            float64 num;
            if (!readNumber(&num)) return false;
            *dst = num == 1.0;
            return true;
        };

        PhiChart chart {};
        chart.meta.isHoldCoverAtHead = true;
        chart.meta.isZeroLengthHoldHidden = false;
        chart.meta.isHighNoteHidden = false;
        chart.meta.isRegLineAlphaNoteHidden = true;
        chart.meta.lineWidthUnit = { (float64)4000 / 1350, 0.0 };
        chart.meta.lineHeightUnit = { 0.0, (float64)1 / 180 };
        chart.meta.worldOrigin = { 0.0, 1400 };
        chart.meta.worldViewport = { 2048, -1400 };
        chart.meta.speedUnit = 120.0;

        float64 offset;
        if (!readNumber(&offset)) err("failed to read offset");
        chart.meta.offset = (offset - 150.0) / 1000.0;

        struct Commands {
            struct Bpm {
                float64 startTime, bpm;
            };

            struct Note {
                int64 lineIndex;
                PhiNote note;
                bool isAbove;
                float64 speed = 1.0, size = 1.0, positionX;
            };

            struct Event {
                Vec2 timeZone;
                float64 value;
                bool useFront = false;
                uint64 easingType = 1;
            };
        };

        std::vector<Commands::Bpm> bpmCommands;
        std::vector<Commands::Note> noteCommands;
        std::unordered_map<int64, std::unordered_map<EnumPhiEventType, std::vector<Commands::Event>>> eventCommands;

        while (reader.nextToken(token)) {
            if (token == "bp") {
                float64 startTime, bpm;
                if (!readNumber(&startTime)) err("failed to read startTime (bp)");
                if (!readNumber(&bpm)) err("failed to read bpm (bp)");

                bpmCommands.push_back({
                    .startTime = startTime,
                    .bpm = bpm
                });
            } else if (token == "n1" || token == "n2" || token == "n3" || token == "n4") {
                auto type = PhiNoteTypeHelper::FromPEC(token);

                float64 lineIndex;
                if (!readNumber(&lineIndex)) err("failed to read lineIndex (nx)");

                float64 startTime, endTime;
                if (!readNumber(&startTime)) err("failed to read startTime (nx)");
                if (type == EnumPhiNoteType::Hold) {
                    if (!readNumber(&endTime)) err("failed to read endTime (nx)");
                } else endTime = startTime;

                float64 positionX;
                bool isAbove, isFake;

                if (!readNumber(&positionX)) err("failed to read positionX (nx)");
                if (!readBool(&isAbove)) err("failed to read isAbove (nx)");
                if (!readBool(&isFake)) err("failed to read isFake (nx)");

                noteCommands.push_back(Commands::Note {
                    .lineIndex = (int64)lineIndex,
                    .note = PhiNote {
                        .type = type,
                        .time = startTime,
                        .holdTime = endTime - startTime,
                        .isFake = isFake
                    },
                    .isAbove = isAbove,
                    .positionX = positionX
                });
            } else if (token == "#") {
                float64 speed;
                if (!readNumber(&speed)) err("failed to read speed (#)");
                if (!noteCommands.empty()) noteCommands.back().speed = speed;
            } else if (token == "&") {
                float64 size;
                if (!readNumber(&size)) err("failed to read size (&)");
                if (!noteCommands.empty()) noteCommands.back().size = size;
            } else if (token == "cp") {
                float64 lineIndex;
                if (!readNumber(&lineIndex)) err("failed to read lineIndex (cp)");

                float64 time;
                if (!readNumber(&time)) err("failed to read time (cp)");

                float64 x, y;
                if (!readNumber(&x)) err("failed to read x (cp)");
                if (!readNumber(&y)) err("failed to read y (cp)");

                eventCommands[lineIndex][EnumPhiEventType::PositionX].push_back(Commands::Event {
                    .timeZone = { time, time }, .value = x
                });

                eventCommands[lineIndex][EnumPhiEventType::PositionY].push_back(Commands::Event {
                    .timeZone = { time, time }, .value = y
                });
            } else if (token == "cd") {
                float64 lineIndex;
                if (!readNumber(&lineIndex)) err("failed to read lineIndex (cd)");

                float64 time;
                if (!readNumber(&time)) err("failed to read time (cd)");

                float64 r;
                if (!readNumber(&r)) err("failed to read y (cd)");

                eventCommands[lineIndex][EnumPhiEventType::SelfRotation].push_back(Commands::Event {
                    .timeZone = { time, time }, .value = r
                });
            } else if (token == "ca") {
                float64 lineIndex;
                if (!readNumber(&lineIndex)) err("failed to read lineIndex (ca)");

                float64 time;
                if (!readNumber(&time)) err("failed to read time (ca)");

                float64 a;
                if (!readNumber(&a)) err("failed to read a (ca)");

                eventCommands[lineIndex][EnumPhiEventType::AdditiveAlpha].push_back(Commands::Event {
                    .timeZone = { time, time }, .value = a
                });
            } else if (token == "cv") {
                float64 lineIndex;
                if (!readNumber(&lineIndex)) err("failed to read lineIndex (cv)");

                float64 time;
                if (!readNumber(&time)) err("failed to read time (cv)");

                float64 v;
                if (!readNumber(&v)) err("failed to read v (cv)");

                eventCommands[lineIndex][EnumPhiEventType::Speed].push_back(Commands::Event {
                    .timeZone = { time, time }, .value = v
                });
            } else if (token == "cm") {
                float64 lineIndex;
                if (!readNumber(&lineIndex)) err("failed to read lineIndex (cm)");

                float64 startTime, endTime;
                if (!readNumber(&startTime)) err("failed to read startTime (cm)");
                if (!readNumber(&endTime)) err("failed to read endTime (cm)");

                float64 x, y;
                if (!readNumber(&x)) err("failed to read x (cm)");
                if (!readNumber(&y)) err("failed to read y (cm)");

                float64 easingType;
                if (!readNumber(&easingType)) err("failed to read easingType (cm)");

                eventCommands[lineIndex][EnumPhiEventType::PositionX].push_back(Commands::Event {
                    .timeZone = { startTime, endTime }, .value = x,
                    .useFront = true, .easingType = (uint64)easingType
                });

                eventCommands[lineIndex][EnumPhiEventType::PositionY].push_back(Commands::Event {
                    .timeZone = { startTime, endTime }, .value = y,
                    .useFront = true, .easingType = (uint64)easingType
                });
            } else if (token == "cr") {
                float64 lineIndex;
                if (!readNumber(&lineIndex)) err("failed to read lineIndex (cr)");

                float64 startTime, endTime;
                if (!readNumber(&startTime)) err("failed to read startTime (cr)");
                if (!readNumber(&endTime)) err("failed to read endTime (cr)");

                float64 r;
                if (!readNumber(&r)) err("failed to read r (cr)");

                float64 easingType;
                if (!readNumber(&easingType)) err("failed to read easingType (cr)");

                eventCommands[lineIndex][EnumPhiEventType::SelfRotation].push_back(Commands::Event {
                    .timeZone = { startTime, endTime }, .value = r,
                    .useFront = true, .easingType = (uint64)easingType
                });
            } else if (token == "cf") {
                float64 lineIndex;
                if (!readNumber(&lineIndex)) err("failed to read lineIndex (cf)");

                float64 startTime, endTime;
                if (!readNumber(&startTime)) err("failed to read startTime (cf)");
                if (!readNumber(&endTime)) err("failed to read endTime (cf)");

                float64 a;
                if (!readNumber(&a)) err("failed to read a (cf)");

                eventCommands[lineIndex][EnumPhiEventType::AdditiveAlpha].push_back(Commands::Event {
                    .timeZone = { startTime, endTime }, .value = a,
                    .useFront = true
                });
            }
        }

        std::stable_sort(bpmCommands.begin(), bpmCommands.end(), [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
        std::vector<PhiBPMEvent> sharedBpmEvents;
        for (auto& cmd : bpmCommands) {
            sharedBpmEvents.push_back(PhiBPMEvent {
                .time = cmd.startTime,
                .bpm = cmd.bpm
            });
        }

        std::unordered_map<int64, uint64> lineIndexMap;
        auto getLineByIndex = [&](int64 index) -> PhiLine& {
            if (lineIndexMap.contains(index)) return chart.lines[lineIndexMap[index]];
            PhiLine line {};
            line.bpms = sharedBpmEvents;
            chart.lines.push_back(line);
            lineIndexMap[index] = chart.lines.size() - 1;
            return chart.lines.back();
        };

        auto toSeconds = [&](int64 lineIndex, float64 beatTime) {
            auto& line = getLineByIndex(lineIndex);
            return line.beat2sec(beatTime);
        };

        for (auto& cmd : noteCommands) {
            auto& line = getLineByIndex(cmd.lineIndex);
            auto& note = line.notes.emplace_back(std::move(cmd.note));

            auto time = toSeconds(cmd.lineIndex, note.time);
            auto holdTime = toSeconds(cmd.lineIndex, note.time + note.holdTime) - time;
            note.time = time;
            note.holdTime = holdTime;

            if (!cmd.isAbove) {
                chart.animator.addEvent(note, PhiEvent {
                    .timeZone = INF_TZ,
                    .valueZone = { -1.0, -1.0 },
                    .type = EnumPhiEventType::SpeedCoefficient,
                    .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS_2
                });

                chart.animator.addEvent(note, PhiEvent {
                    .timeZone = INF_TZ,
                    .valueZone = { 180.0, 180.0 },
                    .type = EnumPhiEventType::SelfRotation,
                    .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS_2
                });

                note.reverseCover();
            }

            if (cmd.positionX != 0.0) {
                chart.animator.addEvent(note, PhiEvent {
                    .timeZone = INF_TZ,
                    .valueZone = { cmd.positionX, cmd.positionX },
                    .type = EnumPhiEventType::PositionX,
                    .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                });
            }

            if (cmd.speed != 1.0) {
                chart.animator.addEvent(note, PhiEvent {
                    .timeZone = INF_TZ,
                    .valueZone = { cmd.speed, cmd.speed },
                    .type = EnumPhiEventType::SpeedCoefficient,
                    .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                });
            }

            if (cmd.size != 1.0) {
                chart.animator.addEvent(note, PhiEvent {
                    .timeZone = INF_TZ,
                    .valueZone = { cmd.size, cmd.size },
                    .type = EnumPhiEventType::ScaleX,
                    .layerIndex = PhiEventLayerIndexs::NOTE_ATTRS
                });
            }
        }

        for (auto& [lineIndex, events] : eventCommands) {
            for (auto& [type, typedEvents] : events) {
                std::stable_sort(typedEvents.begin(), typedEvents.end(), [](const auto& a, const auto& b) {
                    if (a.timeZone.x != b.timeZone.x) return a.timeZone.x < b.timeZone.x;
                    if (a.timeZone.y != b.timeZone.y) return a.timeZone.y < b.timeZone.y;
                    return b.useFront;
                });

                for (auto& cmd : typedEvents) {
                    cmd.timeZone.x = toSeconds(lineIndex, cmd.timeZone.x);
                    cmd.timeZone.y = toSeconds(lineIndex, cmd.timeZone.y);

                    if (type == EnumPhiEventType::AdditiveAlpha) cmd.value /= 255.0;
                }

                for (uint64 i = 0; i < typedEvents.size(); i++) {
                    auto& cmd = typedEvents[i];
                    auto& line = getLineByIndex(lineIndex);

                    float64 startValue;
                    if (cmd.useFront && i - 1 >= 0) startValue = typedEvents[i - 1].value;
                    else startValue = cmd.value;

                    if (i < typedEvents.size() - 1) {
                        auto& next = typedEvents[i + 1];
                        if (cmd.timeZone.isZeroZone() && next.timeZone.x == cmd.timeZone.y) continue;
                    }
                    
                    if (cmd.timeZone.isZeroZone()) cmd.timeZone.y += 1.0;

                    PhiEvent e {};
                    e.timeZone = cmd.timeZone;
                    e.valueZone = { startValue, cmd.value };
                    e.type = type;
                    e.layerIndex = PhiEventLayerIndexs::LINE_DEFAULT;

                    if (cmd.easingType > 1) {
                        e.easingFuncContext = (void*)cmd.easingType;
                        e.easingFunc = [](void* ctx, float64 p) { return EaseSet::Phigros::RePhiEdit::easing((uint64)ctx, p); };
                        e.easingIntFunc = [](void* ctx, float64 p) { return EaseSet::Phigros::RePhiEdit::easing_int((uint64)ctx, p); };
                    }

                    chart.animator.addEvent(line, e);
                }
            }
        }

        chart.rawHash = data.getHash();
        return chart;
    }

    PhiChart loadPhiChartFromData(const Data& data) {
        std::vector<std::string> msgs;

        #define try_(func) \
            try { return func(data); } \
            catch (const std::exception& err) { msgs.push_back(err.what()); }
        
        try_(loadPhiChartFromOfficialJson);
        try_(loadPhiChartFromRpeJson);
        try_(loadPhiChartFromPec);

        std::string msg = "failures: \n";
        for (auto& m : msgs) msg += m + "\n";
        throw std::runtime_error(msg);
        return {};

        #undef try
    }

    PhiExtra loadPhiExtraFromJsonData(const Data& data, PhiStoryboardAssets& assets) {
        auto err = [](const std::string& msg) {
            throw std::runtime_error(msg);
        };

        auto jsonRoot = JsonNode::Parse(data);

        if (!jsonRoot.isObject()) err("root is not an object");

        PhiExtra extra {};
        
        auto parseTimeTuple = [](const JsonNode& node, float64* dst) {
            if (!node.isArray()) return false;
            if (node.getArray().size() != 3) return false;

            const auto& arr = node.getArray();
            if (!arr[0].isNumber()) return false;
            if (!arr[1].isNumber()) return false;
            if (!arr[2].isNumber()) return false;

            float64 n1 = arr[0].getNumber(),
                n2 = arr[1].getNumber(),
                n3 = arr[2].getNumber();

            *dst = n1 + n2 / n3;
            return true;
        };

        std::vector<PhiBPMEvent> bpmEvents;

        if (!jsonRoot.hasKey("bpm")) err("missing bpm");
        if (!jsonRoot["bpm"].isArray()) err("bpm is not an array");

        auto& bpmArr = jsonRoot["bpm"].getArray();
        for (auto& bpmEventNode : bpmArr) {
            if (!bpmEventNode.isObject()) err("bpm item is not an object");

            if (!bpmEventNode.hasKey("time")) err("missing time");
            float64 time;
            if (!parseTimeTuple(bpmEventNode["time"], &time)) err("time is not a valid time tuple");

            if (!bpmEventNode.hasKey("bpm")) err("missing bpm");
            if (!bpmEventNode["bpm"].isNumber()) err("bpm is not a number");
            float64 bpm = bpmEventNode["bpm"].getNumber();

            bpmEvents.push_back({
                .time = time,
                .bpm = bpm
            });
        }

        PhiBPMEvent::SortBpmEvents(bpmEvents);

        PhiLine tempLine {};
        tempLine.bpms = bpmEvents;

        auto parseTimeTupleToSecond = [&](const JsonNode& node, float64* dst) {
            if (!parseTimeTuple(node, dst)) return false;
            *dst = tempLine.beat2sec(*dst);
            return true;
        };

        auto parseVectorUniform = [&](JsonNode& node, PhiShaderUniform* dst) {
            if (!node.isArray()) return false;
            auto& arr = node.getArray();
            for (auto& i : arr) {
                if (!i.isNumber()) return false;
            }

            if (!(2 <= arr.size() && arr.size() <= 4)) return false;

            dst->used = arr.size();

            for (uint8 i = 0; i < dst->used; i++) {
                dst->value[i] = arr[i].getNumber();
                if (dst->used >= 3) dst->value[i] /= 255.0;
            }

            return true;
        };

        if (!jsonRoot.hasKey("effects")) err("missing effects");
        if (!jsonRoot["effects"].isArray()) err("effects is not an array");
        auto& effectsNode = jsonRoot["effects"].getArray();

        for (auto& effectNode : effectsNode) {
            if (!effectNode.isObject()) err("effects item is not an object");

            if (!effectNode.hasKey("start")) err("missing start");
            if (!effectNode.hasKey("end")) err("missing end");
            
            float64 startTime, endTime;
            if (!parseTimeTupleToSecond(effectNode["start"], &startTime)) err("start is not a valid time tuple");
            if (!parseTimeTupleToSecond(effectNode["end"], &endTime)) err("end is not a valid time tuple");

            bool isGlobal = false;
            if (effectNode.hasKey("global")) {
                if (!effectNode["global"].isBool()) err("global is not a bool");
                isGlobal = effectNode["global"].getBool();
            }

            std::optional<uint64> targetLine;
            if (effectNode.hasKey("line")) {
                if (!effectNode["line"].isNumber()) err("line is not a number");
                targetLine = effectNode["line"].getNumber();
            }

            uint64 order = 0;
            if (effectNode.hasKey("order")) {
                if (!effectNode["order"].isNumber()) err("order is not a number");
                order = effectNode["order"].getNumber();
            }

            if (!effectNode.hasKey("shader")) err("missing shader");
            if (!effectNode["shader"].isString()) err("shader is not a string");
            auto shaderName = effectNode["shader"].getString();

            auto& item = extra.effects.emplace_back();
            item.timeZone = { startTime, endTime };
            item.targetLine = targetLine;
            item.order = order;
            item.isGlobal = isGlobal;
            item.shaderName = shaderName;

            if (effectNode.hasKey("vars")) {
                if (!effectNode["vars"].isObject()) err("vars is not an object");
                auto& varsNode = effectNode["vars"].getObject();

                for (auto& [uniformName, eventsNode] : varsNode) {
                    auto& layer = item.uniforms[uniformName];

                    if (eventsNode.isArray()) {
                        auto& eventsArr = eventsNode.getArray();
                        if (eventsArr.empty()) err("events array is empty");

                        JsonNode::EnumType eventItemNodeType = eventsArr[0].type;
                        for (auto& node : eventsArr) {
                            if (node.type != eventItemNodeType) err("events array contains different types of nodes");
                        }

                        if (eventItemNodeType == JsonNode::EnumType::Object) {
                            for (auto& eventNode : eventsArr) {
                                if (!eventNode.hasKey("startTime")) err("missing startTime");
                                if (!eventNode.hasKey("endTime")) err("missing endTime");

                                float64 startTime, endTime;
                                if (!parseTimeTupleToSecond(eventNode["startTime"], &startTime)) err("startTime is not a valid time tuple");
                                if (!parseTimeTupleToSecond(eventNode["endTime"], &endTime)) err("endTime is not a valid time tuple");

                                if (!eventNode.hasKey("start")) err("missing start");
                                if (!eventNode.hasKey("end")) err("missing end");
                                if (eventNode["start"].type != eventNode["end"].type) err("start and end are not the same type");

                                Vec2 valueZone;
                                
                                if (eventNode["start"].isNumber()) {
                                    valueZone = assets.requestShaderUniformPair(eventNode["start"].getNumber(), eventNode["end"].getNumber());
                                } else if (eventNode["start"].isArray()) {
                                    PhiShaderUniform startUniform, endUniform;
                                    if (!parseVectorUniform(eventNode["start"], &startUniform)) err("start is not a valid vector uniform");
                                    if (!parseVectorUniform(eventNode["end"], &endUniform)) err("end is not a valid vector uniform");
                                    valueZone = assets.requestShaderUniformPair(startUniform, endUniform);
                                } else err("start and end are not a number or array");

                                uint64 easingType = 1;
                                if (eventNode.hasKey("easingType")) {
                                    if (!eventNode["easingType"].isNumber()) err("easingType is not a number");
                                    easingType = eventNode["easingType"].getNumber();
                                }

                                PhiEvent e {};
                                e.timeZone = { startTime, endTime };
                                e.valueZone = valueZone;
                                e.type = EnumPhiEventType::PhiShaderUniform;
                                e.layerIndex = PhiEventLayerIndexs::SHADER_UNIFORM_DEFAULT;

                                if (easingType > 1) {
                                    e.easingFuncContext = (void*)easingType;
                                    e.easingFunc = [](void* ctx, float64 p) { return EaseSet::Phigros::RePhiEdit::easing((uint64)ctx, p); };
                                }

                                layer.addEvent(e);
                            }
                        } else if (eventItemNodeType == JsonNode::EnumType::Number) {
                            PhiShaderUniform uniform;
                            if (!parseVectorUniform(eventsNode, &uniform)) err("events item is not a valid vector uniform");
                            layer.addEvent({
                                .timeZone = INF_TZ,
                                .valueZone = assets.requestShaderUniformPair(uniform, uniform),
                                .type = EnumPhiEventType::PhiShaderUniform,
                                .layerIndex = PhiEventLayerIndexs::SHADER_UNIFORM_DEFAULT
                            });
                        } else err("events array item is not an object or number");
                    } else if (eventsNode.isNumber()) {
                        layer.addEvent({
                            .timeZone = INF_TZ,
                            .valueZone = assets.requestShaderUniformPair(eventsNode.getNumber(), eventsNode.getNumber()),
                            .type = EnumPhiEventType::PhiShaderUniform,
                            .layerIndex = PhiEventLayerIndexs::SHADER_UNIFORM_DEFAULT
                        });
                    } else err("event(s) is not an array or number");
                }
            }
        }

        return extra;
    }

    struct PhiStoryboardHelpers {
        static std::string nameToPath(const std::string& dir, const std::string& name) {
            return std::filesystem::path(std::format("{}/{}", dir, name))
                .lexically_normal().string();
        }

        static std::unordered_map<std::string, PhiShaderUniform> parseDefaultShaderUniforms(
            const std::string& code
        ) {
            std::vector<std::string> lines;
            splitString(code, lines);

            std::unordered_map<std::string, PhiShaderUniform> result;

            for (auto& line : lines) {
                stripString(line);
                if (!stringIsStartsWith(line, "uniform ")) continue;

                auto s = line.find('%');
                if (s == std::string::npos) continue;

                auto e = line.find('%', s);
                if (e == std::string::npos) continue;

                auto default_str = line.substr(s + 1, e - s - 1);

                std::vector<std::string> value_strs;
                splitString(default_str, value_strs, ',');

                std::vector<float64> values;
                for (auto& value_str : value_strs) {
                    stripString(value_str);
                    try { values.push_back(std::stod(value_str)); }
                    catch (...) { values.push_back(0); }
                }

                if (values.empty() || values.size() > 4) continue;

                s = line.find(';');
                if (s == std::string::npos) continue;
                e = s;
                while (e > 0 && line[e - 1] != ' ') e--;

                result[line.substr(e, s - e)] = PhiShaderUniform(values);
            }

            return result;
        }
    };

    struct ParsedRPEChartInfo {
        std::string name;
        std::string path;
        std::string song;
        std::string picture;
        std::string chart;
        std::string level;
        std::string composer;
        std::string lastEditTime;
        std::string length;
        std::string editTime;
        std::string group;

        static std::vector<ParsedRPEChartInfo> parse(const Data& data) {
            std::vector<ParsedRPEChartInfo> infos;

            auto str = data.toString();
            str.erase(std::remove(str.begin(), str.end(), '\r'), str.end());

            std::vector<std::string> lines;
            splitString(str, lines);

            ParsedRPEChartInfo info {};
            uint64 vaildLineCount = 0;

            for (auto& line : lines) {
                stripString(line);

                if (line.empty()) continue;
                if (line[0] == '#') {
                    if (vaildLineCount) {
                        infos.push_back(info);
                        info = {};
                        vaildLineCount = 0;
                    }
                    continue;
                }

                auto split = line.find(": ");
                if (split == std::string::npos) continue;

                auto key = line.substr(0, split);
                auto value = line.substr(split + 2);

                if (key == "Name") info.name = value;
                else if (key == "Path") info.path = value;
                else if (key == "Song") info.song = value;
                else if (key == "Picture") info.picture = value;
                else if (key == "Chart") info.chart = value;
                else if (key == "Level") info.level = value;
                else if (key == "Composer") info.composer = value;
                else if (key == "LastEditTime") info.lastEditTime = value;
                else if (key == "Length") info.length = value;
                else if (key == "EditTime") info.editTime = value;
                else if (key == "Group") info.group = value;
                vaildLineCount++;
            }

            if (vaildLineCount) {
                infos.push_back(info);
            }

            return infos;
        }
    };

    struct PhiCalculateFrameConfig {
        struct NoteTextureInfo {
            struct Item {
                Vec2 textureSize;
                Vec2 cutPadding;
                Vec2 scaling = { 1.0, 1.0 };
            };

            Item single;
            Item simul;
        };

        Vec2 screenSize;
        Vec2 backgroundTextureSize;
        std::unordered_map<EnumPhiNoteType, NoteTextureInfo> noteTextureInfos;
        float64 songLength;
        float64 maxNoteBodyLength = 8192.0;
    };

    struct PhiCalculatedFrame {
        UsingSharedCalculatedObjects;

        struct CalculatedNote {
            Vec2 position;
            float64 rotation;
            float64 width, head, body, tail;
            EnumPhiNoteType type;
            bool isSimul;
            Color color;
        };

        struct CalculatedStoryboardTexture {
            uint64 texture;
            Vec2 position, size, scale, anchor;
            float64 rotation;
            Color color;
        };

        struct CalculatedHitEffectTexture {
            Vec2 position, size;
            float64 progress, rotation;
            Color color;
        };

        struct CalculatedShader {
            uint64 id;
            std::unordered_map<std::string, PhiShaderUniform> uniforms;
        };

        using CalculatedObject = std::variant<
            ListSharedCalculatedObjects,
            CalculatedNote,
            CalculatedStoryboardTexture,
            CalculatedHitEffectTexture,
            CalculatedShader
        >;

        float64 backgroundImageBlurRadius;
        Rect unsafeBackgroundRect, backgroundRect;
        float64 unsafeAreaDim, backgroundDim;
        Rect objectsClipRect;
        std::vector<CalculatedObject> objects;
        std::unordered_map<EnumPhiNoteType, uint64> hitsounds;

        void culling(std::vector<CalculatedObject>& objects, const Rect& screenRect) noexcept {
            if (SharedCalculatedObjects::sharedCulling(objects, screenRect)) return;

            auto& obj = objects.back();

            if (std::holds_alternative<CalculatedStoryboardTexture>(obj)) {
                auto& tex = std::get<CalculatedStoryboardTexture>(obj);
                if (!quadStrictlyIntersectRect(makeQuadFromRectInfo({
                    .position = tex.position,
                    .size = tex.size * tex.scale,
                    .rotation = tex.rotation,
                    .anchor = tex.anchor
                }).data(), screenRect)) objects.pop_back();
            } else if (std::holds_alternative<CalculatedHitEffectTexture>(obj)) {
                auto& tex = std::get<CalculatedHitEffectTexture>(obj);
                if (!quadStrictlyIntersectRect(makeQuadFromRectInfo({
                    .position = tex.position,
                    .size = tex.size,
                    .rotation = tex.rotation
                }).data(), screenRect)) objects.pop_back();
            }
        }

        void addObject(std::vector<CalculatedObject>& objects, const CalculatedObject& obj, const Rect& screenRect) noexcept {
            objects.push_back(obj);
            culling(objects, screenRect);
        }

        struct Cache {
            struct AttachUIData {
                Vec2 position, scale = { 1.0, 1.0 };
                float64 rotation;
                Color color = { 1.0, 1.0, 1.0, 1.0 };
            };

            std::unordered_map<EnumPhiLineAttachUI, AttachUIData> attachUIDatas;
            std::unordered_map<EnumPhiNoteType, std::vector<CalculatedObject>> noteObjects;

            void clear() {
                attachUIDatas.clear();
                for (auto& [_, v] : noteObjects) v.clear();
            }
        };

        Cache cache;
    };

    void calculatePhiFrame(
        PhiChart& chart, float64 time,
        const PhiCalculateFrameConfig& config,
        PhiCalculatedFrame& frame
    ) {
        frame.objects.clear();
        frame.hitsounds.clear();
        frame.cache.clear();

        float64 screenRatio = config.screenSize.x / config.screenSize.y;
        Rect safeArea = screenRatio > chart.meta.maxViewRatio ? getCoveredOrContainRect(
            { 0.0, 0.0, config.screenSize.x, config.screenSize.y },
            { chart.meta.maxViewRatio, 1.0 }, false
        ) : Rect { 0.0, 0.0, config.screenSize.x, config.screenSize.y };

        auto safeAreaPosition = safeArea.position();
        auto safeAreaSize = safeArea.size();
        auto toScreen = [&](Vec2 pos) { return pos + safeAreaPosition; };

        frame.backgroundImageBlurRadius = config.backgroundTextureSize.sum() * chart.options.backgroundTextureBlurRadius;

        frame.unsafeBackgroundRect = getCoveredOrContainRect(
            { 0.0, 0.0, config.screenSize.x, config.screenSize.y },
            config.backgroundTextureSize, true
        );
        frame.unsafeAreaDim = chart.options.unsafeBackgroundDim;

        frame.backgroundRect = getCoveredOrContainRect(safeArea, config.backgroundTextureSize, true);
        frame.backgroundDim = chart.options.backgroundDim;

        frame.objectsClipRect = safeArea;

        auto processAttachUIText = [&](PhiCalculatedFrame::CalculatedText rawText, EnumPhiLineAttachUI attachUIType) {
            auto& data = frame.cache.attachUIDatas[attachUIType];
            rawText.position += data.position;
            rawText.scale *= data.scale;
            rawText.rotation += data.rotation;
            rawText.color *= data.color;
            return rawText;
        };

        time -= chart.meta.offset;
        
        auto lineWidth = (chart.meta.lineWidthUnit * safeAreaSize).sum();
        auto lineHeight = (chart.meta.lineHeightUnit * safeAreaSize).sum();
        auto standardNoteWidth = safeAreaSize.x * 0.1234375 * chart.options.noteScaling;

        struct NoteTextureSizeInfo {
            float64 width;
            float64 head, body, tail;

            void scale(const Vec2& v) {
                width *= v.x;
                head *= v.y;
                body *= v.y;
                tail *= v.y;
            }

            float64 getHeadHalfDiagonal() const {
                return Vec2 { width, head }.length() / 2;
            }
        };

        auto getNoteTextureSizeInfo = [&](EnumPhiNoteType type, bool isSimul, bool hideHead) {
            const auto& texInfo = (
                isSimul
                ? config.noteTextureInfos.at(type).simul
                : config.noteTextureInfos.at(type).single
            );

            auto width = standardNoteWidth;
            auto totalHeight = width / texInfo.textureSize.x * texInfo.textureSize.y;
            width *= texInfo.scaling.x; totalHeight *= texInfo.scaling.y;
            auto cutPadding = texInfo.cutPadding / texInfo.textureSize.y;
            auto head = hideHead ? 0.0 : cutPadding.x * totalHeight;
            auto tail = cutPadding.y * totalHeight;

            return NoteTextureSizeInfo {
                .width = width,
                .head = head,
                .tail = tail
            };
        };

        float64 maxHalfNoteHeadDiagonal = 0.0;

        for (auto& [type, _] : config.noteTextureInfos) {
            maxHalfNoteHeadDiagonal = std::max({
                maxHalfNoteHeadDiagonal,
                getNoteTextureSizeInfo(type, false, false).getHeadHalfDiagonal(),
                getNoteTextureSizeInfo(type, true, false).getHeadHalfDiagonal()
            });
        }

        chart.state.timeUpdated(time);

        for (auto& lineIndex : chart.zOrderSortedLines) {
            auto& line = chart.lines[lineIndex];

            auto linePosition = chart.getLinePosition(time, line, safeAreaSize);
            auto lineScreenPosition = toScreen(linePosition);
            auto linePositionRelOrigin = chart.getLinePositionRelOrigin(time, line, safeAreaSize);
            auto lineRotation = chart.animator.get(line, time, EnumPhiEventType::SelfRotation);
            auto lineAlpha = chart.animator.get_alpha(line, time, 0.0);
            auto lineTextIndex = chart.animator.get(line, time, EnumPhiEventType::Text);
            auto lineTextIndexZone = chart.animator.get_zone(line, time, EnumPhiEventType::Text);
            auto lineText = chart.storyboardAssets.getText(lineTextIndex, lineTextIndexZone);
            auto lineColorIndex = chart.animator.get(line, time, EnumPhiEventType::Color);
            auto lineColorIndexZone = chart.animator.get_zone(line, time, EnumPhiEventType::Color);
            auto lineColor = chart.storyboardAssets.getColor(
                lineColorIndex,
                (line.attachUI.has_value() || lineText.has_value() || line.textureName.has_value())
                    ? Color::White()
                    : chart.options.lineDefaultColor,
                lineColorIndexZone
            );
            auto lineScale = Vec2 {
                chart.animator.get(line, time, EnumPhiEventType::ScaleX),
                chart.animator.get(line, time, EnumPhiEventType::ScaleY)
            };

            if (line.attachUI.has_value()) {
                frame.cache.attachUIDatas[line.attachUI.value()] = {
                    .position = linePositionRelOrigin,
                    .scale = lineScale,
                    .rotation = lineRotation,
                    .color = lineColor.applyAlpha(lineAlpha)
                };
            }

            if (lineAlpha * lineColor.a > 0) {
                if (lineText.has_value()) {
                    frame.objects.push_back(PhiCalculatedFrame::CalculatedText {
                        .text = lineText.value(),
                        .position = lineScreenPosition,
                        .scale = lineScale,
                        .anchor = line.anchor,
                        .fontSize = (chart.options.storyboardTextBaseSize * safeAreaSize).sum(),
                        .rotation = lineRotation,
                        .color = lineColor.applyAlpha(lineAlpha)
                    });
                } else if (!line.attachUI.has_value()) {
                    if (line.textureName.has_value()) {
                        auto& textureName = line.textureName.value();

                        if (chart.storyboardAssets.isTextureLoaded(textureName)) {
                            auto& texture = chart.storyboardAssets.getTexture(textureName);
                            float64 textureWidth, textureHeight;

                            if (chart.options.storyboardTextureSclaingBehavior == PhiChart::UserOptions::EnumStoryboardTextureSclaingBehavior::AboutWidth) {
                                textureWidth = texture.second.x / std::abs(chart.meta.worldViewport.x) * safeAreaSize.x;
                                textureHeight = textureWidth / texture.second.x * texture.second.y;
                            } else if (chart.options.storyboardTextureSclaingBehavior == PhiChart::UserOptions::EnumStoryboardTextureSclaingBehavior::AboutHeight) {
                                textureHeight = texture.second.y / std::abs(chart.meta.worldViewport.y) * safeAreaSize.y;
                                textureWidth = textureHeight / texture.second.y * texture.second.x;
                            } else if (chart.options.storyboardTextureSclaingBehavior == PhiChart::UserOptions::EnumStoryboardTextureSclaingBehavior::Stretch) {
                                textureWidth = texture.second.x / std::abs(chart.meta.worldViewport.x) * safeAreaSize.x;
                                textureHeight = texture.second.y / std::abs(chart.meta.worldViewport.y) * safeAreaSize.y;
                            } else textureWidth = textureHeight = 0;

                            textureWidth *= chart.options.storyboardTextureScaling.x;
                            textureHeight *= chart.options.storyboardTextureScaling.y;

                            frame.addObject(frame.objects, PhiCalculatedFrame::CalculatedStoryboardTexture {
                                .texture = texture.first,
                                .position = lineScreenPosition,
                                .size = Vec2 { textureWidth, textureHeight },
                                .scale = lineScale,
                                .anchor = line.anchor,
                                .rotation = lineRotation,
                                .color = lineColor.applyAlpha(lineAlpha)
                            }, safeArea);
                        }
                    } else {
                        frame.addObject(frame.objects, PhiCalculatedFrame::CalculatedPoly::Make(
                            Vec2 { -lineWidth, -lineHeight } * line.anchor * lineScale,
                            Vec2 { lineWidth, lineHeight } * lineScale,
                            lineColor.applyAlpha(lineAlpha),
                            Transform2D()
                                .translate(lineScreenPosition)
                                .rotateDegrees(lineRotation)
                        ), safeArea);
                    }
                }
            }

            for (auto& noteGroup : line.noteGroups) {
                noteGroup.state.timeUpdated(time);

                for (uint64 note_ii = noteGroup.state.firstNoteIndex; note_ii < noteGroup.indexs.size(); note_ii++) {
                    auto note_i = noteGroup.indexs[note_ii];
                    auto& note = line.notes[note_i];
                    note.state.timeUpdated(note, time);

                    auto frameInfo = chart.getNoteFrameInfo(line, note, time, safeAreaSize);

                    if (frameInfo.isArrived && note.state.onPlayHitsound()) {
                        if (!note.isFake) {
                            frame.hitsounds[note.type]++;
                        }
                    }

                    if (note.time + note.holdTime < time) {
                        noteGroup.state.passedNoteIndex(note_ii);
                        continue;
                    }

                    auto noteScreenHeadPosition = toScreen(frameInfo.headPosition);
                    auto noteScreenTailPosition = toScreen(frameInfo.tailPosition);

                    auto sizeInfo = getNoteTextureSizeInfo(note.type, note.isSimul, frameInfo.isArrived);
                    sizeInfo.body = std::min(config.maxNoteBodyLength, (noteScreenHeadPosition - noteScreenTailPosition).length());
                    sizeInfo.scale(frameInfo.scale);

                    Transform2D noteTransform;
                    noteTransform.translate(noteScreenHeadPosition);
                    noteTransform.rotateDegrees(frameInfo.textureRotation);
                    noteTransform.scale(1.0, -1.0);

                    Vec2 noteQuad[4] = {
                        noteTransform.transformPoint({ -sizeInfo.width / 2, -sizeInfo.head }),
                        noteTransform.transformPoint({ sizeInfo.width / 2, -sizeInfo.head }),
                        noteTransform.transformPoint({ sizeInfo.width / 2, sizeInfo.body + sizeInfo.tail }),
                        noteTransform.transformPoint({ -sizeInfo.width / 2, sizeInfo.body + sizeInfo.tail })
                    };

                    // 只 hide 不用考虑 maxHalfNoteHeadDiagonal, 但是这里 break 优化也要用
                    auto extendedSafeArea = safeArea.extend(maxHalfNoteHeadDiagonal * frameInfo.scale.max());
                    bool noteInsideScreen = quadStrictlyIntersectRect(noteQuad, extendedSafeArea);

                    if (noteInsideScreen) {
                        if (frameInfo.color.a > 0.0) {
                            frame.cache.noteObjects[note.type].push_back(PhiCalculatedFrame::CalculatedNote {
                                .position = noteScreenHeadPosition,
                                .rotation = frameInfo.textureRotation,
                                .width = sizeInfo.width,
                                .head = sizeInfo.head,
                                .body = sizeInfo.body,
                                .tail = sizeInfo.tail,
                                .type = note.type,
                                .isSimul = note.isSimul,
                                .color = frameInfo.color
                            });
                        }
                    } else {
                        if (noteGroup.breakable) {
                            if (lineIsLeavingScreen(
                                noteScreenHeadPosition,
                                frameInfo.speedVectorRotation + 90.0,
                                extendedSafeArea
                            ) && lineIsLeavingScreen(
                                noteScreenHeadPosition,
                                frameInfo.textureRotation,
                                extendedSafeArea
                            )) break;
                        }
                    }
                }
            }
        }

        for (const auto type : {
            EnumPhiNoteType::Hold,
            EnumPhiNoteType::Drag,
            EnumPhiNoteType::Tap,
            EnumPhiNoteType::Flick
        }) {
            auto& noteObjects = frame.cache.noteObjects[type];
            frame.objects.insert(frame.objects.end(), noteObjects.begin(), noteObjects.end());
        }

        const float64 hitEffectTextureSize = standardNoteWidth * chart.options.hitEffectTextureScaling;

        for (uint64 i = chart.state.firstHitEffectIndex; i < chart.hitEffects.size(); i++) {
            auto& hitEffect = chart.hitEffects[i];
            if (hitEffect.time > time) break;

            auto& line = chart.lines[hitEffect.lineIndex];
            auto& note = line.notes[hitEffect.noteIndex];

            auto info = chart.getNoteFrameInfo(line, note, hitEffect.time, safeAreaSize);
            auto endTime = hitEffect.time + std::max(chart.options.hitEffectDuration, hitEffect.particles.size() ? (hitEffect.particles.back().dt + chart.options.hitEffectDuration) : 0.0);

            if (endTime < time) {
                chart.state.passedHitEffectIndex(i);
                continue;
            }

            auto effectScreenPosition = toScreen(info.headPosition);
            auto progress = (time - hitEffect.time) / chart.options.hitEffectDuration;

            if (progress <= 1.0) {
                frame.addObject(frame.objects, PhiCalculatedFrame::CalculatedHitEffectTexture {
                    .position = effectScreenPosition,
                    .size = { hitEffectTextureSize, hitEffectTextureSize },
                    .progress = progress,
                    .rotation = 0.0,
                    .color = chart.options.lineDefaultColor.applyAlpha(chart.options.hitEffectAlpha)
                }, safeArea);
            }

            for (auto& particle : hitEffect.particles) {
                auto particleTime = hitEffect.time + particle.dt;
                if (particleTime > time) break;
                if (particleTime + chart.options.hitEffectDuration < time) continue;

                auto info = chart.getNoteFrameInfo(line, note, particleTime, safeAreaSize);
                auto effectScreenHeadPosition = toScreen(info.headPosition);
                auto progress = std::clamp((time - particleTime) / chart.options.hitEffectDuration, 0.0, 1.0);
                auto size = standardNoteWidth / 5.3 * chart.options.hitEffectParticleSize * (((0.20783014 * progress - 1.65243926) * progress + 1.6398785) * progress + 0.49884492);
                auto distance = standardNoteWidth / 180 * chart.options.hitEffectParticleDistance * particle.size * (((850.3997391752 * progress + 6236.3848902154) * progress + 80.3542231806) * progress / ((6570.5817658876 * progress + 495.7977913926) * progress + 1.0));

                auto particlePosition = toScreen(info.headPosition.rotateDegrees(particle.rotation, distance));
                frame.addObject(frame.objects, PhiCalculatedFrame::CalculatedRect {
                    .position = particlePosition,
                    .size = { size, size },
                    .rotation = 0.0,
                    .color = chart.options.lineDefaultColor.applyAlpha(chart.options.hitEffectAlpha * (1.0 - progress))
                }, safeArea);
            }
        }

        auto calculateExtra = [&](bool isGlobal) {
            for (auto& effectIndex : chart.extra.zOrderSortedEffects) {
                auto& effect = chart.extra.effects[effectIndex];
                if (effect.isGlobal != isGlobal) continue;
                if (!effect.timeZone.include(time)) continue;

                PhiCalculatedFrame::CalculatedShader shader { .id = effect.shaderId };

                for (auto& [uniformName, layer] : effect.uniforms) {
                    layer.updateType(EnumPhiEventType::PhiShaderUniform, time);
                    auto uniformIndex = layer.get(EnumPhiEventType::PhiShaderUniform);
                    auto uniformIndexZone = layer.get_zone(EnumPhiEventType::PhiShaderUniform).value_or(Vec2 {});
                    auto uniformValue = chart.storyboardAssets.getShaderUniform(uniformIndex, PhiShaderUniform(), uniformIndexZone);
                    shader.uniforms[uniformName] = uniformValue;
                }

                frame.objects.push_back(shader);
            }
        };

        calculateExtra(false);

        auto combo = chart.getCombo(time);

        time += chart.meta.offset;
        float64 songPorgress = time / config.songLength;

        float64 progressBarHeight = safeAreaSize.x * 0.005921;
        float64 progressBarWidth = safeAreaSize.x * songPorgress;
        float64 progressBarPointWidth = safeAreaSize.x * 0.00175;

        auto& progressBarAttachUIData = frame.cache.attachUIDatas[EnumPhiLineAttachUI::Bar];

        frame.objects.push_back(PhiCalculatedFrame::CalculatedPoly::Make(
            { 0.0, 0.0 },
            { progressBarWidth, progressBarHeight },
            chart.options.progressBarDefaultColor.first * progressBarAttachUIData.color,
            Transform2D()
                .translate(safeAreaPosition)
                .translate(progressBarAttachUIData.position)
                .scale(progressBarAttachUIData.scale)
                .rotateDegrees(progressBarAttachUIData.rotation)
        ));

        frame.objects.push_back(PhiCalculatedFrame::CalculatedPoly::Make(
            { progressBarWidth - progressBarPointWidth, 0.0 },
            { progressBarPointWidth, progressBarHeight },
            chart.options.progressBarDefaultColor.second * progressBarAttachUIData.color,
            Transform2D()
                .translate(safeAreaPosition)
                .translate(progressBarAttachUIData.position)
                .scale(progressBarAttachUIData.scale)
                .rotateDegrees(progressBarAttachUIData.rotation)
        ));

        auto pauseButtonPosition = Vec2 { 3.16669, 3.6065 } * progressBarHeight;
        auto pauseButtonSize = Vec2 { safeAreaSize.x * 32 / 1920, safeAreaSize.x * 37.48 / 1920 };
        float64 pauseButtonItemWidth = pauseButtonSize.x * 0.323;

        auto& pauseButtonAttachUIData = frame.cache.attachUIDatas[EnumPhiLineAttachUI::Pause];

        frame.objects.push_back(PhiCalculatedFrame::CalculatedPoly::Make(
            { 0.0, 0.0 },
            { pauseButtonItemWidth, pauseButtonSize.y },
            pauseButtonAttachUIData.color,
            Transform2D()
                .translate(safeAreaPosition)
                .translate(pauseButtonPosition)
                .translate(pauseButtonAttachUIData.position)
                .scale(pauseButtonAttachUIData.scale)
                .rotateDegrees(pauseButtonAttachUIData.rotation)
        ));

        frame.objects.push_back(PhiCalculatedFrame::CalculatedPoly::Make(
            { pauseButtonSize.x - pauseButtonItemWidth, 0.0 },
            { pauseButtonItemWidth, pauseButtonSize.y },
            pauseButtonAttachUIData.color,
            Transform2D()
                .translate(safeAreaPosition)
                .translate(pauseButtonPosition)
                .translate(pauseButtonAttachUIData.position)
                .scale(pauseButtonAttachUIData.scale)
                .rotateDegrees(pauseButtonAttachUIData.rotation)
        ));

        if (combo >= 3) {
            frame.objects.push_back(processAttachUIText(PhiCalculatedFrame::CalculatedText {
                .text = std::to_string(combo),
                .position = toScreen({ safeAreaSize.x / 2, safeAreaSize.x * 0.027083 }),
                .anchor = { 0.5, 0.5 },
                .fontSize = safeAreaSize.x * 0.0393081
            }, EnumPhiLineAttachUI::ComboNumber));

            frame.objects.push_back(processAttachUIText(PhiCalculatedFrame::CalculatedText {
                .text = "AUTOPLAY",
                .position = toScreen({ safeAreaSize.x / 2, safeAreaSize.x * 0.0478125 }),
                .anchor = { 0.5, 0.0 },
                .fontSize = safeAreaSize.x * 0.0130208
            }, EnumPhiLineAttachUI::Combo));
        }

        uint64 score = chart.comboTimes.size() ? std::clamp<float64>(std::ceil((float64)1000000 / chart.comboTimes.size() * combo), 0, 1000000) : 1000000;
        frame.objects.push_back(processAttachUIText(PhiCalculatedFrame::CalculatedText {
            .text = std::format("{:07}", score),
            .position = toScreen({ safeAreaSize.x * (1 - ((float64)40 / 1920)), safeAreaSize.x * 0.01614583 }),
            .anchor = { 1.0, 0.0 },
            .fontSize = safeAreaSize.x * 0.0277778
        }, EnumPhiLineAttachUI::Score));
        
        frame.objects.push_back(processAttachUIText(PhiCalculatedFrame::CalculatedText {
            .text = chart.meta.title,
            .position = toScreen({ safeAreaSize.x * 0.0225, safeAreaSize.y - safeAreaSize.x * 0.0196875 }),
            .anchor = { 0.0, 1.0 },
            .fontSize = safeAreaSize.x * 0.018115942
        }, EnumPhiLineAttachUI::Name));
        
        frame.objects.push_back(processAttachUIText(PhiCalculatedFrame::CalculatedText {
            .text = chart.meta.difficulty,
            .position = toScreen({ safeAreaSize.x * 0.9775, safeAreaSize.y - safeAreaSize.x * 0.0196875 }),
            .anchor = { 1.0, 1.0 },
            .fontSize = safeAreaSize.x * 0.018115942
        }, EnumPhiLineAttachUI::Level));

        calculateExtra(true);
    }

    struct PhiTakeOverer {
        PhiTakeOverer() = default;
        PhiTakeOverer(const PhiTakeOverer&) = delete;
        PhiTakeOverer(PhiTakeOverer&&) = delete;
        PhiTakeOverer& operator=(const PhiTakeOverer&) = delete;
        PhiTakeOverer& operator=(PhiTakeOverer&&) = delete;

        static gsp<PhiTakeOverer> Make() {
            auto* tor = new PhiTakeOverer();
            return gsp<PhiTakeOverer>(tor);
        }

        struct NoteTextureDataLoaderConfig {
            EnumPhiNoteType type;
            bool isSimul;
        };

        struct NoteTextureDataLoaderResult {
            Data encoded;
            Vec2 cutPadding;
            bool cutPaddingIsPixel = true;
            bool ignoreCutPadding;
            Vec2 scaling = { 1.0, 1.0 };
        };

        using NoteTextureDataLoader = std::function<NoteTextureDataLoaderResult(const NoteTextureDataLoaderConfig&)>;
        NoteTextureDataLoader noteTextureDataLoader;

        using HitEffectDataLoader = std::function<std::vector<Data>()>;
        HitEffectDataLoader hitEffectDataLoader;

        using HitsoundDataLoader = std::function<Data(EnumPhiNoteType)>;
        HitsoundDataLoader hitsoundDataLoader;

        using StoryboardDataLoader = std::function<Data(const std::string&)>;
        StoryboardDataLoader storyboardDataLoader;

        using ShaderDataLoader = std::function<std::string(const std::string&)>;
        ShaderDataLoader shaderDataLoader;

        gsp<GL::GL33Context> glCtx;
        TakeOvererComponents::SharedComp sharedComp;
        GL::TextManager textManager;
        TakeOvererComponents::AudioManager audioManager;

        // 这个放在后面会因为析构顺序相反崩溃, 即先析构这个再析构 chart, chart 回来再用这个就炸了
        private:
        std::unordered_map<uint64, gsp<GL::TextureInfo>> storyboardTextures;
        public:

        PhiCalculateFrameConfig calcConfig;
        PhiChart chart;
        PhiCalculatedFrame calculatedFrame;

        void init() {
            gassert::asrt(!!noteTextureDataLoader, "PhiTakeOverer: noteTextureDataLoader is not set");
            gassert::asrt(!!hitEffectDataLoader, "PhiTakeOverer: hitEffectDataLoader is not set");
            gassert::asrt(!!hitsoundDataLoader, "PhiTakeOverer: hitsoundDataLoader is not set");
            gassert::asrt(!!storyboardDataLoader, "PhiTakeOverer: storyboardDataLoader is not set");
            gassert::asrt(!!shaderDataLoader, "PhiTakeOverer: shaderDataLoader is not set");
            gassert::asrt(!!glCtx, "PhiTakeOverer: glCtx is not set");

            textManager.glCtx = glCtx;

            sharedComp.check();
            textManager.check();
            audioManager.check();

            loadResources();
        }

        void loadIllustion(const Data& data) {
            auto decoded = sharedComp.textureDecoder(data);
            sharedComp.illustionTexture = glCtx->createTextureFromDecoded(decoded, true);
            bluredIllustionCache.key = -1.0;
        }

        void loadIllustion(const std::string& path) { loadIllustion(Data::MakeFromFile(path)); }

        struct MixBgmConfig {
            float64 musicVol = 1.0, sfxVol = 1.0;
            bool sfxRandshake = false;
        };

        gsp<DecodedAudio> mixFinalBgm(const PhiChart& chart, const MixBgmConfig& config) {
            if (!audioManager.bgmAudio) throw std::runtime_error("bgm is not loaded");

            auto result = audioManager.bgmAudio->copy();
            result->applyVolume(config.musicVol);
            
            std::mt19937 rng { std::random_device {} () };
            std::uniform_real_distribution<float64> sfxRandshakeDist { 0.0, 0.02 };

            for (const auto& line : chart.lines) {
                for (const auto& note : line.notes) {
                    if (note.isFake) continue;

                    float64 t = note.time + chart.meta.offset;
                    if (config.sfxRandshake) t += sfxRandshakeDist(rng);

                    auto sfx = hitsoundAudios.at(note.type);
                    result->overlapSecond(sfx, t, config.sfxVol);
                }
            }

            return result;
        }

        using ChartIniter = std::function<void(PhiChart&)>;

        struct LoadChartConfig {
            Data data;
            ChartIniter initer = [](PhiChart& chart) { chart.init(); };
            std::optional<Data> extraData;
        };

        TakeOvererComponents::LoadChartResultInfo loadChart(const LoadChartConfig& config) {
            TakeOvererComponents::LoadChartResultInfo resultInfo {};

            {
                gtime::Timer timer;

                try {
                    chart = loadPhiChartFromData(config.data);
                } catch (const std::exception& e) {
                    resultInfo.success = false;
                    resultInfo.error = e.what();
                    return resultInfo;
                }

                resultInfo.createObjectTook = timer.elapsed();
            }

            uint64 storyboardTextureId = 0;

            chart.storyboardAssets.clearTextures();

            chart.storyboardAssets.textureLoader = [&, this](const std::string& name) {
                auto data = storyboardDataLoader(name);
                auto decoded = sharedComp.textureDecoder(data);
                auto tex = glCtx->createTextureFromDecoded(decoded, true);
                auto id = storyboardTextureId++;
                storyboardTextures[id] = tex;
                return std::make_pair(id, Vec2 { (float64)decoded.width, (float64)decoded.height });
            };

            chart.storyboardAssets.textureDestroyer = [this](uint64 id) {
                storyboardTextures.erase(id);
            };

            chart.storyboardAssets.shaderPreloader = [this](const std::string& name, uint64 id) {
                auto shaderString = shaderDataLoader(name);

                if (shaderString.empty()) {
                    throw std::runtime_error("shader string is empty: " + name);
                }

                try {
                    auto prog = glCtx->createConfiguredProgram(GL::GL33Context::CreateProgramConfig {
                        .vertCode = R"(
#version 100

attribute vec2 inPosition;
attribute vec2 inTexCoord;

varying vec2 uv;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    uv = inTexCoord;
}
)",
                        .fragCode = shaderString,
                        .vertConfigurer = [](GL::ProgramInfo* prog, GL::VertexArrayInfo* vao, GL::BufferInfo* vbo) {
                            using namespace GL;
                            auto vaoGuard = vao->use();
                            auto vboGuard = vbo->use();
                            auto inPosition = prog->getAttribLocationPosition("inPosition");
                            auto inTexCoord = prog->getAttribLocationPosition("inTexCoord");
                            vaoGuard.enable(inPosition);
                            vaoGuard.enable(inTexCoord);
                            vaoGuard.pointer(inPosition, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
                            vaoGuard.pointer(inTexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
                        }
                    });

                    prog->fragConfig.textureUniformName = "screenTexture";
                    prog->fragConfig.colorUniformName = std::nullopt;
                    shaders[id] = prog;
                } catch (const std::exception& e) {
                    throw std::runtime_error("failed to load shader: " + name + "\n" + e.what());
                }

                auto defaultUnfiroms = PhiStoryboardHelpers::parseDefaultShaderUniforms(shaderString);
                shadersDefaultUniforms[id] = defaultUnfiroms;
            };

            shaders.clear();
            shadersDefaultUniforms.clear();

            if (config.extraData.has_value()) {
                chart.extra = loadPhiExtraFromJsonData(config.extraData.value(), chart.storyboardAssets);
            }

            {
                gtime::Timer timer;
                config.initer(chart);
                resultInfo.initTook = timer.elapsed();
            }

            return resultInfo;
        }

        struct RenderConfig {
            TakeOvererComponents::RenderConfigBase base;
        };

        struct RenderResultInfo {
            TakeOvererComponents::RenderResultInfoBase base;
        };

        RenderResultInfo& render(const RenderConfig& renderConfig) {
            calcConfig.songLength = audioManager.getBgmLength();
            calcConfig.backgroundTextureSize = { sharedComp.illustionTexture->width, sharedComp.illustionTexture->height };

            auto t = renderConfig.base.getTime(audioManager);

            {
                gtime::Timer timer;
                calculatePhiFrame(chart, t, calcConfig, calculatedFrame);
                renderResultInfoCache.base.calculatedTook = timer.elapsed();
            }

            gtime::Timer glOpsTimer;

            using namespace GL;

            auto illuTex = bluredIllustionCache.get(calculatedFrame.backgroundImageBlurRadius, [&](float64 radius) {
                auto tex = glCtx->createTexture();
                glCtx->copyTexture(sharedComp.illustionTexture.get(), tex.get());
                glCtx->gaussianBlurToTexture(tex.get(), radius);
                return tex;
            });

            glCtx->setViewport(calcConfig.screenSize.x, calcConfig.screenSize.y);
            glCtx->gl.glClearColor(0.0, 0.0, 0.0, 0.0);
            glCtx->gl.glClear(GL_COLOR_BUFFER_BIT);

            auto cvs = GL33Canvas::Make(glCtx.get());

            cvs.drawRect({
                .position = { calculatedFrame.unsafeBackgroundRect.x, calculatedFrame.unsafeBackgroundRect.y },
                .size = { calculatedFrame.unsafeBackgroundRect.w, calculatedFrame.unsafeBackgroundRect.h },
                .color = GLvec4::Gray(1.0 - calculatedFrame.unsafeAreaDim),
                .texture = illuTex.get()
            });

            glCtx->setViewport(
                calculatedFrame.objectsClipRect.x, calculatedFrame.objectsClipRect.y,
                calculatedFrame.objectsClipRect.w, calculatedFrame.objectsClipRect.h
            );

            cvs.drawRect({
                .position = { calculatedFrame.backgroundRect.x, calculatedFrame.backgroundRect.y },
                .size = { calculatedFrame.backgroundRect.w, calculatedFrame.backgroundRect.h },
                .color = GLvec4::Gray(1.0 - calculatedFrame.backgroundDim),
                .texture = illuTex.get()
            });

            for (auto& obj : calculatedFrame.objects) {
                if (TakeOvererComponents::renderSharedObject(obj, glCtx, cvs, textManager)) {
                    continue;
                }
                    
                if (std::holds_alternative<PhiCalculatedFrame::CalculatedNote>(obj)) {
                    auto& note = std::get<PhiCalculatedFrame::CalculatedNote>(obj);
                    auto& img = note.isSimul ? noteTextures[note.type].second : noteTextures[note.type].first;
                    auto& imgInfo = note.isSimul ? calcConfig.noteTextureInfos[note.type].simul : calcConfig.noteTextureInfos[note.type].single;

                    cvs.save();
                    cvs.translate(note.position);
                    cvs.rotateDegrees(note.rotation);

                    auto mesh = glCtx->requestMesh(6 * 3);
                    mesh.color = note.color;
                    mesh.texture = img.get();

                    mesh.addRect(
                        { -note.width / 2, 0.0 }, { note.width, note.head },
                        GLvec2 { 0.0, imgInfo.textureSize.y - imgInfo.cutPadding.x } / imgInfo.textureSize,
                        GLvec2 { imgInfo.textureSize.x, imgInfo.cutPadding.x } / imgInfo.textureSize
                    );

                    mesh.addRect(
                        { -note.width / 2, -note.body },
                        { note.width, note.body },
                        GLvec2 { 0.0, imgInfo.cutPadding.y } / imgInfo.textureSize,
                        GLvec2 { imgInfo.textureSize.x, imgInfo.textureSize.y - imgInfo.cutPadding.sum() } / imgInfo.textureSize
                    );

                    mesh.addRect(
                        { -note.width / 2, -note.body - note.tail },
                        { note.width, note.tail },
                        GLvec2 { 0.0, 0.0 },
                        GLvec2 { imgInfo.textureSize.x, imgInfo.cutPadding.y } / imgInfo.textureSize
                    );

                    cvs.drawMesh(mesh);
                    cvs.restore();
                } else if (std::holds_alternative<PhiCalculatedFrame::CalculatedStoryboardTexture>(obj)) {
                    auto& sbTexture = std::get<PhiCalculatedFrame::CalculatedStoryboardTexture>(obj);
                    auto& img = storyboardTextures[sbTexture.texture];

                    cvs.save();
                    cvs.translate(sbTexture.position);
                    cvs.rotateDegrees(sbTexture.rotation);
                    cvs.scale(sbTexture.scale);
                    cvs.drawRect({
                        .position = -sbTexture.size * sbTexture.anchor,
                        .size = sbTexture.size,
                        .color = sbTexture.color,
                        .texture = img.get()
                    });
                    cvs.restore();
                } else if (std::holds_alternative<PhiCalculatedFrame::CalculatedHitEffectTexture>(obj)) {
                    auto& effect = std::get<PhiCalculatedFrame::CalculatedHitEffectTexture>(obj);
                    auto& img = hitEffectTextures[std::clamp<uint64>(effect.progress * hitEffectTextures.size(), 0, hitEffectTextures.size() - 1)];

                    cvs.save();
                    cvs.translate(effect.position);
                    cvs.rotateDegrees(effect.rotation);
                    cvs.drawRect({
                        .position = -effect.size / 2,
                        .size = effect.size,
                        .color = effect.color,
                        .texture = img.get()
                    });
                    cvs.restore();
                } else if (std::holds_alternative<PhiCalculatedFrame::CalculatedShader>(obj)) {
                    auto& shader = std::get<PhiCalculatedFrame::CalculatedShader>(obj);
                    auto& prog = shaders[shader.id];
                    if (!prog) continue;

                    {
                        auto guard = prog->use();

                        for (auto& [k, v] : shadersDefaultUniforms[shader.id]) v.setToGlLocation(prog->getUniformLocation(k));
                        for (auto& [k, v] : shader.uniforms) v.setToGlLocation(prog->getUniformLocation(k));

                        prog->getUniformLocation("screenSize").setf(calcConfig.screenSize.x, calcConfig.screenSize.y);
                        prog->getUniformLocation("time").setf(t);
                    }

                    auto mesh = glCtx->requestMesh(6);
                    mesh.program = prog.get();
                    mesh.color = GLvec4::White();
                    glCtx->renderToDrawFbo(calcConfig.screenSize.x, calcConfig.screenSize.y, mesh);
                }
            }

            if (renderConfig.base.flushGl) {
                glCtx->gl.glFlush();
            }

            renderResultInfoCache.base.glOperationsTook = glOpsTimer.elapsed();

            if (!renderConfig.base.disableHitsound) {
                for (auto& [type, count] : calculatedFrame.hitsounds) {
                    audioManager.playSfx(hitsoundAudios.at(type), count);
                }
            }

            return renderResultInfoCache;
        }

        private:
        SKVCache<float64, gsp<GL::TextureInfo>> bluredIllustionCache;
        std::unordered_map<EnumPhiNoteType, std::pair<gsp<GL::TextureInfo>, gsp<GL::TextureInfo>>> noteTextures;
        std::vector<gsp<GL::TextureInfo>> hitEffectTextures;
        std::unordered_map<EnumPhiNoteType, gsp<DecodedAudio>> hitsoundAudios;
        RenderResultInfo renderResultInfoCache;
        std::unordered_map<uint64, gsp<GL::ProgramInfo>> shaders;
        std::unordered_map<uint64, std::unordered_map<std::string, PhiShaderUniform>> shadersDefaultUniforms;

        void loadResources() {
            noteTextures.clear();
            hitEffectTextures.clear();
            storyboardTextures.clear();

            for (const auto type : {
                EnumPhiNoteType::Tap, EnumPhiNoteType::Drag,
                EnumPhiNoteType::Flick, EnumPhiNoteType::Hold
            }) {
                noteTextures[type] = { nullptr, nullptr };
                calcConfig.noteTextureInfos[type] = {};

                for (const auto isSimul : { false, true }) {
                    auto loadResult = noteTextureDataLoader(NoteTextureDataLoaderConfig {
                        .type = type,
                        .isSimul = isSimul
                    });

                    auto decoded = sharedComp.textureDecoder(loadResult.encoded);
                    auto tex = glCtx->createTextureFromDecoded(decoded, true);
                    if (!loadResult.cutPaddingIsPixel) loadResult.cutPadding *= decoded.height;
                    if (loadResult.ignoreCutPadding) loadResult.cutPadding = Vec2 { (float64)decoded.height, (float64)decoded.height } / 2;

                    if (!isSimul) noteTextures[type].first = tex;
                    else noteTextures[type].second = tex;

                    PhiCalculateFrameConfig::NoteTextureInfo::Item item {
                        .textureSize = Vec2 { (float64)decoded.width, (float64)decoded.height },
                        .cutPadding = loadResult.cutPadding,
                        .scaling = loadResult.scaling
                    };

                    if (!isSimul) calcConfig.noteTextureInfos[type].single = item;
                    else calcConfig.noteTextureInfos[type].simul = item;
                }

                auto& info = calcConfig.noteTextureInfos[type];
                auto simulScale = (float64)info.simul.textureSize.x / info.single.textureSize.x;
                info.simul.scaling = { simulScale, simulScale };
            }

            auto hitEffectDatas = hitEffectDataLoader();
            for (const auto& data : hitEffectDatas) {
                auto decoded = sharedComp.textureDecoder(data);
                auto tex = glCtx->createTextureFromDecoded(decoded, true);
                hitEffectTextures.push_back(tex);
            }

            for (const auto type : {
                EnumPhiNoteType::Tap, EnumPhiNoteType::Drag,
                EnumPhiNoteType::Flick, EnumPhiNoteType::Hold
            }) {
                auto data = hitsoundDataLoader(type);
                hitsoundAudios[type] = audioManager.decodeAndCheck(data);
            }
        }
    };

    enum class EnumMilEventType : uint64 {
        PositionX, PositionY,
        Transparency, Size, Rotation,
        FlowSpeed,
        RelativeX, RelativeY,
        LineBodyTransparency, LineHeadTransparency,
        StoryBoardWidth, StoryBoardHeight,
        Speed,
        WholeTransparency,
        StoryBoardLeftBottomX, StoryBoardLeftBottomY,
        StoryBoardRightBottomX, StoryBoardRightBottomY,
        StoryBoardLeftTopX, StoryBoardLeftTopY,
        StoryBoardRightTopX, StoryBoardRightTopY,
        Color,
        VisibleArea,
        MAX = VisibleArea + 1
    };

    enum class EnumMilObjectType : uint64 {
        Line, Note, Storyboard,
        MAX = Storyboard + 1
    };

    enum class EnumMilNoteType {
        Hit, Drag, Fracture
    };

    enum class EnumMilFinalNoteType {
        Tap, Hold, Drag, Fracture
    };

    enum class EnumMilStoryboardType {
        Picture, Text
    };

    enum class EnumMilStoryboardLayer {
        Background, Normal, Foreground
    };

    struct MilEventTypeHelper {
        static EnumMilEventType FromInt(uint64 type) {
            if (type == 0) return EnumMilEventType::PositionX;
            if (type == 1) return EnumMilEventType::PositionY;
            if (type == 2) return EnumMilEventType::Transparency;
            if (type == 3) return EnumMilEventType::Size;
            if (type == 4) return EnumMilEventType::Rotation;
            if (type == 5) return EnumMilEventType::FlowSpeed;
            if (type == 6) return EnumMilEventType::RelativeX;
            if (type == 7) return EnumMilEventType::RelativeY;
            if (type == 8) return EnumMilEventType::LineBodyTransparency;
            if (type == 9) return EnumMilEventType::LineHeadTransparency;
            if (type == 10) return EnumMilEventType::StoryBoardWidth;
            if (type == 11) return EnumMilEventType::StoryBoardHeight;
            if (type == 12) return EnumMilEventType::Speed;
            if (type == 13) return EnumMilEventType::WholeTransparency;
            if (type == 14) return EnumMilEventType::StoryBoardLeftBottomX;
            if (type == 15) return EnumMilEventType::StoryBoardLeftBottomY;
            if (type == 16) return EnumMilEventType::StoryBoardRightBottomX;
            if (type == 17) return EnumMilEventType::StoryBoardRightBottomY;
            if (type == 18) return EnumMilEventType::StoryBoardLeftTopX;
            if (type == 19) return EnumMilEventType::StoryBoardLeftTopY;
            if (type == 20) return EnumMilEventType::StoryBoardRightTopX;
            if (type == 21) return EnumMilEventType::StoryBoardRightTopY;
            if (type == 22) return EnumMilEventType::Color;
            if (type == 23) return EnumMilEventType::VisibleArea;
            return EnumMilEventType::PositionX;
        }
    };

    struct MilObjectTypeHelper {
        static EnumMilObjectType FromInt(uint64 type) {
            if (type == 0) return EnumMilObjectType::Line;
            if (type == 1) return EnumMilObjectType::Note;
            if (type == 2) return EnumMilObjectType::Storyboard;
            return EnumMilObjectType::Line;
        }
    };

    struct MilNoteTypeHelper {
        static EnumMilNoteType FromInt(uint64 type) {
            if (type == 0) return EnumMilNoteType::Hit;
            if (type == 1) return EnumMilNoteType::Drag;
            if (type == 2) return EnumMilNoteType::Fracture;
            return EnumMilNoteType::Hit;
        }
    };

    struct MilStoryboardTypeHelper {
        static EnumMilStoryboardType FromInt(uint64 type) {
            if (type == 0) return EnumMilStoryboardType::Picture;
            if (type == 1) return EnumMilStoryboardType::Text;
            return EnumMilStoryboardType::Picture;
        }
    };

    struct MilStoryboardLayerHelper {
        static EnumMilStoryboardLayer FromInt(uint64 type) {
            if (type == 0) return EnumMilStoryboardLayer::Background;
            if (type == 1) return EnumMilStoryboardLayer::Normal;
            if (type == 2) return EnumMilStoryboardLayer::Foreground;
            return EnumMilStoryboardLayer::Background;
        }
    };

    struct MilMeta {
        std::string title;
        std::string composer;
        std::string artist;
        std::string charter;
        std::string difficultyName;
        float64 difficultyValue;

        enum class NoteFlowSpeedBehavior {
            Override, Multiply, Add
        };

        NoteFlowSpeedBehavior noteFlowSpeedBehavior;

        Vec2 worldOrigin, worldViewport;
        float64 speedUnit;
        float64 holdDisappearTime = 0.2;

        std::string getFinalDifficultyString() {
            auto ret = difficultyName + " ";
            ret += std::to_string((int64)difficultyValue);
            if (std::fmod(difficultyValue, 1.0) != 0.0) ret += "+";
            return ret;
        }
    };

    struct MilBPMEvent {
        float64 time;
        float64 bpm;
    };

    struct MilEventLayerIndexs {
        static constexpr uint64 UNIT = 1000000;

        static constexpr uint64 DEFAULT = UNIT * 1;
    };

    struct MilEvent {
        Vec2 timeZone;
        Vec2 valueZone;
        EnumMilEventType type;

        float64 (* easingFunc)(void*, float64);
        float64 (* easingIntFunc)(void*, float64);
        void* easingFuncContext;
        uint64 index;

        float64 cumulativeValueAtStart;

        static float64 getDefaultValue(EnumMilObjectType objType, EnumMilEventType eventType) {
            static std::unordered_map<EnumMilObjectType, std::unordered_map<EnumMilEventType, float64>> defaultValues = {
                { EnumMilObjectType::Line, {
                    { EnumMilEventType::PositionY, -350 },
                    { EnumMilEventType::Transparency, 1 },
                    { EnumMilEventType::Size, 1 },
                    { EnumMilEventType::Rotation, 90 },
                    { EnumMilEventType::FlowSpeed, 1 },
                    { EnumMilEventType::LineBodyTransparency, 1 },
                    { EnumMilEventType::LineHeadTransparency, 1 },
                    { EnumMilEventType::Speed, 1 },
                    { EnumMilEventType::WholeTransparency, 1 },
                    { EnumMilEventType::VisibleArea, (float64)2500 / 1080 }
                } },
                { EnumMilObjectType::Note, {
                    { EnumMilEventType::Transparency, 1 },
                    { EnumMilEventType::Size, 1 },
                    { EnumMilEventType::FlowSpeed, 1 },
                } },
                { EnumMilObjectType::Storyboard, {
                    { EnumMilEventType::Transparency, 1 },
                    { EnumMilEventType::Size, 1 },
                    { EnumMilEventType::StoryBoardWidth, 1 },
                    { EnumMilEventType::StoryBoardHeight, 1 },
                    { EnumMilEventType::StoryBoardLeftBottomX, -0.5 },
                    { EnumMilEventType::StoryBoardLeftBottomY, -0.5 },
                    { EnumMilEventType::StoryBoardRightBottomX, 0.5 },
                    { EnumMilEventType::StoryBoardRightBottomY, -0.5 },
                    { EnumMilEventType::StoryBoardLeftTopX, -0.5 },
                    { EnumMilEventType::StoryBoardLeftTopY, 0.5 },
                    { EnumMilEventType::StoryBoardRightTopX, 0.5 },
                    { EnumMilEventType::StoryBoardRightTopY, 0.5 },
                } }
            };

            return defaultValues[objType][eventType];
        }

        float64 getProgressAtTime(float64 t) const noexcept {
            if (timeZone.isZeroZone()) return 1.0;
            return std::clamp((t - timeZone.x) / (timeZone.y - timeZone.x), 0.0, 1.0);
        }

        float64 valueAtTime(float64 t) const noexcept {
            auto p = getProgressAtTime(t);

            if (hasValueEasing()) {
                p = easingFunc(easingFuncContext, p);
            }

            return valueZone.x + p * (valueZone.y - valueZone.x);
        }

        float64 getIntegralValue(float64 t) const noexcept {
            auto p = getProgressAtTime(t);
            float64 iv = p * p / 2.0;

            if (hasIntEasing()) {
                iv = easingIntFunc(easingFuncContext, p);
            }

            float64 res = (timeZone.y - timeZone.x) * (valueZone.x * p + (valueZone.y - valueZone.x) * iv);
            if (t > timeZone.y) res += valueZone.y * (t - timeZone.y);
            if (t < timeZone.x) res -= valueZone.x * (timeZone.x - t);
            return res;
        }

        private:
        bool hasValueEasing() const noexcept { return easingFunc != nullptr; }
        bool hasIntEasing() const noexcept { return easingIntFunc != nullptr; }
    };

    struct MilAnimGroup {
        std::vector<MilEvent> events[(uint64)EnumMilEventType::MAX];
        EnumMilObjectType objType;

        void addEvent(const MilEvent& e) { events[(uint64)e.type].push_back(e); }

        void init() {
            std::ranges::fill(lastUpdatedTimes, -std::numeric_limits<float64>::infinity());

            for (uint64 i = 0; i < (uint64)EnumMilEventType::MAX; i++) {
                auto& typedEvents = events[i];

                std::sort(typedEvents.begin(), typedEvents.end(), [](const auto& a, const auto& b) {
                    if (a.timeZone.x != b.timeZone.x) return a.timeZone.x < b.timeZone.x;
                    if (a.timeZone.y != b.timeZone.y) return a.timeZone.y < b.timeZone.y;
                    return a.index < b.index;
                });

                if (typedEvents.empty()) {
                    auto value = MilEvent::getDefaultValue(objType, (EnumMilEventType)i);
                    currentValues[i] = value;
                    currentValueZones[i] = Vec2(value);
                }
            }

            initSpeedCumul();
        }

        void updateType(uint64 type, float64 t) noexcept {
            auto& typedEvents = events[type];

            if (typedEvents.empty()) {
                if (type == (uint64)EnumMilEventType::Speed) {
                    currentValues[type] = MilEvent::getDefaultValue(objType, (EnumMilEventType)type) * t;
                }

                return;
            }

            if (lastUpdatedTimes[type] == t) return;
            if (lastUpdatedTimes[type] > t) rewindTo(type, t);
            
            while (shouldAdvanceToNext(type, t)) currentIndexs[type]++;

            auto& e = typedEvents[currentIndexs[type]];

            if (type == (uint64)EnumMilEventType::Speed) {
                currentValues[type] = e.cumulativeValueAtStart + e.getIntegralValue(t);
            } else {
                currentValues[type] = e.valueAtTime(t);
            }

            currentValueZones[type] = e.valueZone;
            lastUpdatedTimes[type] = t;
        }

        float64 get(EnumMilEventType type) const noexcept {
            return currentValues[(uint64)type];
        }

        std::optional<float64> getAlwaysValue(EnumMilEventType type) noexcept {
            auto& typedEvents = events[(uint64)type];
            if (typedEvents.empty()) return MilEvent::getDefaultValue(objType, type);

            if (type == EnumMilEventType::Speed) {
                if (typedEvents.size() == 1 && typedEvents[0].valueZone.isZeroZone()) {
                    return typedEvents[0].valueZone.x;
                }

                return std::nullopt;
            }

            float64 fixedValue = typedEvents[0].valueZone.x;
            for (auto& e : typedEvents) {
                if (e.timeZone.isZeroZone()) {
                    if (e.valueZone.y != fixedValue) {
                        return std::nullopt;
                    }

                    continue;
                }

                if (!e.valueZone.isZeroZone() || fixedValue != e.valueZone.x) {
                    return std::nullopt;
                }
            }

            return fixedValue;
        }

        bool has(EnumMilEventType type) const noexcept {
            return !events[(uint64)type].empty();
        }

        Vec2 get_zone(EnumMilEventType type) const noexcept {
            return currentValueZones[(uint64)type];
        }

        private:
        float64 lastUpdatedTimes[(uint64)EnumMilEventType::MAX];
        uint64 currentIndexs[(uint64)EnumMilEventType::MAX];
        float64 currentValues[(uint64)EnumMilEventType::MAX];
        Vec2 currentValueZones[(uint64)EnumMilEventType::MAX];

        void initSpeedCumul() {
            auto& speedEvents = events[(uint64)EnumMilEventType::Speed];
            if (speedEvents.empty()) return;

            float64 cumulativeValue = speedEvents[0].timeZone.x * speedEvents[0].valueZone.x;

            for (uint64 i = 0; i < speedEvents.size(); i++) {
                auto& e = speedEvents[i];
                e.cumulativeValueAtStart = cumulativeValue;

                if (i < speedEvents.size() - 1) {
                    cumulativeValue += e.getIntegralValue(speedEvents[i + 1].timeZone.x);
                }
            }
        }

        bool shouldAdvanceToNext(uint64 type, float64 t) const noexcept {
            return (
                currentIndexs[type] < events[type].size() - 1
                && events[type][currentIndexs[type]].timeZone.y <= t
                && events[type][currentIndexs[type] + 1].timeZone.x <= t
            );
        }

        void rewindTo(uint64 type, float64 t) noexcept {
            while (!shouldAdvanceToNext(type, t) && currentIndexs[type] > 0) {
                currentIndexs[type]--;
            }
        }
    };

    struct MilAnimator {
        using ObjDesc = std::pair<EnumMilObjectType, uint64>;

        ObjectIndexGenerator<ObjDesc> indexGen;
        std::unordered_map<uint64, MilAnimGroup> groups;

        MilAnimGroup& requestGroup(const ObjDesc& obj) {
            auto& ret = groups.try_emplace(indexGen.get(obj), MilAnimGroup { }).first->second;
            ret.objType = obj.first;
            return ret;
        }

        void addEvent(const ObjDesc& obj, const MilEvent& e) {
            requestGroup(obj).addEvent(e);
        }

        void init() {
            for (auto& [_, group] : groups) {
                group.init();
            }
        }

        float64 get(const ObjDesc& obj, float64 t, EnumMilEventType type) noexcept {
            auto group_it = groups.find(obj.second);

            if (group_it == groups.end()) {
                auto value = MilEvent::getDefaultValue(obj.first, type);
                if (type == EnumMilEventType::Speed) value *= t;
                return value;
            }

            auto& group = group_it->second;
            group.updateType((uint64)type, t);
            return group.get(type);
        }

        template <typename T>
        float64 get(T& obj, float64 t, EnumMilEventType type) noexcept {
            return get({ T::ObjType, obj.indexer.get() }, t, type);
        }

        template <typename T>
        std::optional<float64> getNoteAnimHash(T& obj) {
            HashBucket hash;

            auto group_it = groups.find(obj.indexer.get());

            for (const auto type : {
                EnumMilEventType::PositionX,
                EnumMilEventType::PositionY,
                EnumMilEventType::Size,
                EnumMilEventType::Rotation,
                EnumMilEventType::FlowSpeed,
                EnumMilEventType::RelativeX,
                EnumMilEventType::RelativeY,
                EnumMilEventType::Speed
            }) {
                if (group_it == groups.end()) {
                    hash.submitNumber(MilEvent::getDefaultValue(T::ObjType, type));
                } else {
                    auto v = group_it->second.getAlwaysValue(type);
                    if (!v.has_value()) return std::nullopt;
                    hash.submitNumber(v.value());
                }
            }

            return hash.hash;
        }

        bool has(const ObjDesc& obj, EnumMilEventType type) const noexcept {
            auto group_it = groups.find(obj.second);
            if (group_it == groups.end()) return false;

            return group_it->second.has(type);
        }

        template <typename T>
        bool has(T& obj, EnumMilEventType type) const noexcept {
            return has({ T::ObjType, obj.indexer.get() }, type);
        }

        Vec2 get_zone(const ObjDesc& obj, float64 t, EnumMilEventType type) noexcept {
            auto group_it = groups.find(obj.second);
            if (group_it == groups.end()) return Vec2(MilEvent::getDefaultValue(obj.first, type));

            auto& group = group_it->second;
            group.updateType((uint64)type, t);
            return group.get_zone(type);
        }

        template <typename T>
        Vec2 get_zone(T& obj, float64 t, EnumMilEventType type) noexcept {
            return get_zone({ T::ObjType, obj.indexer.get() }, t, type);
        }
    };

    // type, isHold, isSimul, isAlwaysPerfect
    using MilNoteTextureDesc = std::tuple<EnumMilNoteType, bool, bool, bool, bool>;
    static constexpr uint64 MilNoteTextureDescAttrType = 0;
    static constexpr uint64 MilNoteTextureDescAttrIsHold = 1;
    static constexpr uint64 MilNoteTextureDescAttrIsSimul = 2;
    static constexpr uint64 MilNoteTextureDescAttrIsAlwaysPerfect = 3;
    static constexpr uint64 MilNoteTextureDescAttrIsActive = 4;

    // [hold 判定材质过渡] 未判定 = builtin.hold.png(暗、弱蓝描边); 判定后 = builtin.hold_active.png(强白辉光)
    // 两种材质几何同构(cutPadding/textureSize 比例均为 0.16551, scaling 均为 1.77)
    // 过渡时长取自本家 libil2cpp.so(arm64) HoldController.UpdateGraphics(RVA 0x4187574) 的立即数
    // 0.4f(字面量池 @0xda1748): activeTime 每帧 ±deltaTime 累加并 clamp 到 [0, 0.4], mix = activeTime / 0.4
    static constexpr float64 MilHoldActivateMixSeconds = 0.4;

    bool fallbackMilNoteTextureDesc(MilNoteTextureDesc& desc) {
        if (std::get<MilNoteTextureDescAttrIsActive>(desc)) {
            std::get<MilNoteTextureDescAttrIsActive>(desc) = false;
            return true;
        }
        if (std::get<MilNoteTextureDescAttrIsAlwaysPerfect>(desc)) {
            std::get<MilNoteTextureDescAttrIsAlwaysPerfect>(desc) = false;
            return true;
        }

        if (std::get<MilNoteTextureDescAttrIsHold>(desc)) {
            std::get<MilNoteTextureDescAttrIsHold>(desc) = false;
            return true;
        }

        if (std::get<MilNoteTextureDescAttrIsSimul>(desc)) {
            std::get<MilNoteTextureDescAttrIsSimul>(desc) = false;
            return true;
        }

        return false;
    }

    struct MilNote {
        ObjectIndexer indexer;
        static constexpr auto ObjType = EnumMilObjectType::Note;

        struct State {
            float64 lastUpdateTime;
            bool playedHitsound;

            void timeUpdated(const MilNote& note, float64 t) noexcept {
                if (lastUpdateTime > t) {
                    playedHitsound = note.timeZone.x < t;
                }

                lastUpdateTime = t;
            }

            bool onPlayHitsound() noexcept {
                if (!playedHitsound) {
                    playedHitsound = true;
                    return true;
                }

                return false;
            }
        };

        EnumMilNoteType type;
        Vec2 timeZone;
        bool isFake, isAlwaysPerfect;

        uint64 lineIndex;
        Vec2 floorPosition;
        bool isSimul;
        MilNoteTextureDesc textureDesc;
        EnumMilFinalNoteType finalType;

        State state;

        void init(MilAnimator& animator) {
            floorPosition = { getFloorPositionAt(timeZone.x, animator), getFloorPositionAt(timeZone.y, animator) };
            textureDesc = MilNoteTextureDesc(type, isHold(), isSimul, isAlwaysPerfect, false);

            if (type == EnumMilNoteType::Hit) {
                finalType = isHold() ? EnumMilFinalNoteType::Hold : EnumMilFinalNoteType::Tap;
            } else if (type == EnumMilNoteType::Fracture) {
                finalType = EnumMilFinalNoteType::Fracture;
            } else {
                finalType = EnumMilFinalNoteType::Drag;
            }
        }

        float64 getFloorPositionAt(float64 t, MilAnimator& animator) noexcept {
            return animator.get({ EnumMilObjectType::Line, lineIndex }, t, EnumMilEventType::Speed) + animator.get(*this, t, EnumMilEventType::Speed);
        }

        // Fracture 由本家自动判定(AutoHit), 玩家不能击中; 且谱面里恒为单点(from==to), 绝不能进 hold 逻辑
        bool isFracture() const noexcept {
            return type == EnumMilNoteType::Fracture;
        }

        bool isHold() const noexcept {
            return !isFracture() && !timeZone.isZeroZone() && type == EnumMilNoteType::Hit;
        }
    };

    struct MilNoteGroup {
        struct State {
            float64 lastUpdateTime;
            uint64 firstNoteIndex;

            void timeUpdated(float64 t) noexcept {
                if (lastUpdateTime > t) {
                    firstNoteIndex = 0;
                }

                lastUpdateTime = t;
            }

            void passedNoteIndex(uint64 index) noexcept {
                if (firstNoteIndex == index) {
                    firstNoteIndex++;
                }
            }
        };

        std::vector<uint64> indexs;
        bool breakable = true;

        State state;
    };

    struct MilLine {
        ObjectIndexer indexer;
        static constexpr auto ObjType = EnumMilObjectType::Line;

        std::vector<MilNote> notes;

        std::vector<MilNoteGroup> noteGroups;

        void init(MilAnimator& animator) {
            std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) {
                return a.timeZone.x < b.timeZone.x;
            });

            noteGroups.emplace_back().breakable = false;
            std::unordered_map<uint64, uint64> noteGroupMap;

            for (uint64 i = 0; i < notes.size(); i++) {
                auto& note = notes[i];
                note.lineIndex = indexer.get();
                note.init(animator);
                
                auto hash = animator.getNoteAnimHash(note);
                if (hash.has_value()) {
                    if (!noteGroupMap.contains(hash.value())) {
                        noteGroups.emplace_back();
                        noteGroupMap[hash.value()] = noteGroups.size() - 1;
                    }

                    auto& group = noteGroups[noteGroupMap[hash.value()]];
                    group.indexs.push_back(i);
                } else noteGroups[0].indexs.push_back(i);
            }
        }
    };

    struct MilStoryboardObject {
        ObjectIndexer indexer;
        static constexpr auto ObjType = EnumMilObjectType::Storyboard;

        EnumMilStoryboardType type;
        std::string data;
        EnumMilStoryboardLayer layer;
    };

    struct MilStoryboardAssets {
        static constexpr uint64 kColorIndexOffset = 1;

        std::vector<Color> colors;

        Vec2 requestColorPair(const Color& start, const Color& end) {
            Vec2 valueZone;
            if (colors.empty() || colors[colors.size() - 1] != start) colors.push_back(start);
            valueZone.x = colors.size() - 1;
            if (colors.empty() || colors[colors.size() - 1] != end) colors.push_back(end);
            valueZone.y = colors.size() - 1;
            return valueZone + kColorIndexOffset;
        }

        Color getColor(float64 index, const Vec2& valueZone) noexcept {
            if (valueZone.x < kColorIndexOffset) return Color::White();

            auto start = colors[(uint64)valueZone.x - kColorIndexOffset];
            auto end = colors[(uint64)valueZone.y - kColorIndexOffset];
            auto p = index - valueZone.x;
            return start * (1.0 - p) + end * p;
        }
    };

    struct MilHitEffectItem {
        struct Particle {
            float64 dt;
            float64 rotate;
            float64 initialSpeed;
            float64 initialSize;
            Vec2 scale;
            float64 gravCoeff;

            float64 getRadius(float64 p) const noexcept {
                return p * initialSpeed * initialSpeed * (p * p / 3.0 - p + 1);
            }

            float64 getDeltaY(float64 p) const noexcept {
                return p * p * gravCoeff * 0.025;
            }

            Vec2 getScale(float64 p) const noexcept {
                return Vec2 {
                    std::pow(p + 1, -scale.x) * 1.34,
                    std::pow(p + 1, -scale.y) * 0.25
                } / (p + 1);
            }
        };

        float64 time;
        float64 texRotation;
        uint64 lineIndex, noteIndex;
        std::vector<Particle> particles;

        float64 getEndTime(float64 duration) const noexcept {
            return (particles.empty() ? time : time + particles.back().dt) + duration;
        }
    };

    // ==== Fracture 撞击特效 ====
    // 实现已抽成独立可注入模块 mil_fx_emitter.hpp (零依赖, 可直接移植到其他工程)
    // 规格来源: APK 反解的粒子预置体, 全表见 frac_fx_ref/fx_spec.md
    using MilFractureFxLayer = milfx::Layer;
    enum {
        MilFxFxVerticalStrike = milfx::LayerVerticalStrike,
        MilFxFxLongLightning  = milfx::LayerLongLightning,
        MilFxFxExplode        = milfx::LayerExplode,
        MilFxFxPass           = milfx::LayerPass,
    };
    inline constexpr uint64 MilFxFxLayerCount = (uint64)milfx::LayerCount;

    // 四层全开, 与本家规格一致: VerticalStrike(1) + LongLightning(6) + Explode(14) + Pass(25)。
    // 2026-09-22: 之前只发 Pass 层 -> 本家命中时那个巨大的白色爆闪全丢了, 画面上只剩
    // 暗淡小粒子 + hitEffect 蓝紫细环, 看起来像"气泡"。主体其实是 Explode 的 14 条
    // 放射长闪电: 长度 = size*speed*lengthScale = 0.15*5*5 = 3.75 unity ≈ 237 px。
    inline constexpr bool milFxLayerEnabled[MilFxFxLayerCount] = { true, true, true, true };

    // 渲染器侧「整体缩放」: 等比作用于 粒子尺寸 + 散布半径 + 位移, 粒子的相对比例与形态完全不变。
    // 本家 = 1.0(不缩放): fxPixelScale=63.32 已经是本家原样(FracturePass 的 Transform.scale=1.5
    // 已含在模块 spec.scale 里), Pass 粒子原始直径只有 2.3~6.8 px、散布半径 27 px。
    // 🔴 2026-09-22: 之前设 2.0 / exe 用 2.6 是错的。放大后每颗粒子变成 6~20 px 的可见方块
    // (lightning2 是 512x128 横长贴图, Billboard 会把 4:1 压成 1:1 正方形), 在纯黑背景上
    // 看着像一颗颗半透明"气泡" —— 本家没有这种东西。已改回 1.0。
    // 模块 mil_fx_emitter.hpp 保持 1:1 按 APK 不变。对照图 frac_fx_ref/fxscale_*.png。
    inline constexpr float64 MilFxFxGlobalScale = 1.0;

    // 临时: 允许环境变量 MILFX_SCALE 覆盖倍率, 一次编译扫多档标定
    inline float64 milFxFxGlobalScaleRuntime() noexcept {
        static float64 v = [] () -> float64 {
            if (auto* s = std::getenv("MILFX_SCALE")) {
                float64 f = std::atof(s);
                if (f > 0.0) return f;
            }
            return MilFxFxGlobalScale;
        }();
        return v;
    }

    // ColorOverLifetime / SizeOverLifetime / 层参数表全部在 mil_fx_emitter.hpp
    inline Color milFractureFxColorAt(uint64 layer, float64 p) noexcept {
        milfx::Fx c[4];
        milfx::colorAt(milfx::kColorTracks[layer], p, c);
        return Color { c[0], c[1], c[2], c[3] };
    }

    inline float64 milFxSizeCurveAt(const MilFractureFxLayer& spec, float64 p) noexcept {
        return milfx::sizeCurveAt(spec, p);
    }

    // Unity 世界单位 -> 渲染像素。Fracture 精灵在 Unity 里 m_Size=(1.87,1.87), 渲染器绘制该音符
    // 用 lineHeadBase*1.77 px, 故 1 unit = lineHeadBase*1.77/1.87 px。
    // 锚在音符自身尺寸上(音符已 1:1 验收), 比按参考视频标定可靠。
    inline float64 milFractureUnitScaleOf(float64 lineHeadBase) noexcept {
        return lineHeadBase * 1.77 / 1.87;
    }

    // 粒子类型与层参数全部来自 mil_fx_emitter.hpp; 这里只按层分组存放
    using MilFractureFxParticle = milfx::Particle;

    struct MilFractureFx {
        float64 time;
        uint64 lineIndex, noteIndex;
        std::vector<MilFractureFxParticle> particles[MilFxFxLayerCount];

        float64 getEndTime() const noexcept {
            float64 e = time;
            for (auto& g : particles)
                for (auto& p : g) e = std::max(e, time + p.life);
            return e;
        }
    };

    struct MilChart {
        struct State {
            float64 lastUpdateTime;
            uint64 firstHitEffectIndex;
            uint64 firstFractureFxIndex = 0;

            TimeBasedAnim scoreAnim = { 
                .duration = 0.15
            };

            TimeBasedAnim comboScaleAnim = {
                .duration = 0.15,
                .easing = [](auto, float64 t) { return 1.0 + 0.07 * std::sin(t * std::numbers::pi); }
            };

            void timeUpdated(float64 t) noexcept {
                if (lastUpdateTime > t) {
                    firstHitEffectIndex = 0;
                    firstFractureFxIndex = 0;
                    scoreAnim.reset();
                }

                lastUpdateTime = t;
            }

            void passedHitEffectIndex(uint64 index) noexcept {
                if (firstHitEffectIndex == index) {
                    firstHitEffectIndex++;
                }
            }

            void passedFractureFxIndex(uint64 index) noexcept {
                if (firstFractureFxIndex == index) {
                    firstFractureFxIndex++;
                }
            }
        };

        struct UserOptions {
            float64 noteScaling = 1.0;
            float64 flowSpeed = 1.66;

            float64 backgroundDim = 0.8;

            float64 hitEffectDuration = 0.5;

            ColorLink particleRGBColor = {
                .steps = {
                    { 0.0,  Color { 142, 197, 252 } / 255 },
                    { 0.75, Color { 162, 66,  255 } / 255 }
                }
            };

            ColorLink particleAlphaColor = {
                .steps = {
                    { 0.0,   Color { 0, 0, 0, 0 } },
                    { 0.128, Color { 0, 0, 0, 1 } },
                    { 0.805, Color { 0, 0, 0, 1 } },
                    { 1.0,   Color { 0, 0, 0, 0 } }
                }
            };
        };

        MilMeta meta;
        std::vector<MilLine> lines;
        std::vector<MilStoryboardObject> storyboardObjects;
        MilAnimator animator;
        MilStoryboardAssets storyboardAssets;
        
        std::vector<MilHitEffectItem> hitEffects;
        std::vector<MilFractureFx> fractureFxs;
        std::vector<float64> comboTimes;
        uint64 rawHash;

        UserOptions options;

        State state;

        void init() {
            animator.init();

            initSimulNote();

            for (auto& line : lines) {
                line.init(animator);
            }

            initHitEffects();
            initPlayemntInfo();
            initFractureFxs();
        }

        template <typename T>
        Vec2 getObjectPosition(float64 t, T& obj, const Vec2& screenSize) {
            Vec2 pos = {
                animator.get(obj, t, EnumMilEventType::PositionX) + animator.get(obj, t, EnumMilEventType::RelativeX),
                animator.get(obj, t, EnumMilEventType::PositionY) + animator.get(obj, t, EnumMilEventType::RelativeY)
            };

            return (pos - meta.worldOrigin) / meta.worldViewport * screenSize;
        }

        struct NoteFrameInfo {
            Vec2 headPosition, tailPosition;
            bool isArrived;
            float64 textureRotation, speedVectorRotation;
            Vec2 scale;
            Color color;

            void setHidden() noexcept {
                color.a = 0.0;
            }
        };

        NoteFrameInfo getNoteFrameInfo(
            MilLine& line, MilNote& note,
            float64 time, const Vec2& screenSize
        ) noexcept {
            NoteFrameInfo info {};

            auto linePosition = getObjectPosition(time, line, screenSize);
            auto lineRotation = animator.get(line, time, EnumMilEventType::Rotation);
            auto lineFlowSpeed = animator.get(line, time, EnumMilEventType::FlowSpeed);
            auto lineSize = animator.get(line, time, EnumMilEventType::Size);
            auto lineWholeAlpha = animator.get(line, time, EnumMilEventType::WholeTransparency);
            auto lineVisibleArea = animator.get(line, time, EnumMilEventType::VisibleArea);
            auto noteSize = animator.get(note, time, EnumMilEventType::Size);
            auto noteAlpha = animator.get(note, time, EnumMilEventType::Transparency);
            auto noteRotation = animator.get(note, time, EnumMilEventType::Rotation);
            auto noteColorIndex = animator.get(note, time, EnumMilEventType::Color);
            auto noteColorIndexZone = animator.get_zone(note, time, EnumMilEventType::Color);
            auto noteColor = storyboardAssets.getColor(noteColorIndex, noteColorIndexZone);
            
            Transform2D lineTransform {};
            lineTransform.translate(linePosition);
            lineTransform.rotateDegrees(90 - lineRotation);
            lineTransform.scale(screenSize / meta.worldViewport.abs());
            lineTransform.scale(lineSize);
            lineTransform.scale(1.0, -1.0);

            info.isArrived = time >= note.timeZone.x;

            auto finalFlowSpeed = lineFlowSpeed;
            if (meta.noteFlowSpeedBehavior == MilMeta::NoteFlowSpeedBehavior::Override) {
                if (animator.has(note, EnumMilEventType::FlowSpeed)) {
                    finalFlowSpeed = animator.get(note, time, EnumMilEventType::FlowSpeed);
                }
            } else if (meta.noteFlowSpeedBehavior == MilMeta::NoteFlowSpeedBehavior::Multiply) {
                finalFlowSpeed *= animator.get(note, time, EnumMilEventType::FlowSpeed);
            } else if (meta.noteFlowSpeedBehavior == MilMeta::NoteFlowSpeedBehavior::Add) {
                finalFlowSpeed += animator.get(note, time, EnumMilEventType::FlowSpeed);
            }

            auto noteFloorPosition = (note.floorPosition - note.getFloorPositionAt(std::min(time, note.timeZone.y), animator)) * finalFlowSpeed * meta.speedUnit * options.flowSpeed;

            if (time >= note.timeZone.x) {
                noteFloorPosition.x = 0.0;
            }
            
            if (noteFloorPosition.x > lineVisibleArea) {
                info.setHidden();
            }

            if (animator.has(note, EnumMilEventType::PositionY)) {
                noteFloorPosition -= noteFloorPosition.x;
                noteFloorPosition += animator.get(note, time, EnumMilEventType::PositionY);
            }

            Vec2 noteBasePosition = {
                animator.get(note, time, EnumMilEventType::RelativeX) + animator.get(note, time, EnumMilEventType::PositionX),
                animator.get(note, time, EnumMilEventType::RelativeY)
            };

            auto noteRelPositionHead = noteBasePosition + Vec2 { 0.0, noteFloorPosition.x },
                noteRelPositionTail = noteBasePosition + Vec2 { 0.0, noteFloorPosition.y };
            
            info.headPosition = lineTransform.transformPoint(noteRelPositionHead);
            info.tailPosition = lineTransform.transformPoint(noteRelPositionTail);
            info.textureRotation = -lineRotation - noteRotation;
            info.speedVectorRotation = -lineRotation;
            if (finalFlowSpeed < 0) info.speedVectorRotation += 180.0;
            info.scale = Vec2(lineSize * noteSize);
            info.color = noteColor.applyAlpha(lineWholeAlpha * noteAlpha);

            return info;
        }

        uint64 getCombo(float64 t) const noexcept {
            if (comboTimes.empty() || comboTimes[0] > t) return 0;

            uint64 left = 0, right = comboTimes.size() - 1;
            uint64 ans = 1;

            while (left <= right) {
                uint64 mid = left + (right - left) / 2;
                if (comboTimes[mid] <= t) {
                    ans = mid + 1;
                    left = mid + 1;
                } else {
                    right = mid - 1;
                }
            }

            return ans;
        }

        private:
        void initSimulNote() {
            std::unordered_map<float64, uint64> noteTimes;

            for (auto& line : lines) {
                for (auto& note : line.notes) {
                    noteTimes[note.timeZone.x]++;
                }
            }

            for (auto& line : lines) {
                for (auto& note : line.notes) {
                    note.isSimul = noteTimes[note.timeZone.x] > 1;
                }
            }
        }

        void initHitEffects() {
            std::mt19937 rng { std::random_device {} () };
            std::uniform_real_distribution<float64> rng_dist { 0.0, 1.0 };
            auto uniform = [&](float64 a, float64 b) { return a + (b - a) * rng_dist(rng); };

            auto makeParticle = [&](MilHitEffectItem& item, float64 dt = 0.0) {
                auto& particle = item.particles.emplace_back();
                particle.dt = dt;
                particle.rotate = uniform(0.0, 360.0);
                particle.initialSpeed = uniform(0.3, 0.72);
                particle.initialSize = std::pow(particle.initialSpeed, 0.22) * uniform(0.6, 0.7) / 42;
                particle.scale = { uniform(1.5, 2.1), uniform(-0.5, 0.5) };
                particle.gravCoeff = uniform(0.9, 1.3);
            };

            hitEffects.clear();

            for (uint64 i = 0; i < lines.size(); i++) {
                auto& line = lines[i];
                for (uint64 j = 0; j < line.notes.size(); j++) {
                    auto& note = line.notes[j];
                    if (note.isFake) continue;
                    // Fracture = AutoHit 音符: 无判定环、无常规打击粒子; 撞击走 FractureExplode 特效(见 initFractureFxs)
                    if (note.isFracture()) continue;

                    auto& item = hitEffects.emplace_back();
                    item.time = note.timeZone.x;
                    item.texRotation = uniform(0.0, 360.0);
                    item.lineIndex = i;
                    item.noteIndex = j;

                    if (!note.isHold()) {
                        for (uint64 i = 0; i < 10; i++) {
                            makeParticle(item);
                        }
                    } else {
                        // [防爆] hold 粒子按 0.01s 步长铺满全程; 若时长非法(NaN/Inf/负)则 while 永不 break
                        // -> 无限 emplace_back 撑爆内存(2026-09-15 灯ノ桜蝶 实测 >1.2GB + std::bad_alloc)。
                        // 兜底: 非有限/负时长按 0 处理; 单 note 粒子上限 4096(=40s) 防病态谱面。
                        constexpr uint64 maxHoldParticles = 4096;
                        float64 dur = note.timeZone.y - note.timeZone.x;
                        if (!std::isfinite(dur) || dur < 0) dur = 0;
                        uint64 pcount = (uint64)(dur / 0.01);
                        if (pcount > maxHoldParticles) pcount = maxHoldParticles;
                        for (uint64 k = 0; k < pcount; k++) {
                            makeParticle(item, 0.01 * (float64)k);
                        }
                    }
                }
            }

            std::stable_sort(hitEffects.begin(), hitEffects.end(), [](const auto& a, const auto& b) {
                return a.time < b.time;
            });
        }

        void initPlayemntInfo() {
            for (auto& line : lines) {
                for (auto& note : line.notes) {
                    if (note.isFake) continue;
                    comboTimes.push_back(note.timeZone.x);
                    if (note.isHold()) comboTimes.push_back(note.timeZone.y);
                }
            }

            std::sort(comboTimes.begin(), comboTimes.end());
        }

        // Fracture 撞击特效: 按 FractureExplode 预置体四层规格生成确定性粒子。
        // 触发时刻 = note 落到判定线的时刻(note.timeZone.x), 与 AutoHit 结算同刻。
        void initFractureFxs() {
            fractureFxs.clear();

            for (uint64 i = 0; i < lines.size(); i++) {
                auto& line = lines[i];
                for (uint64 j = 0; j < line.notes.size(); j++) {
                    auto& note = line.notes[j];
                    if (note.isFake) continue;
                    if (!note.isFracture()) continue;

                    auto& fx = fractureFxs.emplace_back();
                    fx.time = note.timeZone.x;
                    fx.lineIndex = i;
                    fx.noteIndex = j;

                    // 种子只依赖 (line, note, layer, k) -> 同一帧多次计算结果一致, --dumpmil 可回归
                    auto seedBase = (i * 1000003ull) ^ (j * 9176ull);
                    for (uint64 layer = 0; layer < MilFxFxLayerCount; layer++) {
                        if (!milFxLayerEnabled[layer]) continue;
                        milfx::build(fx.particles[layer], milfx::kLayers[layer], layer, seedBase);
                    }
                }
            }

            std::stable_sort(fractureFxs.begin(), fractureFxs.end(), [](const auto& a, const auto& b) {
                return a.time < b.time;
            });
        }
    };

    MilChart loadMilChartFromDevJson(const Data& data) {
        auto err = [](const std::string& msg) {
            throw std::runtime_error(std::format("dev: {}", msg));
        };
        auto jsonRoot = JsonNode::Parse(data);
        MilChart chart {};
        chart.meta.noteFlowSpeedBehavior = MilMeta::NoteFlowSpeedBehavior::Override;
        chart.meta.worldOrigin = { (float64)-1920 / 2, (float64)1080 / 2 };
        chart.meta.worldViewport = { 1920, -1080 };
        chart.meta.speedUnit = 108.0;
        if (!jsonRoot.isObject()) err("root is not an object");
        if (!jsonRoot.hasKey("meta")) err("missing meta");
        if (!jsonRoot["meta"].isObject()) err("meta is not an object");
        auto& metaNode = jsonRoot["meta"];
        if (!metaNode.hasKey("Title")) err("missing Title");
        if (!metaNode["Title"].isString()) err("Title is not a string");
        chart.meta.title = metaNode["Title"].getString();
        if (!metaNode.hasKey("Composer")) err("missing Composer");
        if (!metaNode["Composer"].isString()) err("Composer is not a string");
        chart.meta.composer = metaNode["Composer"].getString();
        if (!metaNode.hasKey("Illustrator")) err("missing Illustrator");
        if (!metaNode["Illustrator"].isString()) err("Illustrator is not a string");
        chart.meta.artist = metaNode["Illustrator"].getString();
        if (!metaNode.hasKey("Beatmapper")) err("missing Beatmapper");
        if (!metaNode["Beatmapper"].isString()) err("Beatmapper is not a string");
        chart.meta.charter = metaNode["Beatmapper"].getString();
        if (!metaNode.hasKey("Difficulty")) err("missing Difficulty");
        if (!metaNode["Difficulty"].isString()) err("Difficulty is not a string");
        chart.meta.difficultyName = metaNode["Difficulty"].getString();
        if (!metaNode.hasKey("DifficultyValue")) err("missing DifficultyValue");
        if (!metaNode["DifficultyValue"].isNumber()) err("DifficultyValue is not a number");
        chart.meta.difficultyValue = metaNode["DifficultyValue"].getNumber();
        std::vector<MilBPMEvent> bpms;
        if (!jsonRoot.hasKey("bpms")) err("missing bpms");
        if (!jsonRoot["bpms"].isArray()) err("bpms is not an array");
        for (auto& bpmNode : jsonRoot["bpms"].getArray()) {
            if (!bpmNode.isObject()) err("bpm is not an object");
            if (!bpmNode.hasKey("start")) err("missing start");
            if (!bpmNode["start"].isNumber()) err("start is not a number");
            float64 start = bpmNode["start"].getNumber();
            if (!bpmNode.hasKey("bpm")) err("missing bpm");
            if (!bpmNode["bpm"].isNumber()) err("bpm is not a number");
            float64 bpm = bpmNode["bpm"].getNumber();
            bpms.push_back({ .time = start, .bpm = bpm });
        }
        auto cvtTime = [&](JsonNode& node, const std::string& key, uint64 bpm, float64* dst) {
            if (!node.hasKey(key)) return false;
            auto& timeNode = node[key];
            if (timeNode.isNumber()) { *dst = timeNode.getNumber(); return true; }
            if (timeNode.isArray()) {
                auto& timeArray = timeNode.getArray();
                if (timeArray.size() != 3 && timeArray.size() != 4) return false;
                if (!timeArray[0].isNumber() || !timeArray[1].isNumber() || !timeArray[2].isNumber()) return false;
                if (timeArray.size() == 4 && !timeArray[3].isNumber()) return false;
                float64 beatTime = timeArray[0].getNumber() + timeArray[1].getNumber() / timeArray[2].getNumber();
                auto& bpmEvent = bpms[timeArray.size() == 3 ? bpm : (uint64)timeArray[3].getNumber()];
                *dst = bpmEvent.time + beatTime * (60.0 / bpmEvent.bpm);
                return true;
            }
            return false;
        };
        if (!jsonRoot.hasKey("lines")) err("missing lines");
        if (!jsonRoot["lines"].isArray()) err("lines is not an array");
        uint64 lineIndex = 0;
        for (auto& lineNode : jsonRoot["lines"].getArray()) {
            if (!lineNode.isObject()) err("line is not an object");
            auto& line = chart.lines.emplace_back();
            line.indexer.set(chart.animator.indexGen.get({ EnumMilObjectType::Line, lineIndex++ }));
            if (!lineNode.hasKey("notes")) err("missing notes");
            if (!lineNode["notes"].isArray()) err("notes is not an array");
            for (auto& noteNode : lineNode["notes"].getArray()) {
                if (!noteNode.isObject()) err("note is not an object");
                auto& note = line.notes.emplace_back();
                if (!noteNode.hasKey("bpm")) err("missing bpm");
                if (!noteNode["bpm"].isNumber()) err("bpm is not a number");
                uint64 bpm = noteNode["bpm"].getNumber();
                if (!cvtTime(noteNode, "startTime", bpm, &note.timeZone.x)) err("invalid startTime");
                if (!cvtTime(noteNode, "endTime", bpm, &note.timeZone.y)) err("invalid endTime");
                if (!noteNode.hasKey("type")) err("missing type");
                if (!noteNode["type"].isNumber()) err("type is not a number");
                note.type = MilNoteTypeHelper::FromInt(noteNode["type"].getNumber());
                if (!noteNode.hasKey("isFake")) err("missing isFake");
                if (!noteNode["isFake"].isBool()) err("isFake is not a bool");
                note.isFake = noteNode["isFake"].getBool();
                if (!noteNode.hasKey("isAlwaysPerfect")) err("missing isAlwaysPerfect");
                if (!noteNode["isAlwaysPerfect"].isBool()) err("isAlwaysPerfect is not a bool");
                note.isAlwaysPerfect = noteNode["isAlwaysPerfect"].getBool();
                if (!noteNode.hasKey("index")) err("missing index");
                if (!noteNode["index"].isNumber()) err("index is not a number");
                note.indexer.set(chart.animator.indexGen.get({ EnumMilObjectType::Note, (uint64)noteNode["index"].getNumber() }));
            }
        }
        if (!jsonRoot.hasKey("storyboardObjects")) err("missing storyboardObjects");
        if (!jsonRoot["storyboardObjects"].isArray()) err("storyboardObjects is not an array");
        uint64 sbIndex = 0;
        for (auto& sbNode : jsonRoot["storyboardObjects"].getArray()) {
            if (!sbNode.isObject()) err("storyboardObject is not an object");
            auto& sb = chart.storyboardObjects.emplace_back();
            sb.indexer.set(chart.animator.indexGen.get({ EnumMilObjectType::Storyboard, sbIndex++ }));
            if (!sbNode.hasKey("type")) err("missing type");
            if (!sbNode["type"].isNumber()) err("type is not a number");
            sb.type = MilStoryboardTypeHelper::FromInt(sbNode["type"].getNumber());
            if (!sbNode.hasKey("data")) err("missing data");
            if (!sbNode["data"].isString()) err("data is not a string");
            sb.data = sbNode["data"].getString();
            if (!sbNode.hasKey("layer")) err("missing layer");
            if (!sbNode["layer"].isNumber()) err("layer is not a number");
            sb.layer = MilStoryboardLayerHelper::FromInt(sbNode["layer"].getNumber());
        }
        auto cvtAnimVal = [](JsonNode& node, const std::string& key, float64* dst) {
            if (!node.hasKey(key)) return false;
            auto& valNode = node[key];
            if (valNode.isNumber()) { *dst = valNode.getNumber(); return true; }
            if (valNode.isString()) {
                if (valNode.getString().empty()) { *dst = 0.0; return true; }
                try { *dst = std::stod(valNode.getString()); } catch (...) { return false; }
                return true;
            }
            return false;
        };
        auto cvtColorAnimVal = [](JsonNode& node, const std::string& key, Color* dst) {
            if (!node.hasKey(key)) return false;
            auto& valNode = node[key];
            if (valNode.isString()) { try { *dst = Color::FromCss(valNode.getString()); } catch (...) { return false; } return true; }
            return false;
        };
        if (!jsonRoot.hasKey("animations")) err("missing animations");
        if (!jsonRoot["animations"].isArray()) err("animations is not an array");
        uint64 eventIndex = 0;
        for (auto& animNode : jsonRoot["animations"].getArray()) {
            if (!animNode.isObject()) err("animation is not an object");
            MilEvent e {}; e.index = eventIndex++;
            if (!animNode.hasKey("bpmId")) err("missing bpmId");
            if (!animNode["bpmId"].isNumber()) err("bpmId is not a number");
            uint64 bpm = animNode["bpmId"].getNumber();
            if (!cvtTime(animNode, "fromBeat", bpm, &e.timeZone.x)) err("invalid fromBeat");
            if (!cvtTime(animNode, "toBeat", bpm, &e.timeZone.y)) err("invalid toBeat");
            if (!animNode.hasKey("key")) err("missing key");
            if (!animNode["key"].isNumber()) err("key is not a number");
            e.type = MilEventTypeHelper::FromInt(animNode["key"].getNumber());
            if (e.type == EnumMilEventType::Color) {
                Color fv, tv;
                if (!cvtColorAnimVal(animNode, "fv", &fv)) err("invalid fv");
                if (!cvtColorAnimVal(animNode, "tv", &tv)) err("invalid tv");
                e.valueZone = chart.storyboardAssets.requestColorPair(fv, tv);
            } else {
                if (!cvtAnimVal(animNode, "fv", &e.valueZone.x)) err("invalid fv");
                if (!cvtAnimVal(animNode, "tv", &e.valueZone.y)) err("invalid tv");
            }
            if (!animNode.hasKey("data")) err("missing data");
            if (!animNode["data"].isNumber()) err("data is not a number");
            auto objType = MilObjectTypeHelper::FromInt(animNode["data"].getNumber());
            if (!animNode.hasKey("i1")) err("missing i1");
            if (!animNode["i1"].isNumber()) err("i1 is not a number");
            uint64 objIndex = animNode["i1"].getNumber();
            if (!animNode.hasKey("ease")) err("missing ease");
            if (!animNode["ease"].isNumber()) err("ease is not a number");
            uint64 ease = animNode["ease"].getNumber();
            if (!animNode.hasKey("press")) err("missing press");
            if (!animNode["press"].isNumber()) err("press is not a number");
            uint64 press = animNode["press"].getNumber();
            if (press != 0) {
                e.easingFuncContext = (void*)EaseSet::Milthm::pack(ease, press);
                e.easingFunc = [](void* ctx, float64 p) {
                    auto [ease, press] = EaseSet::Milthm::unpack((uint64)ctx);
                    return EaseSet::Milthm::easing(ease, press, p);
                };
                e.easingIntFunc = [](void* ctx, float64 p) {
                    auto [ease, press] = EaseSet::Milthm::unpack((uint64)ctx);
                    return EaseSet::Milthm::easing_int(ease, press, p);
                };
            }
            chart.animator.addEvent({ objType, objIndex }, e);
        }
        chart.rawHash = data.getHash();
        return chart;
    }
    MilChart loadMilChartFromIngameJson(const Data& data) {
        auto err = [](const std::string& msg) { throw std::runtime_error(std::format("ingame: {}", msg)); };
        auto jsonRoot = JsonNode::Parse(data);
        MilChart chart {};
        chart.meta.noteFlowSpeedBehavior = MilMeta::NoteFlowSpeedBehavior::Override;
        chart.meta.worldOrigin = { (float64)-1920 / 2, (float64)1080 / 2 };
        chart.meta.worldViewport = { 1920, -1080 };
        chart.meta.speedUnit = 108.0;
        if (!jsonRoot.isObject()) err("root is not an object");
        if (!jsonRoot.hasKey("meta")) err("missing meta");
        if (!jsonRoot["meta"].isObject()) err("meta is not an object");
        auto& metaNode = jsonRoot["meta"];
        if (metaNode.hasKey("name") && metaNode["name"].isString()) chart.meta.title = metaNode["name"].getString();
        if (metaNode.hasKey("music_artist") && metaNode["music_artist"].isString()) chart.meta.composer = metaNode["music_artist"].getString();
        if (metaNode.hasKey("background_artist") && metaNode["background_artist"].isString()) chart.meta.artist = metaNode["background_artist"].getString();
        if (metaNode.hasKey("charter") && metaNode["charter"].isString()) chart.meta.charter = metaNode["charter"].getString();
        if (metaNode.hasKey("difficulty_name") && metaNode["difficulty_name"].isString()) chart.meta.difficultyName = metaNode["difficulty_name"].getString();
        if (metaNode.hasKey("difficulty") && metaNode["difficulty"].isNumber()) chart.meta.difficultyValue = metaNode["difficulty"].getNumber();
        std::vector<MilBPMEvent> bpms;
        if (jsonRoot.hasKey("bpms") && jsonRoot["bpms"].isArray()) {
            for (auto& bpmNode : jsonRoot["bpms"].getArray()) {
                if (!bpmNode.isObject() || !bpmNode.hasKey("bpm") || !bpmNode["bpm"].isNumber()) continue;
                float64 bpmVal = bpmNode["bpm"].getNumber();
                float64 beat = 0.0;
                if (bpmNode.hasKey("time") && bpmNode["time"].isArray()) {
                    auto& tb = bpmNode["time"].getArray();
                    beat = tb.size() >= 3 ? tb[0].getNumber() + tb[1].getNumber() / tb[2].getNumber() : tb[0].getNumber();
                }
                bpms.push_back({ beat, bpmVal });
            }
        }
        if (bpms.empty()) bpms.push_back({ 0.0, 120.0 });
        std::stable_sort(bpms.begin(), bpms.end(), [](auto& a, auto& b) { return a.time < b.time; });
        std::vector<float64> bpmBeats; bpmBeats.resize(bpms.size());
        for (size_t i = 0; i < bpms.size(); i++) bpmBeats[i] = bpms[i].time;
        // Convert beat positions → seconds using running sum
        { float64 running = 0.0, lastBeat = 0.0; for (size_t i=0; i<bpms.size(); i++) { running += (bpmBeats[i]-lastBeat)*(60.0/bpms[i].bpm); bpms[i].time=running; lastBeat=bpmBeats[i]; } }
        auto beatToSec = [&](float64 beat) -> float64 {
            if (beat <= 0.0) return 0.0;
            float64 total = 0.0, prevBeat = 0.0, prevBpm = bpms[0].bpm;
            for (size_t i = 0; i < bpmBeats.size(); i++) {
                if (bpmBeats[i] >= beat) { return total + (beat - prevBeat) * (60.0 / prevBpm); }
                total += (bpmBeats[i] - prevBeat) * (60.0 / prevBpm); prevBeat = bpmBeats[i]; prevBpm = bpms[i].bpm;
            }
            return total + (beat - prevBeat) * (60.0 / prevBpm);
        };
        auto readBeatArray = [&](JsonNode& node, const std::string& key, float64* dst) -> bool {
            if (!node.hasKey(key)) return false;
            auto& tn = node[key];
            if (tn.isNumber()) { *dst = tn.getNumber(); return true; }
            if (tn.isArray()) { auto& tb = tn.getArray(); *dst = beatToSec(tb.size()>=3 ? tb[0].getNumber() + tb[1].getNumber()/tb[2].getNumber() : tb[0].getNumber()); return true; }
            return false;
        };
        if (!jsonRoot.hasKey("lines")) err("missing lines");
        if (!jsonRoot["lines"].isArray()) err("lines is not an array");
        uint64 lineIndex = 0;
        for (auto& lineNode : jsonRoot["lines"].getArray()) {
            if (!lineNode.isObject()) err("line is not an object");
            auto& line = chart.lines.emplace_back();
            line.indexer.set(chart.animator.indexGen.get({ EnumMilObjectType::Line, lineIndex++ }));
            if (!lineNode.hasKey("notes")) err("missing notes");
            if (!lineNode["notes"].isArray()) err("notes is not an array");
            for (auto& noteNode : lineNode["notes"].getArray()) {
                if (!noteNode.isObject()) err("note is not an object");
                auto& note = line.notes.emplace_back();
                readBeatArray(noteNode, "time", &note.timeZone.x);
                if (!readBeatArray(noteNode, "endTime", &note.timeZone.y)) note.timeZone.y = note.timeZone.x;
                if (!noteNode.hasKey("type")) err("missing type");
                if (!noteNode["type"].isNumber()) err("type is not a number");
                note.type = MilNoteTypeHelper::FromInt(noteNode["type"].getNumber());
                if (noteNode.hasKey("isFake") && noteNode["isFake"].isBool()) note.isFake = noteNode["isFake"].getBool();
                if (noteNode.hasKey("isAlwaysPerfect") && noteNode["isAlwaysPerfect"].isBool()) note.isAlwaysPerfect = noteNode["isAlwaysPerfect"].getBool();
                note.indexer.set(chart.animator.indexGen.get({ EnumMilObjectType::Note, (uint64)(line.notes.size() - 1) }));
            }
            if (lineNode.hasKey("animations") && lineNode["animations"].isArray()) {
                for (auto& animNode : lineNode["animations"].getArray()) {
                    if (!animNode.isObject()) continue;
                    MilEvent e {};
                    if (!readBeatArray(animNode, "startTime", &e.timeZone.x)) continue;
                    if (!readBeatArray(animNode, "endTime", &e.timeZone.y)) { e.timeZone.y = e.timeZone.x; }
                    if (!animNode.hasKey("type")) continue;
                    e.type = MilEventTypeHelper::FromInt(animNode["type"].isNumber() ? animNode["type"].getNumber() : 0);
                    if (e.type == EnumMilEventType::Color) {
                        Color fv, tv; try { if (animNode.hasKey("start") && animNode["start"].isString()) fv = Color::FromCss(animNode["start"].getString()); if (animNode.hasKey("end") && animNode["end"].isString()) tv = Color::FromCss(animNode["end"].getString()); e.valueZone = chart.storyboardAssets.requestColorPair(fv, tv); } catch (...) {}
                    } else {
                        if (animNode.hasKey("start")) { if (animNode["start"].isNumber()) e.valueZone.x = animNode["start"].getNumber(); else if (animNode["start"].isString()) try { e.valueZone.x = std::stod(animNode["start"].getString()); } catch(...){} }
                        if (animNode.hasKey("end")) { if (animNode["end"].isNumber()) e.valueZone.y = animNode["end"].getNumber(); else if (animNode["end"].isString()) try { e.valueZone.y = std::stod(animNode["end"].getString()); } catch(...){} }
                    }
                    auto objType = MilObjectTypeHelper::FromInt(animNode.hasKey("bearer_type") && animNode["bearer_type"].isNumber() ? animNode["bearer_type"].getNumber() : 0);
                    uint64 objIndex = animNode.hasKey("bearer") && animNode["bearer"].isNumber() ? animNode["bearer"].getNumber() : (uint64)(&line - chart.lines.data());
                    uint64 easeCfg = 0, pressCfg = 0;
                    if (animNode.hasKey("ease")) {
                        auto& easeNode = animNode["ease"];
                        if (easeNode.isNumber()) easeCfg = easeNode.getNumber();
                        else if (easeNode.isObject()) { if (easeNode.hasKey("type") && easeNode["type"].isNumber()) easeCfg = easeNode["type"].getNumber(); if (easeNode.hasKey("press") && easeNode["press"].isNumber()) pressCfg = easeNode["press"].getNumber(); }
                        }
                    if (pressCfg != 0) { e.easingFuncContext = (void*)EaseSet::Milthm::pack(easeCfg, pressCfg); e.easingFunc = [](void* c, float64 p) { auto [x,y] = EaseSet::Milthm::unpack((uint64)c); return EaseSet::Milthm::easing(x,y,p); }; e.easingIntFunc = [](void* c, float64 p) { auto [x,y] = EaseSet::Milthm::unpack((uint64)c); return EaseSet::Milthm::easing_int(x,y,p); }; }
                    chart.animator.addEvent({ objType, objIndex }, e);
                }
            }
        }
        if (jsonRoot.hasKey("storyboards") && jsonRoot["storyboards"].isArray()) { uint64 sbidx=0; for (auto& sb: jsonRoot["storyboards"].getArray()) { if (!sb.isObject()) continue; auto& s = chart.storyboardObjects.emplace_back(); s.indexer.set(chart.animator.indexGen.get({EnumMilObjectType::Storyboard,sbidx++})); if (sb.hasKey("type")&&sb["type"].isNumber()) s.type=MilStoryboardTypeHelper::FromInt(sb["type"].getNumber()); if (sb.hasKey("data")&&sb["data"].isString()) s.data=sb["data"].getString(); if (sb.hasKey("layer")&&sb["layer"].isNumber()) s.layer=MilStoryboardLayerHelper::FromInt(sb["layer"].getNumber()); } }
        chart.rawHash = data.getHash();
        return chart;
    }
    // ===================== [REMJSON] REM Editor 原生谱面 =====================
    // REMJSON = REM Editor(v1.3+) 导出包内的 chart.json（根含 chartStructure）。
    // 零转换：C++ 直接解析 → MilChart，与 JS 链路同构，分三层（胶水层）：
    //   RemJson                  纯 REM 语义中间结构（解析结果，不含任何 Milthm 概念）
    //   RemUnits                 胶水层换算表（REM 参考系 1500x900 ↔ Milthm 世界 1920x1080）
    //   buildMilChartFromRemJson 胶水：RemJson + RemUnits → MilChart（坐标/旋转/速度/alpha/easing 全在此层）
    // 语义依据 Godot 原生播放器 sim/*.gd（chart_loader / track_player / note_player）。

    // ---------- 1) 解析层：JSON → REM 语义中间结构 ----------
    struct RemJson {
        struct Beat {                 // beat = [主拍, 分子, 分母]，1-based（1 == 第 0 拍）
            float64 main = 1.0, num = 0.0, den = 1.0;
            bool valid = false;
        };
        struct Anim {                 // 数值事件
            Beat start, end;
            float64 sv = 0.0, tv = 0.0;
            int64 easing = 1;
        };
        struct ColorAnim {            // color 事件（值 = [r,g,b]，0~255）
            Beat start, end;
            float64 r0 = 255, g0 = 255, b0 = 255, r1 = 255, g1 = 255, b1 = 255;
            int64 easing = 1;
        };
        struct Note {
            int64 type = 0;           // 0=tap 1=drag 2=hold 3=闪电
            Beat start, end;
            float64 speed = 1.0;
            bool isFake = false, isAlwaysPerfect = false;
        };
        struct Track {
            std::string id, name, image;
            bool bindNoteSize = false;
            std::vector<Note> notes;
            std::map<std::string, std::vector<Anim>> anims;   // 事件键 → 数值事件表
            std::vector<ColorAnim> colors;
        };
        struct BpmChange { float64 beat = 0.0, bpm = 0.0; };
        struct Meta {
            std::string musicTitle, musicAuthor, artworkAuthor, chartAuthor;
            std::string difficultyCode, musicFilePath, artworkPath;
            float64 difficultyLevel = 0.0;
            float64 offset = 0.0;      // 单位: 毫秒(chart.json meta.offset, 如 -430)
        };
        float64 bpm = 120.0;
        std::vector<BpmChange> bpmChanges;
        Meta meta;
        std::vector<Track> tracks;
        uint64 nSkippedLightning = 0;
    };

    RemJson loadRemJson(const Data& data) {
        auto err = [](const std::string& msg) {
            throw std::runtime_error(std::format("remjson: {}", msg));
        };
        auto jsonRoot = JsonNode::Parse(data);
        if (!jsonRoot.isObject()) err("root is not an object");
        if (!jsonRoot.hasKey("chartStructure")) err("missing chartStructure");
        auto& structure = jsonRoot["chartStructure"];
        if (!structure.isObject()) err("chartStructure is not an object");

        RemJson rem;
        auto num = [](JsonNode& node, const char* key, float64 dflt) -> float64 {
            if (node.hasKey(key) && node[key].isNumber()) return node[key].getNumber();
            return dflt;
        };
        auto readBeat = [](JsonNode& node, RemJson::Beat& b) -> bool {
            if (!node.isArray()) return false;
            auto& a = node.getArray();
            if (a.size() >= 3) {
                b.main = a[0].getNumber();
                b.num = a[1].getNumber();
                b.den = a[2].getNumber();
                b.valid = true;
                return true;
            }
            if (a.size() == 1) {      // 简写：单个数字 == 主拍
                b.main = a[0].getNumber();
                b.num = 0.0; b.den = 1.0;
                b.valid = true;
                return true;
            }
            return false;
        };
        // 数值事件键（track_player.gd::_prop_map 的 8 键 + 单独处理的 speedEvent）
        static const char* kNumKeys[] = {
            "x", "y", "rotation", "scale", "noteAlpha", "trackAlpha", "lineAlpha", "speedEvent"
        };

        if (jsonRoot.hasKey("bpm") && jsonRoot["bpm"].isNumber()) rem.bpm = jsonRoot["bpm"].getNumber();
        if (!(rem.bpm > 0.0)) rem.bpm = 120.0;

        if (structure.hasKey("bpmChanges") && structure["bpmChanges"].isArray()) {
            for (auto& c : structure["bpmChanges"].getArray()) {
                if (!c.isObject()) continue;
                float64 bpm = num(c, "bpm", 0.0);
                if (!(bpm > 0.0)) continue;
                RemJson::BpmChange bc;
                bc.bpm = bpm;
                bc.beat = std::max<float64>(0.0, num(c, "beat", 1.0) - 1.0);
                rem.bpmChanges.push_back(bc);
            }
        }

        if (jsonRoot.hasKey("meta") && jsonRoot["meta"].isObject()) {
            auto& m = jsonRoot["meta"];
            auto takeStr = [&](const char* k, std::string& dst) {
                if (m.hasKey(k) && m[k].isString()) dst = m[k].getString();
            };
            takeStr("musicTitle", rem.meta.musicTitle);
            takeStr("musicAuthor", rem.meta.musicAuthor);
            takeStr("artworkAuthor", rem.meta.artworkAuthor);
            takeStr("chartAuthor", rem.meta.chartAuthor);
            takeStr("difficultyCode", rem.meta.difficultyCode);
            takeStr("musicFilePath", rem.meta.musicFilePath);
            takeStr("artworkPath", rem.meta.artworkPath);
            if (m.hasKey("difficultyLevel")) {
                if (m["difficultyLevel"].isNumber()) rem.meta.difficultyLevel = m["difficultyLevel"].getNumber();
                else if (m["difficultyLevel"].isString())
                    rem.meta.difficultyLevel = std::atof(m["difficultyLevel"].getString().c_str());
            }
            rem.meta.offset = num(m, "offset", 0.0);
        }

        if (structure.hasKey("tracks") && structure["tracks"].isArray()) {
            for (auto& t : structure["tracks"].getArray()) {
                if (!t.isObject()) continue;
                RemJson::Track track;
                auto takeStr = [&](const char* k, std::string& dst) {
                    if (t.hasKey(k) && t[k].isString()) dst = t[k].getString();
                };
                takeStr("id", track.id);
                takeStr("name", track.name);
                takeStr("image", track.image);
                if (t.hasKey("bindNoteSize") && t["bindNoteSize"].isBool())
                    track.bindNoteSize = t["bindNoteSize"].getBool();

                if (t.hasKey("notes") && t["notes"].isArray()) {
                    for (auto& n : t["notes"].getArray()) {
                        if (!n.isObject()) continue;
                        RemJson::Note note;
                        note.type = (int64)num(n, "type", 0.0);
                        if (note.type == 3) { rem.nSkippedLightning++; continue; }   // 闪电音符：暂不支持
                        if (n.hasKey("startTime")) readBeat(n["startTime"], note.start);
                        if (n.hasKey("endTime")) readBeat(n["endTime"], note.end);
                        if (!note.end.valid) note.end = note.start;
                        note.speed = num(n, "speed", 1.0);
                        if (n.hasKey("isFake") && n["isFake"].isBool()) note.isFake = n["isFake"].getBool();
                        if (n.hasKey("isAlwaysPerfect") && n["isAlwaysPerfect"].isBool())
                            note.isAlwaysPerfect = n["isAlwaysPerfect"].getBool();
                        track.notes.push_back(note);
                    }
                }

                if (t.hasKey("events") && t["events"].isObject()) {
                    auto& evs = t["events"];
                    for (auto* key : kNumKeys) {
                        if (!evs.hasKey(key)) continue;
                        auto& list = evs[key];
                        if (!list.isArray()) continue;
                        auto& bucket = track.anims[key];
                        for (auto& e0 : list.getArray()) {
                            if (!e0.isObject()) continue;
                            RemJson::Anim a;
                            if (!e0.hasKey("startTime") || !readBeat(e0["startTime"], a.start)) continue;
                            if (!e0.hasKey("endTime") || !readBeat(e0["endTime"], a.end)) a.end = a.start;
                            a.sv = num(e0, "startValue", 0.0);
                            a.tv = num(e0, "endValue", 0.0);
                            a.easing = (int64)num(e0, "easing", 1.0);
                            bucket.push_back(a);
                        }
                    }
                    // color：值 = [r,g,b]（0~255）
                    if (evs.hasKey("color") && evs["color"].isArray()) {
                        for (auto& e0 : evs["color"].getArray()) {
                            if (!e0.isObject()) continue;
                            RemJson::ColorAnim ca;
                            if (!e0.hasKey("startTime") || !readBeat(e0["startTime"], ca.start)) continue;
                            if (!e0.hasKey("endTime") || !readBeat(e0["endTime"], ca.end)) ca.end = ca.start;
                            auto readRGB = [](JsonNode& v, float64& r, float64& g, float64& b) {
                                if (!v.isArray()) return;
                                auto& a = v.getArray();
                                if (a.size() < 3) return;
                                r = a[0].getNumber(); g = a[1].getNumber(); b = a[2].getNumber();
                            };
                            if (e0.hasKey("startValue")) readRGB(e0["startValue"], ca.r0, ca.g0, ca.b0);
                            if (e0.hasKey("endValue")) readRGB(e0["endValue"], ca.r1, ca.g1, ca.b1);
                            ca.easing = (int64)num(e0, "easing", 1.0);
                            track.colors.push_back(ca);
                        }
                    }
                }
                rem.tracks.push_back(std::move(track));
            }
        }
        return rem;
    }

    // ---------- 2) 胶水层换算表 ----------
    // 所有 REM ↔ Milthm 的单位/坐标系换算集中在此，便于校准与复用。
    struct RemUnits {
        // 坐标：REM 参考空间 1500x900 → Milthm 世界 1920x1080
        //   PositionX = remX * (1920/1500) - 960
        //   PositionY = 540 - remY * (1080/900)          （y 轴翻转）
        float64 posXScale = 1920.0 / 1500.0;
        float64 posXBias = -960.0;
        float64 posYScale = -1080.0 / 900.0;
        float64 posYBias = 540.0;
        // 旋转：REM 0 = 朝上、顺时针为正；Milthm 0 = 朝右、默认 90 → Milthm = 90 - REM
        float64 rotScale = -1.0;
        float64 rotBias = 90.0;
        // 尺寸 / 透明度
        float64 sizeScale = 1.0;
        float64 noteAlphaScale = 1.0;          // REM noteAlpha 已是 0~1
        float64 otherAlphaScale = 1.0 / 255.0; // trackAlpha / lineAlpha 为 0~255
        // 变速：REM 播放器 track_player.gd 顶层常量 base_speed = 800（REM 参考系像素/秒），
        //   实际滚动速度 = speedEvent × base_speed。Milthm 每秒位移 = Speed × meta.speedUnit
        //   × UserOptions.flowSpeed(默认 1.66)；REM 1500x900 → Milthm 1920x1080 纵向比 1.2，
        //   故目标 800 × 1.2 世界单位/秒 → speedUnit = 960 / 1.66，Speed 事件值即 REM 倍数。
        float64 baseSpeed = 800.0;
        float64 flowSpeed = 1.66;
        float64 speedUnit() const {
            return baseSpeed * (1080.0 / 900.0) / flowSpeed;
        }
    };

    // ---------- 3) 胶水层：RemJson + RemUnits → MilChart ----------
    MilChart buildMilChartFromRemJson(const RemJson& rem, const RemUnits& u = RemUnits {}) {
        MilChart chart {};
        chart.meta.noteFlowSpeedBehavior = MilMeta::NoteFlowSpeedBehavior::Override;
        chart.meta.worldOrigin = { (float64)-1920 / 2, (float64)1080 / 2 };
        chart.meta.worldViewport = { 1920, -1080 };
        chart.meta.speedUnit = u.speedUnit();
        chart.meta.title = rem.meta.musicTitle;
        chart.meta.composer = rem.meta.musicAuthor;
        chart.meta.artist = rem.meta.artworkAuthor;
        chart.meta.charter = rem.meta.chartAuthor;
        chart.meta.difficultyName = rem.meta.difficultyCode;
        chart.meta.difficultyValue = rem.meta.difficultyLevel;
        // ---- offset (时间轴平移) ----
        // REM(Godot 播放器权威用法, chart_player.gd L695): current_time_ms = audio_pos_ms + offset
        //   ⇒ REM 谱面时间轴 C = 音频时间轴 A + offset_rem(ms)
        // Milthm 渲染器**没有** meta.offset 字段(源 L8426 的 chart.meta.offset 属 Phi 链), 时间轴即音频轴
        //   ⇒ 音符/事件在 REM 谱面轴上时刻 t, 落到 Milthm(音频)轴上应为 A = t - offset_rem/1000
        const float64 timeShift = -rem.meta.offset / 1000.0;

        // ---- beat → 秒（含 bpmChanges）----
        std::vector<RemJson::BpmChange> bpmPoints;
        bpmPoints.push_back({ 0.0, rem.bpm });
        for (auto& c : rem.bpmChanges) bpmPoints.push_back(c);
        std::stable_sort(bpmPoints.begin(), bpmPoints.end(),
            [](const auto& a, const auto& b) { return a.beat < b.beat; });
        auto beatToSecRaw = [&](const RemJson::Beat& b) -> float64 {
            if (!b.valid) return 0.0;
            float64 beat = (b.main - 1.0) + (b.den != 0.0 ? b.num / b.den : 0.0);
            if (!(beat > 0.0)) return 0.0;
            float64 total = 0.0, prevBeat = 0.0, prevBpm = bpmPoints[0].bpm;
            for (size_t i = 0; i < bpmPoints.size(); i++) {
                if (bpmPoints[i].beat >= beat) return total + (beat - prevBeat) * (60.0 / prevBpm);
                total += (bpmPoints[i].beat - prevBeat) * (60.0 / prevBpm);
                prevBeat = bpmPoints[i].beat;
                prevBpm = bpmPoints[i].bpm;
            }
            return total + (beat - prevBeat) * (60.0 / prevBpm);
        };
        // 时间轴平移(offset, 见上): REM 谱面轴 → Milthm 音频轴
        auto beatToSec = [&](const RemJson::Beat& b) -> float64 {
            if (!b.valid) return 0.0;
            return beatToSecRaw(b) + timeShift;
        };

        // ---- REM easing(1..28) → Milthm EaseSet::Milthm::pack(ease, press) ----
        // easing.gd 表下标 → (ease: 0=in/1=out/2=inOut, press)
        auto remEasingPack = [](int64 idx) -> uint64 {
            static constexpr uint8_t kTable[29][2] = {
                { 0, 0 },   // 0  占位（线性）
                { 0, 0 },   // 1  linear
                { 1, 1 },   // 2  easeInSine
                { 0, 1 },   // 3  easeOutSine
                { 1, 2 },   // 4  easeOutQuad
                { 0, 2 },   // 5  easeInQuad
                { 0, 1 },   // 6  easeOutSine（与 3 相同）
                { 2, 2 },   // 7  easeInOutQuad
                { 1, 3 },   // 8  easeOutCubic
                { 0, 3 },   // 9  easeInCubic
                { 1, 4 },   // 10 easeOutQuart
                { 0, 4 },   // 11 easeInQuart
                { 2, 3 },   // 12 easeInOutCubic
                { 2, 4 },   // 13 easeInOutQuart
                { 1, 5 },   // 14 easeOutQuint
                { 0, 5 },   // 15 easeInQuint
                { 0, 6 },   // 16 easeInExpo
                { 1, 6 },   // 17 easeOutExpo
                { 1, 7 },   // 18 easeInCirc
                { 0, 7 },   // 19 easeOutCirc
                { 1, 8 },   // 20 easeOutBack
                { 0, 8 },   // 21 easeInBack
                { 2, 7 },   // 22 easeInOutCirc
                { 2, 8 },   // 23 easeInOutBack
                { 1, 9 },   // 24 easeOutElastic
                { 0, 9 },   // 25 easeInElastic
                { 1, 10 },  // 26 easeOutBounce
                { 0, 10 },  // 27 easeInBounce
                { 2, 10 },  // 28 easeInOutBounce
            };
            if (idx < 0 || idx > 28) return 0;
            return EaseSet::Milthm::pack(kTable[idx][0], kTable[idx][1]);
        };
        auto attachEasing = [&](MilEvent& e, int64 easingIdx) {
            auto packed = remEasingPack(easingIdx);
            auto [ez, pr] = EaseSet::Milthm::unpack(packed);
            if (pr == 0) return;   // 线性：不挂 easingFunc（走默认）
            e.easingFuncContext = (void*)packed;
            e.easingFunc = [](void* c, float64 p) {
                auto [x, y] = EaseSet::Milthm::unpack((uint64)c);
                return EaseSet::Milthm::easing(x, y, p);
            };
            e.easingIntFunc = [](void* c, float64 p) {
                auto [x, y] = EaseSet::Milthm::unpack((uint64)c);
                return EaseSet::Milthm::easing_int(x, y, p);
            };
        };

        // ---- 轨道事件键 → Milthm 事件（obj 默认 Line；值为 仿射 v*scale+bias）----
        struct RemEventKey {
            const char* key;
            EnumMilEventType type;
            float64 scale;
            float64 bias;
        };
        const RemEventKey kKeys[] = {
            { "x",          EnumMilEventType::PositionX,            u.posXScale,      u.posXBias },
            { "y",          EnumMilEventType::PositionY,            u.posYScale,      u.posYBias },
            { "rotation",   EnumMilEventType::Rotation,             u.rotScale,       u.rotBias },
            { "scale",      EnumMilEventType::Size,                 u.sizeScale,      0.0 },
            { "noteAlpha",  EnumMilEventType::WholeTransparency,    u.noteAlphaScale, 0.0 },
            { "trackAlpha", EnumMilEventType::LineHeadTransparency, u.otherAlphaScale, 0.0 },
            { "lineAlpha",  EnumMilEventType::LineBodyTransparency, u.otherAlphaScale, 0.0 },
            { "speedEvent", EnumMilEventType::Speed,                1.0,              0.0 },  // 单位随 speedUnit
        };

        uint64 nEvents = 0, lineIndex = 0;
        for (auto& track : rem.tracks) {
            uint64 thisLine = lineIndex++;
            auto& line = chart.lines.emplace_back();
            line.indexer.set(chart.animator.indexGen.get({ EnumMilObjectType::Line, thisLine }));

            // ---- notes ----
            for (auto& rn : track.notes) {
                auto& note = line.notes.emplace_back();
                note.indexer.set(
                    chart.animator.indexGen.get({ EnumMilObjectType::Note, (uint64)(line.notes.size() - 1) })
                );
                // type: 0=tap / 1=drag / 2=hold（hold 在 Milthm 里是 Hit + 非零时区）
                note.type = MilNoteTypeHelper::FromInt(rn.type == 2 ? 0 : (uint64)rn.type);
                float64 t0 = beatToSec(rn.start), t1 = beatToSec(rn.end);
                if (t1 < t0) std::swap(t0, t1);
                note.timeZone = { t0, t1 };
                note.isFake = rn.isFake;
                note.isAlwaysPerfect = rn.isAlwaysPerfect;

                // 音符级速度倍率 → Note 的 FlowSpeed 事件（恒定值覆盖音符生命周期）
                if (std::fabs(rn.speed - 1.0) > 1e-6) {
                    MilEvent e {};
                    e.type = EnumMilEventType::FlowSpeed;
                    e.timeZone = { 0.0, t1 };
                    e.valueZone = { rn.speed, rn.speed };
                    chart.animator.addEvent({ EnumMilObjectType::Note, note.indexer.get() }, e);
                }
            }

            // ---- 数值事件 ----
            for (auto& kd : kKeys) {
                auto it = track.anims.find(kd.key);
                if (it == track.anims.end()) continue;
                for (auto& ra : it->second) {
                    float64 a = beatToSec(ra.start), b = beatToSec(ra.end);
                    if (b < a) std::swap(a, b);
                    MilEvent e {};
                    e.type = kd.type;
                    e.timeZone = { a, b };
                    e.valueZone = { ra.sv * kd.scale + kd.bias, ra.tv * kd.scale + kd.bias };
                    attachEasing(e, ra.easing);
                    chart.animator.addEvent({ EnumMilObjectType::Line, thisLine }, e);
                    nEvents++;
                }
            }
            // ---- color: [r,g,b](0~255) → storyboardAssets 调色板 ----
            for (auto& rc : track.colors) {
                float64 a = beatToSec(rc.start), b = beatToSec(rc.end);
                if (b < a) std::swap(a, b);
                Color c0 { rc.r0 / 255.0, rc.g0 / 255.0, rc.b0 / 255.0, 1.0 };
                Color c1 { rc.r1 / 255.0, rc.g1 / 255.0, rc.b1 / 255.0, 1.0 };
                MilEvent e {};
                e.type = EnumMilEventType::Color;
                e.timeZone = { a, b };
                e.valueZone = chart.storyboardAssets.requestColorPair(c0, c1);
                attachEasing(e, rc.easing);
                chart.animator.addEvent({ EnumMilObjectType::Line, thisLine }, e);
                nEvents++;
            }
        }

        uint64 nNotes = 0;
        for (auto& l : chart.lines) nNotes += l.notes.size();
        std::fprintf(stderr,
            "[REMJSON] 原生解析(胶水层): bpm=%.3f(变速点 %zu) 轨道=%zu 音符=%zu 事件=%llu 闪电跳过=%llu\n",
            rem.bpm, rem.bpmChanges.size(), chart.lines.size(), (size_t)nNotes,
            (unsigned long long)nEvents, (unsigned long long)rem.nSkippedLightning);
        return chart;
    }

    // 对外入口：保持既有调用点不变（Data → MilChart）
    MilChart loadMilChartFromRemJson(const Data& data) {
        auto chart = buildMilChartFromRemJson(loadRemJson(data));
        chart.rawHash = data.getHash();
        return chart;
    }

    MilChart loadMilChartFromData(const Data& data) {
        std::string devErr;
        try { return loadMilChartFromDevJson(data); }
        catch (const std::exception& e) { devErr = e.what(); }
        try { return loadMilChartFromIngameJson(data); }
        catch (const std::exception& e2) { devErr = std::format("{}\ningame: {}", devErr, e2.what()); }
        try { return loadMilChartFromRemJson(data); }
        catch (const std::exception& e3) { throw std::runtime_error(std::format("failures:\ndev: {}\nremjson: {}", devErr, e3.what())); }
    }

    struct MilCalculateFrameConfig {
        struct NoteTextureInfo {
            Vec2 textureSize;
            Vec2 cutPadding;
            Vec2 scaling = { 1.0, 1.0 };
        };

        Vec2 screenSize;
        Vec2 backgroundTextureSize;
        std::map<MilNoteTextureDesc, NoteTextureInfo> noteTextureInfos;
        float64 songLength;
        float64 lineHeadScale = 1.0;
        float64 lineHeadConnectPoint;
        float64 maxNoteBodyLength = 8192.0;
    };

    struct MilCalculatedFrame {
        UsingSharedCalculatedObjects;

        struct CalculatedLineHead {
            Vec2 position, scale;
            float64 size;
            float64 rotation;
            Color color;
        };

        struct CalculatedLineBody {
            Vec2 origin;
            Vec2 position;
            Vec2 size;
            Vec2 scale;
            float64 rotation;
            Color color;
        };

        struct CalculatedNote {
            Vec2 position;
            float64 rotation;
            float64 width, head, body, tail;
            MilNoteTextureDesc textureDesc;
            Color color;
        };

        struct CalculatedPauseButton {
            Vec2 position, scale = { 1.0, 1.0 };
            float64 size;
            float64 rotation;
            Color color;
        };

        struct CalculatedParticles {
            struct Item {
                Vec2 position, radius;
                float64 rotation;
                Color color;
            };

            Item* items;
            uint64 count;
            // -1 = 无贴图(程序化 circle SDF, 原有打击粒子); 0 = fx/lightning2; 1 = fx/circle
            // 用索引而非 gsp<TextureInfo>: 计算发生在 free 函数 calculateMilFrame, 拿不到 renderer 成员
            int64 fxTexture = -1;
            bool additive = false;
        };

        struct CalculatedHitEffectTexture {
            Vec2 position, size;
            float64 progress, rotation;
            Color color;
        };

        using CalculatedObject = std::variant<
            ListSharedCalculatedObjects,
            CalculatedLineHead,
            CalculatedLineBody,
            CalculatedNote,
            CalculatedPauseButton,
            CalculatedParticles,
            CalculatedHitEffectTexture
        >;

        Rect backgroundRect;
        Rect progressbarRect;
        std::vector<CalculatedObject> objects;
        std::unordered_map<EnumMilNoteType, uint64> hitsounds;

        void culling(std::vector<CalculatedObject>& objects, const Rect& screenRect) noexcept {
            if (SharedCalculatedObjects::sharedCulling(objects, screenRect)) return;

            auto& obj = objects.back();

            if (std::holds_alternative<CalculatedLineHead>(obj)) {
                auto& head = std::get<CalculatedLineHead>(obj);
                if (!quadStrictlyIntersectRect(makeQuadFromRectInfo({
                    .position = head.position,
                    .size = head.scale * head.size,
                    .rotation = head.rotation
                }).data(), screenRect)) objects.pop_back();
            } else if (std::holds_alternative<CalculatedHitEffectTexture>(obj)) {
                auto& effect = std::get<CalculatedHitEffectTexture>(obj);
                if (!quadStrictlyIntersectRect(makeQuadFromRectInfo({
                    .position = effect.position,
                    .size = effect.size,
                    .rotation = effect.rotation
                }).data(), screenRect)) objects.pop_back();
            }
        }

        void addObject(std::vector<CalculatedObject>& objects, const CalculatedObject& obj, const Rect& screenRect) noexcept {
            objects.push_back(obj);
            culling(objects, screenRect);
        }

        struct Cache {
            std::vector<CalculatedObject> trackObjects, hitEffectCircs;
            std::unordered_map<EnumMilFinalNoteType, std::vector<CalculatedObject>> noteObjects;
            std::vector<CalculatedParticles::Item> hitEffectParticles;
            std::vector<CalculatedParticles::Item> fxAdditiveParticles, fxAlphaParticles;
            // FracturePass(独立 HitEffect, Billboard, sortingOrder=0) -> 排在音符之下, 单独一批
            std::vector<CalculatedParticles::Item> fxPassParticles;

            void clear() {
                trackObjects.clear();
                hitEffectCircs.clear();
                for (auto& [_, v] : noteObjects) v.clear();
                hitEffectParticles.clear();
                fxAdditiveParticles.clear();
                fxAlphaParticles.clear();
                fxPassParticles.clear();
            }
        };

        Cache cache;
    };

    // [Intro] 收场后停留: 本家 GamePlayLoops 负时间停留 PrepareDuration(BeatmapModelExtension 0x41d20bc, 下限 3.0f)
    //   进度条 progress = 1.0 + time/PrepareDuration (UpdateProgressBar 0x4183208 反汇编), t=0 回头 time/songLength
    //   本家为纯 localScale 赋值, 无过渡动画(progressBarValHandler 全二进制 0 调用者, EndPrepareDuration 0 调用者)
    // 放 namespace 顶层: calculateMilFrame(自由函数) 与 MilTakeOverer::render 都要用
    //   (introDuration/hold/exit 定义在 MilTakeOverer 内且在其后, 顶层不可见 → 2.05+0.95+1.2 就地展开)
    inline constexpr float64 introAftermath = 3.0;
    inline constexpr float64 introLeadTotal = 2.05 + 0.95 + 1.2 + introAftermath;   // introPreRoll + aftermath = 负时间总窗口 = BGM 静音前导

    void calculateMilFrame(
        MilChart& chart, float64 time,
        const MilCalculateFrameConfig& config,
        MilCalculatedFrame& frame
    ) {
        frame.objects.clear();
        frame.hitsounds.clear();
        frame.cache.clear();

        frame.backgroundRect = getCoveredOrContainRect(
            { 0.0, 0.0, config.screenSize.x, config.screenSize.y },
            config.backgroundTextureSize, true
        );

        Rect screenArea = { 0.0, 0.0, config.screenSize.x, config.screenSize.y };

        struct NoteTextureSizeInfo {
            bool isValid = false;
            MilNoteTextureDesc desc;
            float64 width;
            float64 head, body, tail;

            void scale(const Vec2& v) {
                width *= v.x;
                head *= v.y;
                body *= v.y;
                tail *= v.y;
            }

            float64 getHeadHalfDiagonal() const {
                return Vec2 { width, head }.length() / 2;
            }
        };

        float64 lineHeadBase = config.screenSize.sum() * 0.0223;

        auto fallbackNoteTextureDesc = [&](MilNoteTextureDesc& desc) {
            while (!config.noteTextureInfos.contains(desc)) {
                if (!fallbackMilNoteTextureDesc(desc)) {
                    return false;
                }
            }

            return true;
        };

        auto getNoteTextureSizeInfo = [&](MilNoteTextureDesc desc) -> NoteTextureSizeInfo {
            if (!fallbackNoteTextureDesc(desc)) return {};

            auto& texInfo = config.noteTextureInfos.at(desc);

            auto width = lineHeadBase;
            auto totalHeight = width / texInfo.textureSize.y * texInfo.textureSize.x;
            width *= texInfo.scaling.x; totalHeight *= texInfo.scaling.y;
            auto cutPadding = texInfo.cutPadding / texInfo.textureSize.x;
            auto head = cutPadding.x * totalHeight;
            auto tail = cutPadding.y * totalHeight;

            return NoteTextureSizeInfo {
                .isValid = true,
                .desc = desc,
                .width = width,
                .head = head,
                .tail = tail
            };
        };

        float64 maxHalfNoteHeadDiagonal = 0.0;

        for (auto& [desc, _] : config.noteTextureInfos) {
            maxHalfNoteHeadDiagonal = std::max(
                maxHalfNoteHeadDiagonal,
                getNoteTextureSizeInfo(desc).getHeadHalfDiagonal()
            );
        }

        chart.state.timeUpdated(time);

        auto calculateStoryboards = [&](EnumMilStoryboardLayer targetLayer) {
            for (auto& sb : chart.storyboardObjects) {
                if (sb.layer != targetLayer) continue;

                if (sb.type == EnumMilStoryboardType::Picture) {

                } else if (sb.type == EnumMilStoryboardType::Text) {
                    auto sbPosition = chart.getObjectPosition(time, sb, config.screenSize);
                    auto sbAlpha = chart.animator.get(sb, time, EnumMilEventType::Transparency);
                    auto sbSize = chart.animator.get(sb, time, EnumMilEventType::Size);
                    auto sbRotation = chart.animator.get(sb, time, EnumMilEventType::Rotation);
                    auto sbWidth = chart.animator.get(sb, time, EnumMilEventType::StoryBoardWidth);
                    auto sbHeight = chart.animator.get(sb, time, EnumMilEventType::StoryBoardHeight);
                    auto sbColorIndex = chart.animator.get(sb, time, EnumMilEventType::Color);
                    auto sbColorIndexZone = chart.animator.get_zone(sb, time, EnumMilEventType::Color);
                    auto sbColor = chart.storyboardAssets.getColor(sbColorIndex, sbColorIndexZone);

                    if (sbColor.a * sbAlpha > 0.0) {
                        frame.objects.push_back(MilCalculatedFrame::CalculatedText {
                            .text = sb.data,
                            .position = sbPosition,
                            .scale = { sbWidth, sbHeight },
                            .anchor = { 0.5, 0.5 },
                            .fontSize = config.screenSize.sum() * 0.025 * sbSize,
                            .rotation = -sbRotation,
                            .color = sbColor.applyAlpha(sbAlpha),
                        });
                    }
                }
            }
        };

        calculateStoryboards(EnumMilStoryboardLayer::Background);

        frame.objects.push_back(MilCalculatedFrame::CalculatedRect {
            .position = config.screenSize / 2.0,
            .size = config.screenSize,
            .color = Color::Black().applyAlpha(chart.options.backgroundDim)
        });

        for (auto& line : chart.lines) {
            auto linePosition = chart.getObjectPosition(time, line, config.screenSize);
            auto lineRotation = chart.animator.get(line, time, EnumMilEventType::Rotation);
            auto lineAlpha = chart.animator.get(line, time, EnumMilEventType::Transparency);
            auto lineHeadAlpha = chart.animator.get(line, time, EnumMilEventType::LineHeadTransparency);
            auto lineBodyAlpha = chart.animator.get(line, time, EnumMilEventType::LineBodyTransparency);
            auto lineSize = chart.animator.get(line, time, EnumMilEventType::Size);
            auto lineColorIndex = chart.animator.get(line, time, EnumMilEventType::Color);
            auto lineColorIndexZone = chart.animator.get_zone(line, time, EnumMilEventType::Color);
            auto lineColor = chart.storyboardAssets.getColor(lineColorIndex, lineColorIndexZone);

            lineHeadAlpha *= lineAlpha;
            lineBodyAlpha *= lineAlpha;

            if (lineHeadAlpha > 0.0) {
                frame.addObject(frame.cache.trackObjects, MilCalculatedFrame::CalculatedLineHead {
                    .position = linePosition,
                    .scale = { lineSize, lineSize },
                    .size = lineHeadBase * config.lineHeadScale,
                    .rotation = 180 - lineRotation,
                    .color = lineColor.applyAlpha(lineHeadAlpha)
                }, screenArea);
            }

            if (lineBodyAlpha > 0.0) {
                auto connectRadius = config.lineHeadConnectPoint * lineHeadBase * config.lineHeadScale;
                auto lineWidth = lineHeadBase * 0.096774;
                frame.addObject(frame.cache.trackObjects, MilCalculatedFrame::CalculatedLineBody {
                    .origin = linePosition,
                    .position = { connectRadius, -lineWidth / 2 },
                    .size = { config.screenSize.y * 2.5, lineWidth },
                    .scale = { lineSize, lineSize },
                    .rotation = -lineRotation,
                    .color = lineColor.applyAlpha(lineBodyAlpha)
                }, screenArea);
            }

            for (auto& noteGroup : line.noteGroups) {
                noteGroup.state.timeUpdated(time);

                for (uint64 note_ii = noteGroup.state.firstNoteIndex; note_ii < noteGroup.indexs.size(); note_ii++) {
                    auto note_i = noteGroup.indexs[note_ii];
                    auto& note = line.notes[note_i];
                    note.state.timeUpdated(note, time);

                    auto frameInfo = chart.getNoteFrameInfo(line, note, time, config.screenSize);

                    if (frameInfo.isArrived && note.state.onPlayHitsound()) {
                        // Fracture(闪电) 无打击音(本家实测/用户确认), 不参与 hitsound 计数
                        if (!note.isFake && !note.isFracture()) {
                            frame.hitsounds[note.type]++;
                        }
                    }

                    if (note.timeZone.y < time) {
                        if (!note.isHold() || note.timeZone.y + chart.meta.holdDisappearTime <= time) {
                            noteGroup.state.passedNoteIndex(note_ii);
                            continue;
                        }
                    }

                    if (note.isHold()) {
                        frameInfo.color.a *= 1.0 - std::clamp((time - note.timeZone.y) / chart.meta.holdDisappearTime, 0.0, 1.0);
                    }

                    auto sizeInfo = getNoteTextureSizeInfo(note.textureDesc);
                    sizeInfo.body = std::min(config.maxNoteBodyLength, (frameInfo.headPosition - frameInfo.tailPosition).length());
                    sizeInfo.scale(frameInfo.scale);

                    Transform2D noteTransform;
                    noteTransform.translate(frameInfo.headPosition);
                    noteTransform.rotateDegrees(frameInfo.textureRotation);
                    noteTransform.scale(1.0, -1.0);

                    Vec2 noteQuad[4] = {
                        noteTransform.transformPoint({ -sizeInfo.head, -sizeInfo.width / 2 }),
                        noteTransform.transformPoint({ -sizeInfo.head, sizeInfo.width / 2 }),
                        noteTransform.transformPoint({ sizeInfo.body + sizeInfo.tail, sizeInfo.width / 2 }),
                        noteTransform.transformPoint({ sizeInfo.body + sizeInfo.tail, -sizeInfo.width / 2 })
                    };

                    auto extendedSafeArea = screenArea.extend(maxHalfNoteHeadDiagonal * frameInfo.scale.max());
                    bool noteInsideScreen = quadStrictlyIntersectRect(noteQuad, extendedSafeArea);

                    if (noteInsideScreen) {
                        if (sizeInfo.isValid && frameInfo.color.a > 0.0) {
                            if (note.isHold()) {
                                // [hold 判定材质过渡] 1:1 复刻本家 HoldController.UpdateGraphics(libil2cpp.so arm64
                                // RVA 0x4187574)。本家有**两套渲染器**: 继承来的 Renderer(base) + 独立
                                // ActiveRender/ActiveObject(0x158/0x160)。SetActiveStatus(0x4187358) 激活时
                                // ActiveObject.SetActive(true) 并把 ActiveRender.color 置为 (1,1,1,0), 随后
                                // UpdateGraphics 每帧:
                                //     activeTime = clamp(activeTime ± deltaTime, 0, 0.4)  ; 激活时累加, 否则递减
                                //     mix        = activeTime / 0.4
                                //     ActiveRender.color.a = min(2*mix, 1) * alpha        ; 前 0.2s 淡入 0→1
                                //     alpha               *= 1 - clamp(2*mix-1, 0, 1)     ; 后 0.2s 淡出 1→0
                                // 即「先淡入 active、再淡出 base」的顺序过渡, 中段两材质同时可见 → 合计亮度峰值 2×
                                // (本家的判定闪光)。若写成对称交叉溶解(权重和恒为 1)会丢掉这个亮度峰值。
                                // 本渲染器是「计算全帧、不依赖前序帧累积」(time 可为任意跳变值, --dumpmil 直接跳 t),
                                // 故用纯函数 mix = clamp((time - head) / 0.4, 0, 1) 等价替代本家的逐帧累加。
                                //
                                // 可激活保护: loader 已为「所有 Hit 系 hold」(标准/ex/simul)注册 builtin.hold_active.png。
                                // submission 块内 note.isHold() 必真且 type=Hit(见 L10844 if), 故这里可激活恒成立;
                                // 保留判断以防未来 Drag 系加入。非 Hit/non-hold 不进此块。
                                float64 mix = 0.0;
                                if (note.type == EnumMilNoteType::Hit && time >= note.timeZone.x) {
                                    mix = std::clamp((time - note.timeZone.x) / MilHoldActivateMixSeconds, 0.0, 1.0);
                                }
                                float64 activeWeight = std::min(2.0 * mix, 1.0);
                                float64 baseWeight = 1.0 - std::clamp(2.0 * mix - 1.0, 0.0, 1.0);
                                // base 材质用 note.textureDesc(active=false), 勿用 sizeInfo.desc(可能已 fallback)
                                auto baseDesc = note.textureDesc;
                                if (baseWeight > 0.0) {
                                    auto color = frameInfo.color;
                                    color.a *= baseWeight;
                                    frame.cache.noteObjects[note.finalType].push_back(MilCalculatedFrame::CalculatedNote {
                                        .position = frameInfo.headPosition,
                                        .rotation = frameInfo.textureRotation,
                                        .width = sizeInfo.width,
                                        .head = sizeInfo.head,
                                        .body = sizeInfo.body,
                                        .tail = sizeInfo.tail,
                                        .textureDesc = baseDesc,
                                        .color = color
                                    });
                                }
                                if (activeWeight > 0.0) {
                                    auto activeDesc = baseDesc;
                                    std::get<MilNoteTextureDescAttrIsActive>(activeDesc) = true;
                                    auto color = frameInfo.color;
                                    color.a *= activeWeight;
                                    frame.cache.noteObjects[note.finalType].push_back(MilCalculatedFrame::CalculatedNote {
                                        .position = frameInfo.headPosition,
                                        .rotation = frameInfo.textureRotation,
                                        .width = sizeInfo.width,
                                        .head = sizeInfo.head,
                                        .body = sizeInfo.body,
                                        .tail = sizeInfo.tail,
                                        .textureDesc = activeDesc,
                                        .color = color
                                    });
                                }
                            } else {
                                frame.cache.noteObjects[note.finalType].push_back(MilCalculatedFrame::CalculatedNote {
                                    .position = frameInfo.headPosition,
                                    .rotation = frameInfo.textureRotation,
                                    .width = sizeInfo.width,
                                    .head = sizeInfo.head,
                                    .body = sizeInfo.body,
                                    .tail = sizeInfo.tail,
                                    .textureDesc = sizeInfo.desc,
                                    .color = frameInfo.color
                                });
                            }
                        }
                    } else {
                        if (noteGroup.breakable) {
                            if (lineIsLeavingScreen(
                                frameInfo.headPosition,
                                frameInfo.speedVectorRotation + 90.0,
                                extendedSafeArea
                            ) && lineIsLeavingScreen(
                                frameInfo.headPosition,
                                frameInfo.textureRotation + 90.0,
                                extendedSafeArea
                            )) break;
                        }
                    }
                }
            }
        }

        for (uint64 i = chart.state.firstHitEffectIndex; i < chart.hitEffects.size(); i++) {
            auto& hitEffect = chart.hitEffects[i];
            if (hitEffect.time > time) break;
            
            auto& line = chart.lines[hitEffect.lineIndex];
            auto& note = line.notes[hitEffect.noteIndex];

            auto info = chart.getNoteFrameInfo(line, note, time, config.screenSize);

            if (hitEffect.getEndTime(chart.options.hitEffectDuration) < time) {
                chart.state.passedHitEffectIndex(i);
                continue;
            }

            auto progress = (time - hitEffect.time) / chart.options.hitEffectDuration;

            if (progress <= 1.0) {
                frame.addObject(frame.cache.hitEffectCircs, MilCalculatedFrame::CalculatedHitEffectTexture {
                    .position = info.headPosition,
                    .size = Vec2(lineHeadBase * 4.632 * (1.0 - std::pow(1.0 - progress, 3.0))) * info.scale.max(),
                    .progress = progress,
                    .rotation = hitEffect.texRotation,
                    .color = Color { 150, 144, 253, 255 } / 255
                }, screenArea);
            }

            for (auto& particle : hitEffect.particles) {
                auto particleTime = hitEffect.time + particle.dt;
                if (particleTime > time) break;
                if (particleTime + chart.options.hitEffectDuration < time) continue;

                auto progress = std::clamp((time - particleTime) / chart.options.hitEffectDuration, 0.0, 1.0);
                auto size = particle.initialSize * config.screenSize.sum();
                auto r = particle.getRadius(progress) * config.screenSize.sum();
                auto rotate = particle.rotate;
                auto color = chart.options.particleRGBColor.get(progress);
                color.a = chart.options.particleAlphaColor.get(progress).a;

                auto particlePos = info.headPosition.rotateDegrees(rotate, r * info.scale.max());
                particlePos.y += particle.getDeltaY(progress) * config.screenSize.sum() * info.scale.max();

                rotate += (rotate - (particlePos - info.headPosition).atanDegrees()) * 2;

                auto& item = frame.cache.hitEffectParticles.emplace_back();
                item.position = particlePos;
                item.radius = particle.getScale(progress) * size * info.scale.max();
                item.rotation = rotate;
                item.color = color;

                if (!quadStrictlyIntersectRect(makeQuadFromRectInfo({
                    .position = item.position,
                    .size = item.radius * 2.0,
                    .rotation = item.rotation
                }).data(), screenArea)) frame.cache.hitEffectParticles.pop_back();
            }
        }

        // ---- Fracture 撞击粒子 (发射器实现见 mil_fx_emitter.hpp) ----
        {
            auto fxUnitScale = milFractureUnitScaleOf(lineHeadBase);
            std::vector<milfx::Quad> fxQuads;

            for (uint64 i = chart.state.firstFractureFxIndex; i < chart.fractureFxs.size(); i++) {
                auto& fx = chart.fractureFxs[i];
                if (fx.time > time) break;

                if (fx.getEndTime() < time) {
                    chart.state.passedFractureFxIndex(i);
                    continue;
                }

                auto& fxLine = chart.lines[fx.lineIndex];
                auto& fxNote = fxLine.notes[fx.noteIndex];

                // Fracture 是 AutoHit: 到线即结算, 位置固定在判定线上
                auto fxInfo = chart.getNoteFrameInfo(fxLine, fxNote, time, config.screenSize);
                auto fxBase = fxInfo.headPosition;
                auto fxDt = time - fx.time;
                auto fxPixelScale = fxUnitScale * fxInfo.scale.max();

                for (uint64 layer = 0; layer < MilFxFxLayerCount; layer++) {
                    if (!milFxLayerEnabled[layer]) continue;
                    auto& spec = milfx::kLayers[layer];
                    fxQuads.clear();
                    // 整体缩放: 乘在 pixelScale 上 -> 粒子尺寸/散布半径/位移同比例, originX/originY(判定点)不动
                    milfx::emit(fxQuads, spec, layer, fx.particles[layer], fxDt, fxBase.x, fxBase.y,
                                fxPixelScale * milFxFxGlobalScaleRuntime());

                    for (auto& q : fxQuads) {
                        auto& list = spec.stretch
                            ? (spec.additive ? frame.cache.fxAdditiveParticles : frame.cache.fxAlphaParticles)
                            : frame.cache.fxPassParticles;
                        auto& item = list.emplace_back();
                        item.position = Vec2 { q.x, q.y };
                        item.radius = Vec2 { q.halfLen, q.halfWid };
                        item.rotation = q.rotDeg;
                        item.color = Color { q.r, q.g, q.b, q.a };

                        if (!quadStrictlyIntersectRect(makeQuadFromRectInfo({
                            .position = item.position,
                            .size = item.radius * 2.0,
                            .rotation = item.rotation
                        }).data(), screenArea)) list.pop_back();
                    }
                }
            }
        }

        calculateStoryboards(EnumMilStoryboardLayer::Normal);
        frame.objects.insert(frame.objects.end(), frame.cache.trackObjects.begin(), frame.cache.trackObjects.end());
        frame.objects.insert(frame.objects.end(), frame.cache.hitEffectCircs.begin(), frame.cache.hitEffectCircs.end());

        // FracturePass: sortingOrder=0 < 音符(6) -> 必须排在音符之前
        if (!frame.cache.fxPassParticles.empty()) {
            frame.objects.push_back(MilCalculatedFrame::CalculatedParticles {
                .items = frame.cache.fxPassParticles.data(),
                .count = frame.cache.fxPassParticles.size(),
                .fxTexture = 0,
                .additive = true
            });
        }

        for (const auto type : {
            EnumMilFinalNoteType::Hold,
            EnumMilFinalNoteType::Tap,
            EnumMilFinalNoteType::Drag,
            EnumMilFinalNoteType::Fracture
        }) {
            auto& objs = frame.cache.noteObjects[type];
            frame.objects.insert(frame.objects.end(), objs.begin(), objs.end());
        }

        frame.objects.push_back(MilCalculatedFrame::CalculatedParticles {
            .items = frame.cache.hitEffectParticles.data(),
            .count = frame.cache.hitEffectParticles.size()
        });

        // Fracture 撞击粒子: 加色层与 AlphaBlend 层分开, 各自一个批次
        if (!frame.cache.fxAdditiveParticles.empty()) {
            frame.objects.push_back(MilCalculatedFrame::CalculatedParticles {
                .items = frame.cache.fxAdditiveParticles.data(),
                .count = frame.cache.fxAdditiveParticles.size(),
                .fxTexture = 0,
                .additive = true
            });
        }

        if (!frame.cache.fxAlphaParticles.empty()) {
            frame.objects.push_back(MilCalculatedFrame::CalculatedParticles {
                .items = frame.cache.fxAlphaParticles.data(),
                .count = frame.cache.fxAlphaParticles.size(),
                .fxTexture = 1,
                .additive = false
            });
        }

        calculateStoryboards(EnumMilStoryboardLayer::Foreground);

        auto combo = chart.getCombo(time);

        // 进度条: t>=0 = time/songLength; t<0 = 本家收场后停留(UpdateProgressBar 0x4183208):
        //   progress = 1.0 + time/PrepareDuration(3.0f), 从头滚到底, t=0 回头 — 纯 localScale 赋值无过渡
        frame.progressbarRect = {
            0.0, 0.0,
            time >= 0.0
                ? (config.songLength > 0.0 ? time / config.songLength * config.screenSize.x : 0.0)
                : std::clamp(1.0 + time / introAftermath, 0.0, 1.0) * config.screenSize.x,
            config.screenSize.x * 0.0046875
        };

        frame.objects.push_back(MilCalculatedFrame::CalculatedPauseButton {
            .position = Vec2(config.screenSize.x) * Vec2 { 0.0494792, 0.0489583 },
            .size = config.screenSize.x * 0.040625,
            .color = Color::White().applyAlpha(0.67)
        });

        frame.objects.push_back(MilCalculatedFrame::CalculatedText {
            .text = chart.meta.title,
            .position = Vec2(config.screenSize.x) * Vec2 { 0.0994791, 0.0397208 },
            .anchor = { 0.0, 0.5 },
            .fontSize = config.screenSize.x * 0.0201352
        });

        frame.objects.push_back(MilCalculatedFrame::CalculatedText {
            .text = chart.meta.getFinalDifficultyString(),
            .position = Vec2(config.screenSize.x) * Vec2 { 0.0994791, 0.0604583 },
            .anchor = { 0.0, 0.5 },
            .fontSize = config.screenSize.x * 0.0151472,
            .color = Color::White().applyAlpha(0.75)
        });

        float64 targetScore = chart.comboTimes.size() ? std::clamp<float64>(std::ceil((float64)1010000 / chart.comboTimes.size() * combo), 0, 1010000) : 1010000;
        frame.objects.push_back(MilCalculatedFrame::CalculatedText {
            .text = std::format("{:07}", (uint64)chart.state.scoreAnim.weakSet(time, targetScore).get(time)),
            .position = Vec2(config.screenSize.x) * Vec2 { 0.9752375, 0.0395833 },
            .anchor = { 1.0, 0.5 },
            .fontSize = config.screenSize.x * 0.0268352,
        });

        frame.objects.push_back(MilCalculatedFrame::CalculatedText {
            .text = "100.00%",
            .position = Vec2(config.screenSize.x) * Vec2 { 0.9752375, 0.06684375 },
            .anchor = { 1.0, 0.5 },
            .fontSize = config.screenSize.x * 0.0201352,
            .color = Color::White().applyAlpha(0.75)
        });
        
        frame.objects.push_back(MilCalculatedFrame::CalculatedText {
            .text = "AUTOPLAY",
            .position = Vec2(config.screenSize.x) * Vec2 { 0.5, 0.0359375 },
            .anchor = { 0.5, 0.5 },
            .fontSize = config.screenSize.x * 0.0201352,
        });
        
        frame.objects.push_back(MilCalculatedFrame::CalculatedText {
            .text = std::to_string(combo),
            .position = Vec2(config.screenSize.x) * Vec2 { 0.5, 0.0677083 },
            .anchor = { 0.5, 0.5 },
            .fontSize = config.screenSize.x * 0.0263352 * chart.state.comboScaleAnim.weakSet(time, combo).get(time)
        });
    }

    DecodedRGBATexture spwanMilBackgroundMask() {
        auto tex = DecodedRGBATexture::Make(2, 128);
        const float64 start = 0.2;
        const float64 alphaExp = 0.8;

        for (uint64 i = 0; i < tex.width; i++) {
            for (uint64 j = 0; j < tex.height; j++) {
                float64 p = (float64)j / (tex.height - 1);
                p = p * (start + 1) - start;
                if (p <= 0.0) continue;
                tex.data[tex.getIndexBase(i, j) + 3] = gnumeric::utils::clamp<uint8, float64>(std::pow(p, alphaExp) * 270);
            }
        }

        return tex;
    }

    DecodedRGBATexture spwanMilProgressbar() {
        auto tex = DecodedRGBATexture::Make(128, 2);
        std::fill(tex.data.begin(), tex.data.end(), 255);

        for (uint64 i = 0; i < tex.width; i++) {
            for (uint64 j = 0; j < tex.height; j++) {
                float64 p = (float64)i / (tex.width - 1);
                p = 1.0 - std::pow(1.0 - p, 2.2);
                tex.data[tex.getIndexBase(i, j) + 3] = gnumeric::utils::clamp<uint8, float64>(p * 255);
            }
        }

        return tex;
    }

    struct MilTakeOverer {
        MilTakeOverer() = default;
        MilTakeOverer(const MilTakeOverer&) = delete;
        MilTakeOverer& operator=(const MilTakeOverer&) = delete;
        MilTakeOverer(MilTakeOverer&&) = default;
        MilTakeOverer& operator=(MilTakeOverer&&) = default;

        static gsp<MilTakeOverer> Make() {
            auto* tor = new MilTakeOverer();
            return gsp<MilTakeOverer>(tor);
        }
        
        struct LineHeadTextureLoaderResult {
            Data encoded;
            float64 scale = 1.0, connectPoint;
            bool connectPointIsPixel = true;
        };

        using LineHeadTextureLoader = std::function<LineHeadTextureLoaderResult()>;
        LineHeadTextureLoader lineHeadTextureLoader;

        struct NoteTextureDataLoaderResult {
            Data encoded;
            Vec2 cutPadding;
            bool cutPaddingIsPixel = true;
            bool ignoreCutPadding;
            Vec2 scaling = { 1.0, 1.0 };
        };

        using NoteTextureDataLoader = std::function<NoteTextureDataLoaderResult(const MilNoteTextureDesc&)>;
        NoteTextureDataLoader noteTextureDataLoader;

        using HitsoundDataLoader = std::function<Data(EnumMilNoteType)>;
        HitsoundDataLoader hitsoundDataLoader;
        
        using PauseButtonTextureDataLoader = std::function<Data()>;
        PauseButtonTextureDataLoader pauseButtonTextureDataLoader;

        // [Storyboard] 贴图故事板资源: 按谱面所在目录按名找图片; 缺失 -> 返回空(渲染层直接透明, 不画占位矩形)
        // [Storyboard 根目录自动寻找] zip 解压后谱面常位于 charts/<name>/ 之类子目录而贴图与谱面同层,
        //   所以搜索根不能只有 chartDir(=解压根), 必须自动以"谱面文件实际所在目录"为基准, 并向上下兜底。
        std::string storyboardAssetDir;
        std::string storyboardChartFileDir;                             // 谱面文件所在目录(最优先搜索根)
        std::vector<std::string> storyboardSearchRoots;                 // 解析后的搜索根(去重, 有序)
        std::unordered_map<std::string, std::string> storyboardIndex;   // 小写文件名 -> 全路径(向下自动查找兜底)
        bool storyboardIndexBuilt = false;
        std::unordered_map<std::string, gsp<GL::TextureInfo>> storyboardTextures;
        std::unordered_set<std::string> missingStoryboardTextures;

        void setStoryboardAssetDir(const std::string& dir, const std::string& chartFilePath = "") {
            namespace fs = std::filesystem;
            std::error_code ec;

            std::string fileDir;
            if (!chartFilePath.empty()) {
                fs::path p = fs::path(chartFilePath).lexically_normal();
                if (p.has_parent_path()) fileDir = p.parent_path().string();
            }
            if (storyboardAssetDir == dir && storyboardChartFileDir == fileDir) return;

            storyboardAssetDir = dir;
            storyboardChartFileDir = fileDir;
            storyboardTextures.clear();
            missingStoryboardTextures.clear();
            storyboardIndex.clear();
            storyboardIndexBuilt = false;
            storyboardSearchRoots.clear();

            auto addRoot = [&](const std::string& d) {
                if (d.empty()) return;
                // make_preferred 统一分隔符: 否则 "E:/a" 与 "E:\a" 被当成两个根, 候选重复
                auto n = fs::path(d).lexically_normal().make_preferred().string();
                for (auto& r : storyboardSearchRoots) if (r == n) return;
                storyboardSearchRoots.push_back(n);
            };

            // 谱面文件所在目录最优先(这才是"谱面所在的根目录"), 再 chartDir 及既有约定位置
            if (!fileDir.empty()) {
                addRoot(fileDir);
                addRoot(fileDir + "/other");
                // zip 里 charts/<name>/ 这类结构: 谱面目录的父链也覆盖
                fs::path cur = fs::path(fileDir).lexically_normal();
                for (int i = 0; i < 3 && cur.has_parent_path(); i++) {
                    cur = cur.parent_path();
                    addRoot(cur.string());
                }
            }
            addRoot(dir);
            addRoot(dir + "/other");
            addRoot(dir + "/..");
            addRoot(dir + "/../..");

            std::fprintf(stderr, "[Storyboard] 资源根: 谱面目录='%s' chartDir='%s' (共 %zu 个候选)\n",
                fileDir.c_str(), dir.c_str(), storyboardSearchRoots.size());
        }

        // [Storyboard] 向下自动查找: 在谱面所在目录/chartDir 子树内按文件名建索引(限深 3 层, 惰性建一次)
        void buildStoryboardIndex() {
            namespace fs = std::filesystem;
            if (storyboardIndexBuilt) return;
            storyboardIndexBuilt = true;

            std::vector<std::string> bases { storyboardChartFileDir, storyboardAssetDir };
            for (auto& base : bases) {
                if (base.empty()) continue;
                std::error_code ec;
                if (!fs::is_directory(base, ec)) continue;

                fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
                fs::recursive_directory_iterator end;
                while (!ec && it != end) {
                    // ⚠ 必须先取 path 快照再 increment: libstdc++ 的 recursive_directory_iterator 拷贝时
                    //    共享内部目录栈, increment() 之后读拷贝 = 读已弹出的栈顶 -> 越界崩溃(实测 0xC0000005)
                    fs::path p = it->path();
                    std::error_code ec2;
                    bool isFile = it->is_regular_file(ec2);
                    it.increment(ec);
                    if (!isFile) { ec.clear(); continue; }

                    ec2.clear();
                    auto rel = fs::relative(p, base, ec2);
                    if (ec2) { ec2.clear(); continue; }
                    int depth = 0;
                    for (auto& part : rel) { (void)part; depth++; }
                    if (depth > 3) continue;

                    std::string fn = p.filename().string();
                    for (auto& c : fn) c = (char)std::tolower((unsigned char)c);
                    if (!storyboardIndex.contains(fn)) storyboardIndex.emplace(fn, p.string());
                }
            }
            std::fprintf(stderr, "[Storyboard] 向下索引: %zu 个文件 (root='%s')\n",
                storyboardIndex.size(), (storyboardChartFileDir.empty() ? storyboardAssetDir : storyboardChartFileDir).c_str());
        }

        gsp<GL::TextureInfo> requestStoryboardTexture(const std::string& name) {
            if (name.empty()) return nullptr;
            if (auto it = storyboardTextures.find(name); it != storyboardTextures.end()) return it->second;
            if (missingStoryboardTextures.contains(name)) return nullptr;
            if (!glCtx || !sharedComp.textureDecoder) return nullptr;

            namespace fs = std::filesystem;

            // 谱名 -> 谱名.png(多数写名不带扩展名, 且 "builtin.line" 这种点的后面不是扩展名)
            std::vector<std::string> fileNames { name, name + ".png" };

            auto tryLoad = [&](const std::string& path) -> gsp<GL::TextureInfo> {
                Data data;
                if (!Data::MakeFromFile(data, path)) return nullptr;
                try {
                    auto decoded = sharedComp.textureDecoder(data);
                    auto tex = glCtx->createTextureFromDecoded(decoded, true);
                    std::fprintf(stderr, "[Storyboard] 贴图载入: %s (%dx%d) <- %s\n",
                        name.c_str(), (int)tex->width, (int)tex->height, path.c_str());
                    storyboardTextures[name] = tex;
                    return tex;
                } catch (...) { return nullptr; }
            };

            // ① 按搜索根顺序直查(谱面所在目录优先, 再 chartDir / other / 父目录链)
            for (auto& dir : storyboardSearchRoots) {
                for (auto& fileName : fileNames) {
                    auto path = (fs::path(dir) / fileName).lexically_normal().string();
                    if (auto tex = tryLoad(path)) return tex;
                }
            }

            // ② 向下自动查找兜底: 贴图可能埋在谱面目录的子目录里(zip 解压结构任意嵌套)
            buildStoryboardIndex();
            for (auto& fileName : fileNames) {
                std::string key = fileName;
                for (auto& c : key) c = (char)std::tolower((unsigned char)c);
                if (auto it = storyboardIndex.find(key); it != storyboardIndex.end()) {
                    if (auto tex = tryLoad(it->second)) return tex;
                }
            }

            std::fprintf(stderr, "[Storyboard] 贴图缺失, 透明跳过: %s\n", name.c_str());
            missingStoryboardTextures.insert(name);
            return nullptr;
        }

        // ================== [Intro] 载入信息页 (本家 Milthm.PlayLoadScreen) ==================
        // 反汇编依据: E:/grain_data/build/remtest/opening_animation_re.md
        //   Awake() 进场 18 步: Start[1] .And[2..6] .Then[7..18] .Delayed(1.0) .Then(keyEvent) .Delayed(0.05)
        //     组①[1]=Background.Color 0.25s MQuad ; [2..6]=JacketBack/SquareJacketBack.localScale 0.5s MBackOut
        //                                        + SongTitle/Difficulty.color 0.5s + SongTitle.rectTransform 0.5s MCircOut
        //     组②[7..18]=6 个 credits 文本 color 0.5s + 3 个 Title.rectTransform 0.5s MQuadOut
        //                                       + SongTitle/Difficulty.color 0.5s + SongTitle.rectTransform 0.5s MQuadOut
        //     Delayed(1.0) → keyEvent → Delayed(0.05) = 合计 ≈ 2.05s
        //   FinishAnimation() 退场 15 步 × 0.25s (未接: 先做进场)
        // 时间约定: 打歌 t >= 0; t < 0 为载入页阶段, introTime = t + introDuration
        // 时间线: [0,2.05) 进场 18 步 / [2.05, 3.0) 静止数据布局 / [3.0, 4.2) 退场(参考视频拟合) → 切打歌
        static constexpr float64 introDuration = 2.05;
        // 静止 hold: 真机此段为场景异步加载前段(设备相关), 取参考视频实测 0.95s(标题淡出起点)
        static constexpr float64 introHoldDuration = 0.95;
        // 退场(参考视频逐帧拟合, 视频 t=0.95 起): 标题/难度淡出 [0,0.25) → 间隙 0.70s(真机加载尾段)
        //   → 页面切出 [0.95,1.20): 卡片 0.45→0 + credits→WhiteClear + bg 同窗(0.25s Quad+In, 与二进制常量一致) → 1.20 切打歌
        static constexpr float64 introExitDuration = 1.2;
        static constexpr float64 introPreRoll = introDuration + introHoldDuration + introExitDuration;
        // introAftermath(收场后停留 3.0) / introLeadTotal(负时间总窗口) 在 namespace 顶层(本文件 calculateMilFrame 之前)
        inline static bool introEnabled = true;   // [CLI] --no-intro: 跳过开场信息页(渲染/计时/录制静音三处统一)

        gsp<GL::TextureInfo> introJacketTex;
        gsp<GL::TextureInfo> introJacketGrayTex;    // 灰度副本: 本家 bg 材质 PlayLoadingBackground = Custom/Grayscale(_Saturation=0), 灰度曲绘 × _Color(58/255)
        gsp<DecodedRGBATexture> introCoverDecoded;  // loadIllustion 保留的解码像素(开场页灰度背景用; zip/对话框/milcharts 全流程统一)
        bool introJacketTried = false;
        bool introBgBaked = false;                  // introJacketGrayTex 是否已烘入 tint+gamma 补偿(baked 时 bg 顶点色用 White)

        // 封面(Jacket): 沿谱面目录向上找 artwork/<Title> - <Composer>.png
        // 歌库布局: <Title> - <Composer>/charts/<难度>/<Title> - <Composer>.js  →  ../../artwork/<Title> - <Composer>.png
        gsp<GL::TextureInfo> requestIntroJacket() {
            if (introJacketTried) return introJacketTex;
            introJacketTried = true;
            if (!glCtx || !sharedComp.textureDecoder) return nullptr;

            // 灰度化: 本家 bg 材质 PlayLoadingBackground = Custom/Grayscale
            //   fragment: gray = dot(rgb,(0.299,0.587,0.114)); col = lerp(gray, rgb, _Saturation=0); out = col*_Color
            //   页面绘制时再乘 _Color(58/255) → 灰度在此预乘进纹理
            auto makeGray = [](DecodedRGBATexture& d) {
                // ⚠ 实测: gimage 解码 PNG 时已做 sRGB→linear(顶点色 c 的最终输出 = linear字节 x c,
                //   三轮 bake/顶点色组合输出完全一致证实)。本家(Unity Linear)bg 屏幕值 =
                //   encode(linear灰度 x srgb_decode(_Color=58/255)=0.0423) (参考帧 bg 亮斑 8-17 吻合)
                //   → 这里烘入 encode(linear灰度 x 0.0423), 绘制时顶点色用 White
                auto toSrgb = [](float64 x) { return x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055; };
                constexpr double colorLin = 0.0423;   // srgb_decode(58/255)
                for (size_t i = 0; i + 3 < d.data.size(); i += 4) {
                    double lin01 = (0.299 * d.data[i] + 0.587 * d.data[i + 1] + 0.114 * d.data[i + 2]) / 255.0;
                    uint8 v = (uint8)std::clamp(toSrgb(lin01 * colorLin) * 255.0 + 0.5, 0.0, 255.0);
                    d.data[i] = d.data[i + 1] = d.data[i + 2] = v;
                }
            };
            auto buildTextures = [&](const DecodedRGBATexture& dec) {
                DecodedRGBATexture gray = dec;
                makeGray(gray);
                introJacketTex = glCtx->createTextureFromDecoded(dec, true);
                introJacketGrayTex = glCtx->createTextureFromDecoded(gray, true);
                introBgBaked = true;
            };

            // 优先: loadIllustion 已解码的封面 (zip 播放/导出/对话框/milcharts 全流程统一入口)
            //   ⚠ 引擎在缺图时会塞一张 2x2 透明兜底纹理(render() 内), 尺寸 <=2 一律视为"无图"
            if (introCoverDecoded && introCoverDecoded->valid()
                && introCoverDecoded->width > 2 && introCoverDecoded->height > 2) {
                buildTextures(*introCoverDecoded);
                std::fprintf(stderr, "[Intro] 封面用已载入曲绘 (%dx%d)\n",
                    (int)introJacketTex->width, (int)introJacketTex->height);
                return introJacketTex;
            }

            namespace fs = std::filesystem;

            std::vector<std::string> names;
            if (!chart.meta.title.empty() && !chart.meta.composer.empty())
                names.push_back(chart.meta.title + " - " + chart.meta.composer);
            if (!chart.meta.title.empty()) names.push_back(chart.meta.title);
            if (names.empty()) return nullptr;

            std::vector<std::string> dirs;
            auto addDir = [&](const std::string& d) {
                if (d.empty()) return;
                auto n = fs::path(d).lexically_normal().make_preferred().string();
                for (auto& x : dirs) if (x == n) return;
                dirs.push_back(n);
            };
            const std::string base = storyboardChartFileDir.empty() ? storyboardAssetDir : storyboardChartFileDir;
            addDir(base + "/artwork");
            addDir(base);   // zip 解压后封面常与谱面同目录 (如 Dogbite: 1.js + 1.png)
            addDir(base + "/..");
            fs::path cur = fs::path(base).lexically_normal();
            for (int i = 0; i < 3 && cur.has_parent_path(); i++) {
                cur = cur.parent_path();
                addDir(cur.string() + "/artwork");
            }

            for (auto& d : dirs) {
                for (auto& n : names) {
                    const std::string cands[] = { n + ".png", n + ".jpg", n };
                    for (auto& fn : cands) {
                        auto p = (fs::path(d) / fn).lexically_normal().string();
                        Data data;
                        if (!Data::MakeFromFile(data, p)) continue;
                        try {
                            auto dec = sharedComp.textureDecoder(data);
                            buildTextures(dec);
                            std::fprintf(stderr, "[Intro] 封面载入: %s (%dx%d) <- %s\n",
                                n.c_str(), (int)introJacketTex->width, (int)introJacketTex->height, p.c_str());
                            return introJacketTex;
                        } catch (...) { break; }
                    }
                }
            }

            // 最终兜底: 引擎已载入曲绘 (无像素访问 → 灰度退化=原图, bg 色调略偏彩)
            if (sharedComp.illustionTexture
                && sharedComp.illustionTexture->width > 2
                && sharedComp.illustionTexture->height > 2) {
                introJacketTex = introJacketGrayTex = sharedComp.illustionTexture;
                introBgBaked = false;
                std::fprintf(stderr, "[Intro] 封面兜底用已载入曲绘 (%dx%d)\n",
                    (int)introJacketTex->width, (int)introJacketTex->height);
                return introJacketTex;
            }
            std::fprintf(stderr, "[Intro] 封面缺失, 跳过: %s\n", names[0].c_str());
            return nullptr;
        }

        // 载入信息页绘制; t = introTime ∈ [0, introPreRoll)
        // 三阶段: [0,2.05) 进场 18 步 / [2.05,3.98) 静止数据布局(参考帧 s/001) / [3.98,4.98) FinishAnimation 退场
        void drawMilIntroPage(GL::GL33Canvas& cvs, float64 t) {
            using namespace GL;
            auto S = calcConfig.screenSize;
            const float64 W = S.x, H = S.y;

            auto clamp01 = [](float64 x) { return std::clamp(x, 0.0, 1.0); };
            auto seg = [&](float64 start, float64 dur) { return clamp01((t - start) / dur); };
            // Milease 缓动对应
            auto eInQuad = [&](float64 x) { x = clamp01(x); return x * x; };
            auto eOutQuad = [&](float64 x) { x = clamp01(x); return 1.0 - (1.0 - x) * (1.0 - x); };
            auto eOutCirc = [&](float64 x) { x = clamp01(x); return std::sqrt(std::max(0.0, 1.0 - (x - 1.0) * (x - 1.0))); };
            auto eOutBack = [&](float64 x) {
                x = clamp01(x);
                const float64 c1 = 1.70158, c3 = c1 + 1.0;
                return 1.0 + c3 * std::pow(x - 1.0, 3) + c1 * std::pow(x - 1.0, 2);
            };

            // ---- 退场进度 (参考视频逐帧拟合 + 二进制常量互证; 缓动全部 Quad+In):
            //      标题/难度淡出 [0,0.25) / 页面切出 [0.95,1.20): 卡片坍缩 0.45→0 + credits→WhiteClear + bg 同窗
            //      (视频实测基线修正: 卡亮区比 0.842@2.00 → 0.566@2.07 → 0.110@2.13, Δu≈0.135/帧
            //       → Quad+In 0.25s, 起点 video 1.90 = exitT 0.95 —— 与二进制 FinishAnimation 0.25s 常量一致 ✓;
            //       标题淡出窗口 [0.95,1.20] 视频实测 ✓; 切换后打歌侧 2.13-2.40 渐显属打歌场景自身入场, 不在本页)
            const float64 exitT = t - introDuration - introHoldDuration;   // <0 = 退场未开始
            const float64 exitSmall = eInQuad((exitT - 0.95) / 0.25);
            const float64 exitBg = exitSmall;

            const float64 a = seg(0.0, 0.5);   // 组①进度
            const float64 b = seg(0.5, 0.5);   // 组②进度

            // ---- 第 1 步: Background 材质色 (MQuad 0.25s) (0,0,0,0)→(0,58,58,1); 退场 RGB(58,58,58)→透明 0.5s ----
            // 本家 Background = Image(Simple) + PlayLoadingBackground 材质(Custom/Grayscale):
            //   输出 = 灰度曲绘 × _Color(58/255), _MainTex_ST 恒等 → 曲绘直接拉伸满屏(无等比处理); 菱形=曲绘自身内容
            auto jacket = requestIntroJacket();
            auto jacketGray = introJacketGrayTex ? introJacketGrayTex : jacket;
            const float64 bgA = eInQuad(seg(0.0, 0.25)) * (1.0 - exitBg);
            const Color bgTint = Color { 58.0 / 255.0, 58.0 / 255.0, 58.0 / 255.0, 1.0 };

            if (jacketGray && jacketGray->height > 0) {
                cvs.drawRect({
                    .position = { 0.0, 0.0 },
                    .size = { W, H },
                    .color = (introBgBaked ? Color::White() : bgTint).applyAlpha(bgA),
                    .texture = jacketGray.get()
                });
            } else {
                cvs.drawRect({
                    .position = { 0.0, 0.0 },
                    .size = { W, H },
                    .color = bgTint.applyAlpha(bgA)
                });
            }
            if (t <= 0.0) return;

            // ---- 第 2/3 步: JacketBack/SquareJacketBack.localScale ----
            // 进场 MBackOut: 0 → 0.45 回弹; 退场 Milease: 0.45 → 0 (Vector3.zero, 0.25s Quad+In)
            float64 cardScale = eOutBack(a);
            if (exitT > 0.0) cardScale *= (1.0 - exitSmall);
            const Vec2 cardCenter { 0.4996 * W, 0.4279 * H };
            // 卡片尺寸必须保持曲绘原始宽高比(否则非 16:9 屏幕下曲绘被非等比拉伸:
            //   旧式 {0.4320*W, 0.4223*H} 两个系数独立按 W/H 缩放 → 卡片比 = 1.0230*screenRatio,
            //   21:9 卡片比 2.42 vs 曲绘 1.7778 ⇒ 横向拉 36%; 4:3 ⇒ 纵向拉 25%)。
            //   宽度沿用参考帧(1280x720)实测基准 0.4320*W, 高度由曲绘纹理原始比例推出 ⇒ 永远等比。
            const float64 cardAspect = (jacket && jacket->height > 0)
                ? (float64)jacket->width / jacket->height
                : (16.0 / 9.0);
            const float64 cardW = 0.4320 * W * cardScale;
            const Vec2 cardSize { cardW, cardW / cardAspect };
            const float64 cardRadius = 0.022 * H;   // 参考帧卡片圆角

            if (jacket) {
                cvs.drawRoundedRect({
                    .position = cardCenter - cardSize / 2.0,
                    .size = cardSize,
                    .color = Color::White(),
                    .texture = jacket.get()
                }, cardRadius);
            }

            // ---- 文本动画 (精确按 PlayLoadScreen.Awake 18 步规格; 缓动映射已对 Milease 源码核对:
            //      MQuad/Milease=Quad+In, MQuadOut=Quad+Out, MCircOut=Circ+Out, MBackOut=Back+Out) ----
            // 第4/5步  Milease(Quad+In) : SongTitle/Difficulty 色 WhiteClear → 1.0 / 0.705882(=180/255)
            // 第6步    MCircOut(加色)    : SongTitle.rect (0,+250) → (0,0) 上滑
            // 第7-12步 Milease(Quad+In) : 6 个 credit 元素 WhiteClear → 标签 0.705882 / 值 1.0
            // 第13-15步 MQuadOut(加色)   : 3 个标签列 (-250,0) → (0,0) 左滑入
            // 第16/17步 Milease(Quad+In): SongTitle/Difficulty 色 → WhiteClear (淡出, 不与 credits 同屏)
            // 第18步   MQuadOut(加色)    : SongTitle.rect = MetaData(含 Difficulty 子节点) (0,0) → (+250,0) 右滑
            // 之后 Delayed(1.0)→KeyEvent(触发载入)→Delayed(0.05)→ResetToInitialState(各段回 from:
            //   标题/难度复现, credits 隐藏), 最终数据布局(UpdateAppearance 行容器 x=Padding)
            //   (参考帧逐行实测: 标签左缘 0.2854W, 值列左缘 0.4042W; 行容器=标签+值一体, 步骤13-15 一起滑动;
            //    曲名/难度 CENTER 对齐中心 0.5W —— v/ 全分辨率 1-17 帧恒 0.4977W 实锤, 且 prefab SongTitle
            //    为全宽 stretch(aPos.x=0) 互证; 旧值 0.6151W 系 s/001 低分辨率帧误测, 实为步骤18右滑中途态)
            const float64 inA = eInQuad(a);
            const float64 outB = eInQuad(b);

            auto drawLabel = [&](const std::string& s, Vec2 pos, Vec2 anchor, float64 fontSize, float64 alpha) {
                if (s.empty() || alpha <= 0.0) return;
                TextManager::DrawTextConfig cfg {
                    .text = s,
                    .fontSize = fontSize,
                    .pos = pos,
                    .anchor = anchor,
                    .rotation = 0.0,
                    .scale = { 1.0, 1.0 },
                    .color = Color::White().applyAlpha(alpha)
                };
                textManager.drawText(cvs, cfg);
            };

            // 字号: 全程常数(二进制无字号动画); 曲名 ≈ credits ×1.3(参考帧行高实测), 难度与 credits 同级
            const float64 fontSizeTitle = W * 0.0235;
            const float64 fontSizeBody = W * 0.0181;
            const float64 rowsC[3] = { 0.7093, 0.7611, 0.8204 };   // 行中心(参考帧实测)
            const std::string titleTexts[3] = { "音乐创作", "插画设计", "谱面设计" };   // prefab 序列化原文(bundle 0x1a6c/0x1e9c/0x22bc)
            const std::string valueTexts[3] = { chart.meta.composer, chart.meta.artist, chart.meta.charter };

            if (t < introDuration) {
                // ---- 进场 18 步 ----
                const float64 titleA = (t < 0.5) ? inA : (1.0 - outB);               // 第4/16步
                const float64 diffA = (180.0 / 255.0) * ((t < 0.5) ? inA : (1.0 - outB)); // 第5/17步
                const float64 titleSlideY = (250.0 / 1080.0) * H * (1.0 - eOutCirc(a));   // 第6步: 从布局位上方 250 落下(Circ+Out)
                const float64 titleSlideX = (250.0 / 1920.0) * W * eOutQuad(b);           // 第18步: MetaData(含难度子节点)右滑
                const float64 rowSlideX = (250.0 / 1920.0) * W * (1.0 - eOutQuad(b));     // 第13-15步: 行容器(标签+值一体)从 -250 滑入
                const float64 creditA = eInQuad(b);                                       // 第7-12步

                // SongTitle + Difficulty (MetaData 整体: 第6步从上方落下 / 第18步一起右滑并淡出)
                // 布局中心 0.6151W(参考帧逐行实测, 居中对齐)
                drawLabel(chart.meta.title,
                    { 0.5 * W + titleSlideX, 0.7093 * H - titleSlideY }, { 0.5, 0.5 },
                    fontSizeTitle, titleA);

                drawLabel(chart.meta.getFinalDifficultyString(),
                    { 0.5 * W + titleSlideX, 0.7611 * H - titleSlideY }, { 0.5, 0.5 },
                    fontSizeBody, diffA);

                // credits: 行容器一体动画 —— 标签(α 0.705882)与值(α 1.0)同一 slideX
                for (int i = 0; i < 3; i++) {
                    const float64 y = rowsC[i] * H;
                    drawLabel(titleTexts[i], { 0.2854 * W - rowSlideX, y }, { 0.0, 0.5 },
                        fontSizeBody, creditA * (180.0 / 255.0));
                    drawLabel(valueTexts[i], { 0.4042 * W - rowSlideX, y }, { 0.0, 0.5 },
                        fontSizeBody, creditA);
                }
            } else {
                // ---- 静止数据布局 + 退场 ----
                // 标题/难度: 本家在 ResetToInitialState 后才复现(慢加载场), 快加载(v/)直到切场景都不复现;
                //   我们是固定时间轴, entry 末尾 alpha 已归 0, 若此处重绘则 2.05s 处 alpha 0→1 闪现 → 不复现
                //   (本家退场标题淡出常量 eInQuad(exitT/0.25) 记录于 opening_animation_re.md 备查)
                const float64 creditFade = 1.0 - exitSmall;
                for (int i = 0; i < 3; i++) {
                    const float64 y = rowsC[i] * H;
                    drawLabel(titleTexts[i], { 0.2854 * W, y }, { 0.0, 0.5 },
                        fontSizeBody, (180.0 / 255.0) * creditFade);
                    drawLabel(valueTexts[i], { 0.4042 * W, y }, { 0.0, 0.5 },
                        fontSizeBody, creditFade);
                }
            }
        }

        gsp<GL::GL33Context> glCtx;
        TakeOvererComponents::SharedComp sharedComp;
        GL::TextManager textManager;
        TakeOvererComponents::AudioManager audioManager;

        MilCalculateFrameConfig calcConfig;
        MilChart chart;
        MilCalculatedFrame calculatedFrame;

        uint64 circHitEffectTexSize = 512;
        uint64 circHitEffectTexsCount = 60;

        void init() {
            gassert::asrt(!!lineHeadTextureLoader, "MilTakeOverer: lineHeadTextureLoader is not set");
            gassert::asrt(!!noteTextureDataLoader, "MilTakeOverer: noteTextureDataLoader is not set");
            gassert::asrt(!!hitsoundDataLoader, "MilTakeOverer: hitsoundDataLoader is not set");
            gassert::asrt(!!pauseButtonTextureDataLoader, "MilTakeOverer: pauseButtonTextureDataLoader is not set");
            gassert::asrt(!!glCtx, "MilTakeOverer: glCtx is not set");

            textManager.glCtx = glCtx;

            sharedComp.check();
            textManager.check();
            audioManager.check();

            loadResources();
        }

        void loadIllustion(const Data& data) {
            auto decoded = sharedComp.textureDecoder(data);
            sharedComp.illustionTexture = glCtx->createTextureFromDecoded(decoded);
            introCoverDecoded = gsp<DecodedRGBATexture>(new DecodedRGBATexture(std::move(decoded)));  // 保留解码像素: 开场页灰度背景(zip/对话框/milcharts 全流程统一)
        }

        void loadIllustion(const std::string& path) { loadIllustion(Data::MakeFromFile(path)); }

        struct MixBgmConfig {
            float64 musicVol = 1.0, sfxVol = 1.0;
            bool sfxRandshake = false;
        };

        gsp<DecodedAudio> mixFinalBgm(const MilChart& chart, const MixBgmConfig& config) {
            if (!audioManager.bgmAudio) throw std::runtime_error("bgm is not loaded");

            auto result = audioManager.bgmAudio->copy();
            result->applyVolume(config.musicVol);
            
            std::mt19937 rng { std::random_device {} () };
            std::uniform_real_distribution<float64> sfxRandshakeDist { 0.0, 0.02 };

            for (const auto& line : chart.lines) {
                for (const auto& note : line.notes) {
                    if (note.isFake) continue;
                    // Fracture(闪电) 无打击音, 且未注册音源 -> 跳过(否则 .at 抛 out_of_range)
                    if (note.isFracture()) continue;

                    auto sfx = hitsoundAudios.at(note.type);
                    auto t = note.timeZone.x;
                    if (config.sfxRandshake) t += sfxRandshakeDist(rng);
                    result->overlapSecond(sfx, t, config.sfxVol);
                }
            }

            return result;
        }

        using ChartIniter = std::function<void(MilChart&)>;

        struct LoadChartConfig {
            Data data;
            ChartIniter initer = [](MilChart& chart) { chart.init(); };
        };

        // [Intro] 换谱面: 上一首的封面缓存全部失效(否则 GUI 切歌后开场曲绘仍显示旧图)
        void resetIntroJacketCache() {
            introJacketTried = false;
            introCoverDecoded = nullptr;
            introJacketTex = nullptr;
            introJacketGrayTex = nullptr;
            introBgBaked = false;
        }

        TakeOvererComponents::LoadChartResultInfo loadChart(const LoadChartConfig& config) {
            TakeOvererComponents::LoadChartResultInfo resultInfo {};
            resetIntroJacketCache();

            {
                gtime::Timer timer;

                try {
                    chart = loadMilChartFromData(config.data);
                } catch (const std::exception& e) {
                    resultInfo.success = false;
                    resultInfo.error = e.what();
                    return resultInfo;
                }

                resultInfo.createObjectTook = timer.elapsed();
            }

            {
                gtime::Timer timer;
                config.initer(chart);
                resultInfo.initTook = timer.elapsed();
            }

            return resultInfo;
        }

        // [Milize JS] 直接加载已构造的 MilChart（内嵌引擎产出，免 JSON/文件往返）
        TakeOvererComponents::LoadChartResultInfo loadChartFromMilChart(MilChart&& inChart, const std::function<void(MilChart&)>& initer) {
            TakeOvererComponents::LoadChartResultInfo resultInfo {};
            resetIntroJacketCache();

            {
                gtime::Timer timer;
                chart = std::move(inChart);
                resultInfo.createObjectTook = timer.elapsed();
            }

            {
                gtime::Timer timer;
                initer(chart);
                resultInfo.initTook = timer.elapsed();
            }

            return resultInfo;
        }

        // [Storyboard] 渲染贴图故事板(Picture, 按 layer 过滤)
        // 文字故事板不走这里: calculateMilFrame 的 calculateStoryboards 已把文字按世界->屏幕
        // 换算好并塞进 CalculatedText(由 renderSharedObject 统一绘制); 此处再画一次会重复且
        // 位置错误(旧实现直接把世界坐标当屏幕像素用)。
        void drawMilStoryboards(GL::GL33Canvas& cvs, float64 t, EnumMilStoryboardLayer layer) {
            for (auto& sb : chart.storyboardObjects) {
                if (sb.layer != layer) continue;
                if (sb.type != EnumMilStoryboardType::Picture) continue;
                float64 alpha = chart.animator.get(sb, t, EnumMilEventType::Transparency);
                if (alpha <= 0.0) continue;
                float64 size = chart.animator.get(sb, t, EnumMilEventType::Size);
                float64 rot = chart.animator.get(sb, t, EnumMilEventType::Rotation);
                float64 colorIdx = chart.animator.get(sb, t, EnumMilEventType::Color);
                auto colorZone = chart.animator.get_zone(sb, t, EnumMilEventType::Color);
                Color color = chart.storyboardAssets.getColor(colorIdx, colorZone);
                color.a *= alpha;

                {
                    // [Storyboard] 贴图缺失 -> 直接透明跳过(旧实现画纯色占位矩形, 会整屏遮挡视野)
                    auto tex = requestStoryboardTexture(sb.data);
                    if (!tex) continue;

                    // 4 角事件(默认 ±0.5) = 贴图归一化矩形; 贴图按原始像素尺寸绘制(1px = 1 世界单位)
                    float64 lbx = chart.animator.get(sb, t, EnumMilEventType::StoryBoardLeftBottomX);
                    float64 lby = chart.animator.get(sb, t, EnumMilEventType::StoryBoardLeftBottomY);
                    float64 rtx = chart.animator.get(sb, t, EnumMilEventType::StoryBoardRightTopX);
                    float64 rty = chart.animator.get(sb, t, EnumMilEventType::StoryBoardRightTopY);

                    Vec2 texSize { (float64)tex->width, (float64)tex->height };
                    Vec2 quadWorld = {
                        std::abs(rtx - lbx) * texSize.x * size,
                        std::abs(rty - lby) * texSize.y * size
                    };
                    Vec2 localOffset = {
                        (rtx + lbx) * 0.5 * texSize.x * size,
                        (rty + lby) * 0.5 * texSize.y * size
                    };

                    auto screenSize = calcConfig.screenSize;
                    // 世界 -> 屏幕(worldViewport.y 为负, y 轴翻转由此自动完成)
                    Vec2 center = chart.getObjectPosition(t, sb, screenSize)
                        + localOffset / chart.meta.worldViewport * screenSize;
                    Vec2 quad = quadWorld / chart.meta.worldViewport.abs() * screenSize;

                    cvs.save();
                    cvs.translate(center);
                    cvs.rotateDegrees(-rot);
                    cvs.drawRect({ .position = -quad / 2.0, .size = quad, .color = color, .texture = tex.get() });
                    cvs.restore();
                }
            }
        }

        struct RenderConfig {
            TakeOvererComponents::RenderConfigBase base;
        };

        struct RenderResultInfo {
            TakeOvererComponents::RenderResultInfoBase base;
        };

        RenderResultInfo& render(const RenderConfig& renderConfig) {
            calcConfig.songLength = audioManager.getBgmLength();
            calcConfig.backgroundTextureSize = { sharedComp.illustionTexture->width, sharedComp.illustionTexture->height };

            auto t = renderConfig.base.getTime(audioManager);
            if (!introEnabled && t < 0.0) t = 0.0;   // --no-intro: 无开场, 直接进打歌

            // [Intro] t < 0 = 载入信息页阶段(选歌后/进游玩前): 只画该页, 不画打歌内容
            // 反汇编依据见 remtest/opening_animation_re.md (Awake 18 步 ≈2.05s)
            // [-introAftermath, 0) = 本家收场后停留: 游戏画面 + 进度条从头滚到底, 落到下方正常渲染
            if (introEnabled && t < -introAftermath) {
                glCtx->setViewport(calcConfig.screenSize.x, calcConfig.screenSize.y);
                glCtx->gl.glClearColor(0.0, 0.0, 0.0, 1.0);
                glCtx->gl.glClear(GL::GL_COLOR_BUFFER_BIT);
                auto introCvs = GL::GL33Canvas::Make(glCtx.get());
                drawMilIntroPage(introCvs, std::clamp(t + introLeadTotal, 0.0, introPreRoll));
                if (renderConfig.base.flushGl) glCtx->gl.glFlush();
                return renderResultInfoCache;
            }

            {
                gtime::Timer timer;
                calculateMilFrame(chart, t, calcConfig, calculatedFrame);
                renderResultInfoCache.base.calculatedTook = timer.elapsed();
            }

            gtime::Timer glOpsTimer;

            using namespace GL;

            glCtx->setViewport(calcConfig.screenSize.x, calcConfig.screenSize.y);
            glCtx->gl.glClearColor(0.0, 0.0, 0.0, 0.0);
            glCtx->gl.glClear(GL_COLOR_BUFFER_BIT);

            auto cvs = GL33Canvas::Make(glCtx.get());

            cvs.drawRect({
                .position = { calculatedFrame.backgroundRect.x, calculatedFrame.backgroundRect.y },
                .size = { calculatedFrame.backgroundRect.w, calculatedFrame.backgroundRect.h },
                .texture = sharedComp.illustionTexture.get()
            });

            cvs.drawRect({
                .position = { calculatedFrame.backgroundRect.x, calculatedFrame.backgroundRect.y },
                .size = { calculatedFrame.backgroundRect.w, calculatedFrame.backgroundRect.h },
                .texture = backgroundMask.get()
            });

            // [Storyboard] Background 层：背景之上、轨道之下
            drawMilStoryboards(cvs, t, EnumMilStoryboardLayer::Background);

            for (auto& obj : calculatedFrame.objects) {
                if (TakeOvererComponents::renderSharedObject(obj, glCtx, cvs, textManager)) {
                    continue;
                }

                if (std::holds_alternative<MilCalculatedFrame::CalculatedLineHead>(obj)) {
                    auto& lineHead = std::get<MilCalculatedFrame::CalculatedLineHead>(obj);

                    cvs.save();
                    cvs.translate(lineHead.position);
                    cvs.rotateDegrees(lineHead.rotation);
                    cvs.scale(lineHead.scale);
                    cvs.drawRect({
                        .position = -Vec2 { lineHead.size, lineHead.size } / 2,
                        .size = { lineHead.size, lineHead.size },
                        .color = lineHead.color,
                        .texture = lineHeadTex.get()
                    });
                    cvs.restore();
                } else if (std::holds_alternative<MilCalculatedFrame::CalculatedLineBody>(obj)) {
                    auto& body = std::get<MilCalculatedFrame::CalculatedLineBody>(obj);

                    cvs.save();
                    cvs.translate(body.origin);
                    cvs.rotateDegrees(body.rotation);
                    cvs.scale(body.scale);
                    if (lineBodyTex) {
                        // C 方案:贴图作为 texColor,与 lineColor 相乘(shader: vColor*uColor*texture),
                        // 透明度已由 body.color(applyAlpha(lineBodyAlpha)) 按谱面渐隐
                        cvs.drawRect({
                            .position = body.position,
                            .size = body.size,
                            .color = body.color,
                            .texture = lineBodyTex.get()
                        });
                    } else {
                        // 贴图缺失兜底:纯色 lineColor,同样按 lineBodyAlpha 渐隐,不整段消失
                        cvs.drawRect({
                            .position = body.position,
                            .size = body.size,
                            .color = body.color
                        });
                    }
                    cvs.restore();
                } else if (std::holds_alternative<MilCalculatedFrame::CalculatedNote>(obj)) {
                    auto& note = std::get<MilCalculatedFrame::CalculatedNote>(obj);
                    auto& img = noteTextures.at(note.textureDesc);
                    auto& imgInfo = calcConfig.noteTextureInfos.at(note.textureDesc);

                    cvs.save();
                    cvs.translate(note.position);
                    cvs.rotateDegrees(note.rotation);

                    auto mesh = glCtx->requestMesh(6 * 3);
                    mesh.color = note.color;
                    mesh.texture = img.get();

                    mesh.addRect(
                        { -note.head, -note.width / 2 }, { note.head, note.width },
                        GLvec2 { 0.0, 0.0 },
                        GLvec2 { imgInfo.cutPadding.x, imgInfo.textureSize.y } / imgInfo.textureSize
                    );

                    mesh.addRect(
                        { 0.0, -note.width / 2 }, { note.body, note.width },
                        GLvec2 { imgInfo.cutPadding.x, 0.0 } / imgInfo.textureSize,
                        GLvec2 { imgInfo.textureSize.x - imgInfo.cutPadding.sum(), imgInfo.textureSize.y } / imgInfo.textureSize
                    );

                    mesh.addRect(
                        { note.body, -note.width / 2 }, { note.tail, note.width },
                        GLvec2 { imgInfo.textureSize.x - imgInfo.cutPadding.y, 0.0 } / imgInfo.textureSize,
                        GLvec2 { imgInfo.cutPadding.y, imgInfo.textureSize.y } / imgInfo.textureSize
                    );

                    cvs.drawMesh(mesh);
                    cvs.restore();
                } else if (std::holds_alternative<MilCalculatedFrame::CalculatedPauseButton>(obj)) {
                    auto& btn = std::get<MilCalculatedFrame::CalculatedPauseButton>(obj);

                    cvs.save();
                    cvs.translate(btn.position);
                    cvs.rotateDegrees(btn.rotation);
                    cvs.scale(btn.scale);
                    cvs.drawRect({
                        .position = -Vec2 { btn.size, btn.size } / 2,
                        .size = { btn.size, btn.size },
                        .color = btn.color,
                        .texture = pauseButtonTex.get()
                    });
                    cvs.restore();
                } else if (std::holds_alternative<MilCalculatedFrame::CalculatedParticles>(obj)) {
                    auto& particles = std::get<MilCalculatedFrame::CalculatedParticles>(obj);

                    if (particles.count == 0) continue;

                    auto* fxTex = particles.fxTexture == 0 ? fxLightning2Tex.get() : nullptr;

                    // *_ADD 材质 = Additive: SrcBlend=SrcAlpha / DstBlend=One
                    if (particles.additive) glCtx->gl.glBlendFunc(GL::GL_SRC_ALPHA, GL::GL_ONE);

                    if (fxTex != nullptr) {
                        // 贴图粒子(Fracture 特效): radius = (长, 宽) 半尺寸, 长边沿 rotation 方向
                        for (uint64 i = 0; i < particles.count; i++) {
                            auto& it = particles.items[i];
                            cvs.save();
                            cvs.translate(it.position);
                            cvs.rotateDegrees(it.rotation);
                            cvs.drawRect({
                                .position = -it.radius,
                                .size = it.radius * 2.0,
                                .color = it.color,
                                .texture = fxTex
                            });
                            cvs.restore();
                        }
                    } else {
                        auto mesh = glCtx->requestMesh(6 * particles.count);
                        mesh.program = glCtx->preloadedPrograms.circle.get();
                        mesh.color = GLvec4::White();

                        {
                            auto guard = mesh.program->use();
                            mesh.program->getUniformLocation("uRingRadius").setf(0.0);
                        }

                        for (uint64 i = 0; i < particles.count; i++) {
                            auto& it = particles.items[i];
                            Transform2D transform;
                            transform.translate(it.position);
                            transform.rotateDegrees(it.rotation);
                            mesh.addRectCentered({}, it.radius, { 0.5, 0.5 }, { 0.5, 0.5 }, it.color);
                            mesh.vertices.transformBack(transform, 6);
                        }

                        cvs.drawMesh(mesh);
                    }

                    if (particles.additive) glCtx->gl.glBlendFunc(GL::GL_SRC_ALPHA, GL::GL_ONE_MINUS_SRC_ALPHA);
                } else if (std::holds_alternative<MilCalculatedFrame::CalculatedHitEffectTexture>(obj)) {
                    auto& effect = std::get<MilCalculatedFrame::CalculatedHitEffectTexture>(obj);
                    auto& img = circHitEffectTexs[std::clamp<uint64>(effect.progress * circHitEffectTexs.size(), 0, circHitEffectTexs.size() - 1)];
                    
                    cvs.save();
                    cvs.translate(effect.position);
                    cvs.rotateDegrees(effect.rotation);
                    cvs.drawRect({
                        .position = -effect.size / 2,
                        .size = effect.size,
                        .color = effect.color,
                        .texture = img.get()
                    });
                    cvs.restore();
                }
            }

            // [Storyboard] Normal / Foreground 层：轨道之上、HUD 之下
            drawMilStoryboards(cvs, t, EnumMilStoryboardLayer::Normal);
            drawMilStoryboards(cvs, t, EnumMilStoryboardLayer::Foreground);

            cvs.drawRect({
                .position = { calculatedFrame.progressbarRect.x, calculatedFrame.progressbarRect.y },
                .size = { calculatedFrame.progressbarRect.w, calculatedFrame.progressbarRect.h },
                .color = Color::White(),
                .texture = progressbarTex.get()
            });

            if (renderConfig.base.flushGl) {
                glCtx->gl.glFlush();
            }

            renderResultInfoCache.base.glOperationsTook = glOpsTimer.elapsed();

            if (!renderConfig.base.disableHitsound) {
                for (auto& [type, count] : calculatedFrame.hitsounds) {
                    // Fracture 无打击音源, 防御性跳过(避免 .at 抛 out_of_range)
                    auto audioIt = hitsoundAudios.find(type);
                    if (audioIt == hitsoundAudios.end()) continue;
                    audioManager.playSfx(audioIt->second, count);
                }
            }

            return renderResultInfoCache;
        }

        private:
        RenderResultInfo renderResultInfoCache;
        gsp<GL::TextureInfo> backgroundMask;
        gsp<GL::TextureInfo> progressbarTex;
        gsp<GL::TextureInfo> lineHeadTex;
        gsp<GL::TextureInfo> lineBodyTex;
        std::unordered_map<EnumMilNoteType, gsp<DecodedAudio>> hitsoundAudios;
        std::map<MilNoteTextureDesc, gsp<GL::TextureInfo>> noteTextures;
        gsp<GL::TextureInfo> pauseButtonTex;
        gsp<GL::ProgramInfo> circHitEffectProg;
        std::vector<gsp<GL::TextureInfo>> circHitEffectTexs;
        gsp<GL::TextureInfo> fxLightning2Tex;   // Fracture 撞击特效: lightning2 512x128 (Additive)

        void loadResources() {
            using namespace GL;

            backgroundMask = glCtx->createTextureFromDecoded(spwanMilBackgroundMask());
            progressbarTex = glCtx->createTextureFromDecoded(spwanMilProgressbar());

            {
                auto lineHead = lineHeadTextureLoader();
                auto decoded = sharedComp.textureDecoder(lineHead.encoded);
                lineHeadTex = glCtx->createTextureFromDecoded(decoded, true);

                calcConfig.lineHeadScale = lineHead.scale;
                if (lineHead.connectPointIsPixel) lineHead.connectPoint /= decoded.height;
                calcConfig.lineHeadConnectPoint = lineHead.connectPoint;
            }

            {
                try {
                    auto data = Data::MakeFromGrain("geasy_phi/milthm/line.png");
                    if (!data.empty()) {
                        auto decoded = sharedComp.textureDecoder(data);
                        lineBodyTex = glCtx->createTextureFromDecoded(decoded, true);
                    }
                } catch (...) { lineBodyTex = nullptr; }
            }

            // Fracture 撞击特效贴图 (FractureExplode 四层; 颜色来自 ColorOverLifetime, 贴图本身纯白)
            {
                try {
                    auto data = Data::MakeFromGrain("geasy_phi/milthm/fx/lightning2.png");
                    if (!data.empty()) {
                        fxLightning2Tex = glCtx->createTextureFromDecoded(sharedComp.textureDecoder(data), true);
                    }
                } catch (...) { fxLightning2Tex = nullptr; }
            }

            for (const auto type : {
                EnumMilNoteType::Hit,
                EnumMilNoteType::Drag,
                EnumMilNoteType::Fracture
            }) {
                for (const auto isHold : { false, true }) {
                    for (const auto isSimul : { false, true }) {
                        for (const auto isAlwaysPerfect : { false, true }) {
                            for (const auto isActive : { false, true }) {
                                auto desc = MilNoteTextureDesc(type, isHold, isSimul, isAlwaysPerfect, isActive);
                                auto loadResult = noteTextureDataLoader(desc);
                                if (loadResult.encoded.empty()) continue;

                                auto decoded = sharedComp.textureDecoder(loadResult.encoded);
                                auto tex = glCtx->createTextureFromDecoded(decoded, true);
                                if (!loadResult.cutPaddingIsPixel) loadResult.cutPadding *= decoded.width;
                                if (loadResult.ignoreCutPadding) loadResult.cutPadding = Vec2 { (float64)decoded.width, (float64)decoded.width } / 2;

                                calcConfig.noteTextureInfos[desc] = {
                                    .textureSize = { (float64)decoded.width, (float64)decoded.height },
                                    .cutPadding = loadResult.cutPadding,
                                    .scaling = loadResult.scaling
                                };

                                noteTextures[desc] = tex;
                            }
                        }
                    }
                }
            }

            for (const auto type : {
                EnumMilNoteType::Hit,
                EnumMilNoteType::Drag
            }) {
                auto data = hitsoundDataLoader(type);
                hitsoundAudios[type] = audioManager.decodeAndCheck(data);
            }

            {
                auto btn = pauseButtonTextureDataLoader();
                auto decoded = sharedComp.textureDecoder(btn);
                pauseButtonTex = glCtx->createTextureFromDecoded(decoded, true);
            }

            circHitEffectProg = glCtx->createConfiguredProgram(GL33Context::CreateProgramConfig { .fragCode = R"(
#version 330 core

in vec2 fragTexCoord;

uniform float uProgress;
uniform float uSeed;

out vec4 outColor;

float rand(vec2 n) { 
    return fract(sin(dot(n, vec2(12.9898, 78.233))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    
    float a = rand(ip);
    float b = rand(ip + vec2(1.0, 0.0));
    float c = rand(ip + vec2(0.0, 1.0));
    float d = rand(ip + vec2(1.0, 1.0));
    
    vec2 u = fp * fp * (3.0 - 2.0 * fp);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float circularNoise(vec2 uv, float density, float seed) {
    vec2 center = uv - 0.5;
    float radius = length(center) * density;
    float angle = abs(atan(center.y, center.x));

    if (uv.y > 0.5) {
        angle += sin(angle) * 2.;
    }

    vec2 seedOffset = vec2(seed * 100.0, seed * 100.0);
    vec2 polarCoord = vec2(radius, angle) + seedOffset;
    
    float n = 0.0;
    n += noise(polarCoord) * 0.7;
    n += noise(polarCoord * 2.0) * 0.3;
    n += noise(polarCoord * 4.0) * 0.1;
    
    return n;
}

void main() {
    outColor = vec4(1.0);
    float l = length(fragTexCoord - 0.5);

    if (0.43 <= l && l <= 0.5) {
        float n = circularNoise(fragTexCoord, 50.0, uSeed);
        outColor.a *= (n < uProgress) ? 0.0 : 1.0;
    } else {
        outColor.a = 0.0;
    }
}
)" }.defaultColorVert());

            circHitEffectProg->fragConfig.textureUniformName = std::nullopt;
            circHitEffectProg->fragConfig.colorUniformName = std::nullopt;

            {
                static std::mt19937 rng { std::random_device {} () };
                std::uniform_real_distribution<float64> rng_dist { 0.0, 1.0 };

                auto mesh = glCtx->requestMesh(6);
                mesh.program = circHitEffectProg.get();
                mesh.color = GLvec4::White();

                {
                    auto progGuard = mesh.program->use();
                    mesh.program->getUniformLocation("uSeed").setf(rng_dist(rng));
                }

                for (uint64 i = 0; i < circHitEffectTexsCount; i++) {
                    auto p = (float64)i / (circHitEffectTexsCount - 1);
                    auto tex = glCtx->createTexture();
                    tex->use().image2D(circHitEffectTexSize, circHitEffectTexSize, nullptr);

                    {
                        auto progGuard = mesh.program->use();
                        mesh.program->getUniformLocation("uProgress").setf(p);
                    }

                    glCtx->renderIntoTexture(tex.get(), mesh);

                    tex->use().generateMipmap();
                    circHitEffectTexs.push_back(tex);
            if (!sharedComp.illustionTexture) {
                sharedComp.illustionTexture = backgroundMask;
            }
            if (!sharedComp.illustionTexture) {
                auto fallbackTex = DecodedRGBATexture::Make(2, 2);
                for (size_t bi = 0; bi < fallbackTex.data.size(); bi += 4)
                    fallbackTex.data[bi + 3] = 255;
                sharedComp.illustionTexture = glCtx->createTextureFromDecoded(fallbackTex);
            }
                }
            }
        }
    };

    enum class EnumRizNoteType {
        Tap,
        Drag,
        Hold
    };

    struct RizNoteTypeHelper {
        static EnumRizNoteType FromInt(uint64 type) {
            if (type == 0) return EnumRizNoteType::Tap;
            if (type == 1) return EnumRizNoteType::Drag;
            if (type == 2) return EnumRizNoteType::Hold;
            return EnumRizNoteType::Tap;
        }
    };

    struct RizTheme {
        Color bgColor;
        Color noteColor;
        Color uiColor;
    };

    struct RizChallengeTime {
        float64 checkPoint;
        Vec2 timeZone;
        float64 transTime;
    };

    struct RizEase {
        float64 (* func)(void*, float64);
        float64 (* intFunc)(void*, float64);
        void* context;
        bool isLinear = true;

        void loadFromType(uint64 type) {
            if (type <= 0 || type > 14) return;
            
            context = (void*)type;
            func = [](void* ctx, float64 p) { return EaseSet::Rizline::Official::easing((uint64)ctx, p); };
            intFunc = [](void* ctx, float64 p) { return EaseSet::Rizline::Official::easing_int((uint64)ctx, p); };

            if (type != 13 && type != 14) {
                isLinear = false;
            }
        }

        float64 applyValue(float64 p) const noexcept {
            return func ? func(context, p) : p;
        }

        float64 applyInt(float64 p) const noexcept {
            return intFunc ? intFunc(context, p) : (p * p / 2.0);
        }
    };

    struct RizColorPoint {
        float64 time;
        Color start, end;

        float64 getProgressAtTime(float64 nowTime, RizColorPoint* nextPoint = nullptr) const noexcept {
            if (!nextPoint) return 0.0;
            if (time == nextPoint->time) return 1.0;
            return std::clamp((nowTime - time) / (nextPoint->time - time), 0.0, 1.0);
        };

        Color valueAtTime(float64 nowTime, RizColorPoint* nextPoint = nullptr) const noexcept {
            auto p = getProgressAtTime(nowTime, nextPoint);
            return start + (end - start) * p;
        }
    };

    struct RizColorPointGroup {
        std::vector<RizColorPoint> points;

        auto& create() noexcept { return points.emplace_back(); }

        void init() {
            lastUpdatedTime = -std::numeric_limits<float64>::infinity();
            std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        }

        Color get(float64 nowTime) noexcept {
            update(nowTime);
            return currentValue;
        }

        private:
        uint64 currentIndex;
        Color currentValue;
        float64 lastUpdatedTime;

        void update(float64 nowTime) noexcept {
            if (points.empty()) return;

            if (lastUpdatedTime == nowTime) return;
            if (lastUpdatedTime > nowTime) rewindTo(nowTime);

            while (shouldAdvanceToNext(nowTime)) currentIndex++;

            auto& p = points[currentIndex];
            auto* np = currentIndex < points.size() - 1 ? &points[currentIndex + 1] : nullptr;
            currentValue = p.valueAtTime(nowTime, np);

            lastUpdatedTime = nowTime;
        }

        bool shouldAdvanceToNext(float64 nowTime) const noexcept {
            return (
                currentIndex < points.size() - 1
                && points[currentIndex + 1].time <= nowTime
            );
        }

        void rewindTo(float64 nowTime) noexcept {
            while (!shouldAdvanceToNext(nowTime) && currentIndex > 0) {
                currentIndex--;
            }
        }
    };

    struct RizKeyPoint {
        float64 time, value;
        RizEase ease;

        float64 cumulativeValueAtStart;

        float64 getProgressAtTime(float64 nowTime, RizKeyPoint* nextPoint = nullptr) const noexcept {
            if (!nextPoint) return 0.0;
            if (time == nextPoint->time) return 1.0;
            return std::clamp((nowTime - time) / (nextPoint->time - time), 0.0, 1.0);
        }

        float64 valueAtTime(float64 nowTime, RizKeyPoint* nextPoint = nullptr) const noexcept {
            if (!nextPoint) return value;
            auto p = getProgressAtTime(nowTime, nextPoint);
            p = ease.applyValue(p);
            return value + (nextPoint->value - value) * p;
        }

        float64 getIntegralValue(float64 nowTime, RizKeyPoint* nextPoint = nullptr) const noexcept {
            if (!nextPoint) return value * (nowTime - time);
            auto p = getProgressAtTime(nowTime, nextPoint);
            auto iv = ease.applyInt(p);

            // float64 res = (nextPoint->time - time) * (value * p + (nextPoint->value - value) * iv);
            float64 res = (nextPoint->time - time) * (value * p); // 这里忽略下一个 point 的值
            if (nowTime > nextPoint->time) res += nextPoint->value * (nowTime - nextPoint->time);
            if (nowTime < time) res -= value * (time - nowTime);
            return res;
        }
    };

    struct RizKeyPointGroup {
        std::vector<RizKeyPoint> points;
        bool isSpeed;

        auto& create() noexcept { return points.emplace_back(); }

        void init() {
            lastUpdatedTime = -std::numeric_limits<float64>::infinity();
            std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.time < b.time; });

            initSpeedCumul();
        }

        float64 get(float64 nowTime) noexcept {
            update(nowTime);
            return currentValue;
        }

        private:
        uint64 currentIndex;
        float64 currentValue;
        float64 lastUpdatedTime;

        void initSpeedCumul() {
            if (!isSpeed || points.empty()) return;

            float64 cumulativeValue = 0.0;

            for (uint64 i = 0; i < points.size(); i++) {
                auto& p = points[i];
                p.cumulativeValueAtStart = cumulativeValue;

                if (i < points.size() - 1) {
                    auto& np = points[i + 1];
                    cumulativeValue += p.getIntegralValue(np.time, &np);
                }
            }
        }

        void update(float64 nowTime) noexcept {
            if (points.empty()) return;

            if (lastUpdatedTime == nowTime) return;
            if (lastUpdatedTime > nowTime) rewindTo(nowTime);

            while (shouldAdvanceToNext(nowTime)) currentIndex++;

            auto& p = points[currentIndex];
            auto* np = currentIndex < points.size() - 1 ? &points[currentIndex + 1] : nullptr;

            if (isSpeed) {
                currentValue = p.cumulativeValueAtStart + p.getIntegralValue(nowTime, np);
            } else {
                currentValue = p.valueAtTime(nowTime, np);
            }

            lastUpdatedTime = nowTime;
        }

        bool shouldAdvanceToNext(float64 nowTime) const noexcept {
            return (
                currentIndex < points.size() - 1
                && points[currentIndex + 1].time <= nowTime
            );
        }

        void rewindTo(float64 nowTime) noexcept {
            while (!shouldAdvanceToNext(nowTime) && currentIndex > 0) {
                currentIndex--;
            }
        }
    };

    struct RizCanvasMove {
        int64 index;
        RizKeyPointGroup xPositions;
        RizKeyPointGroup speeds;

        void init() {
            speeds.isSpeed = true;
            xPositions.init();
            speeds.init();
        }
    };

    struct RizCameraMove {
        RizKeyPointGroup xPositions;
        RizKeyPointGroup scales;

        void init() {
            xPositions.init();
            scales.init();
        }
    };

    struct RizLinePoint {
        float64 time;
        float64 xPosition;
        Color color;
        RizEase ease;
        uint64 canvasIndex;

        float64 floorPosition;

        float64 getProgressAtTime(float64 nowTime, const RizLinePoint* nextPoint = nullptr) const noexcept {
            if (!nextPoint) return 0.0;
            if (time == nextPoint->time) return 1.0;
            return std::clamp((nowTime - time) / (nextPoint->time - time), 0.0, 1.0);
        }

        float64 getXPosition(float64 nowTime, std::vector<RizCanvasMove>& canvasMoves, const RizLinePoint* nextPoint = nullptr) const noexcept {
            auto p = getProgressAtTime(nowTime, nextPoint);
            p = ease.applyValue(p);

            if (!nextPoint) nextPoint = this;

            auto res = xPosition + (nextPoint->xPosition - xPosition) * p;
            auto& cv = canvasMoves[canvasIndex];
            auto& ncv = canvasMoves[nextPoint->canvasIndex];
            auto cvXPosition = cv.xPositions.get(nowTime);
            auto ncvXPosition = ncv.xPositions.get(nowTime);
            res += cvXPosition + (ncvXPosition - cvXPosition) * p;
            return res;
        }
    };

    struct RizLinePointGroup {
        std::vector<RizLinePoint> points;

        auto& create() noexcept { return points.emplace_back(); }

        void init() {
            lastUpdatedTime = -std::numeric_limits<float64>::infinity();
            std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        }

        float64 getXPosition(float64 nowTime, std::vector<RizCanvasMove>& canvasMoves) noexcept {
            update(nowTime, canvasMoves);
            return currentXPosition;
        }

        bool getRingVisible(float64 nowTime) const noexcept {
            return !points.empty() && points.front().time <= nowTime && nowTime <= points.back().time;
        }

        private:
        uint64 currentIndex;
        float64 currentXPosition;
        float64 lastUpdatedTime;

        void update(float64 nowTime, std::vector<RizCanvasMove>& canvasMoves) noexcept {
            if (points.empty()) return;

            if (lastUpdatedTime == nowTime) return;
            if (lastUpdatedTime > nowTime) rewindTo(nowTime);

            while (shouldAdvanceToNext(nowTime)) currentIndex++;

            auto& p = points[currentIndex];
            auto* np = currentIndex < points.size() - 1 ? &points[currentIndex + 1] : nullptr;

            currentXPosition = p.getXPosition(nowTime, canvasMoves, np);

            lastUpdatedTime = nowTime;
        }

        bool shouldAdvanceToNext(float64 nowTime) const noexcept {
            return (
                currentIndex < points.size() - 1
                && points[currentIndex + 1].time <= nowTime
            );
        }

        void rewindTo(float64 nowTime) noexcept {
            while (!shouldAdvanceToNext(nowTime) && currentIndex > 0) {
                currentIndex--;
            }
        }
    };

    struct RizNote {
        EnumRizNoteType type;
        Vec2 timeZone;
        uint64 tailCanvasIndex;
    };

    struct RizLine {
        RizLinePointGroup points;
        std::vector<RizNote> notes;
        RizColorPointGroup ringColors;
        RizColorPointGroup lineColors;

        void init() {
            points.init();
            ringColors.init();
            lineColors.init();
        }
    };

    struct RizChart {
        struct UserOptions {
            float64 lineRingY = 0.68;
            float64 lineRingSize = 0.032;
        };

        std::vector<RizTheme> themes;
        std::vector<RizChallengeTime> challengeTimes;
        std::vector<RizCanvasMove> canvasMoves;
        RizCameraMove cameraMove;
        std::vector<RizLine> lines;

        UserOptions options;

        void init() {
            for (auto& move : canvasMoves) {
                move.init();
            }

            cameraMove.init();

            for (auto& line : lines) {
                for (auto& p : line.points.points) {
                    auto& cv = canvasMoves[p.canvasIndex];
                    p.floorPosition = cv.speeds.get(p.time);
                }

                line.init();
            }
        }
    };

    RizChart loadRizChartFromOfficialJson(const Data& data) {
        auto err = [](const std::string& msg) {
            throw std::runtime_error(std::format("official: {}", msg));
        };

        auto loadColor = [&](const JsonNode& node, Color* dst) {
            if (!node.isObject()) err("color is not object");

            if (!node.hasKey("r")) err("missing r");
            if (!node["r"].isNumber()) err("r is not number");
            dst->r = node["r"].getNumber();

            if (!node.hasKey("g")) err("missing g");
            if (!node["g"].isNumber()) err("g is not number");
            dst->g = node["g"].getNumber();

            if (!node.hasKey("b")) err("missing b");
            if (!node["b"].isNumber()) err("b is not number");
            dst->b = node["b"].getNumber();

            if (!node.hasKey("a")) err("missing a");
            if (!node["a"].isNumber()) err("a is not number");
            dst->a = node["a"].getNumber();

            *dst /= 255.0;
        };

        auto jsonRoot = JsonNode::Parse(data);

        RizChart chart {};

        if (!jsonRoot.isObject()) err("root is not object");

        if (!jsonRoot.hasKey("themes")) err("missing themes");
        if (!jsonRoot["themes"].isArray()) err("themes is not array");
        auto& themesNode = jsonRoot["themes"];

        for (auto& themeNode : themesNode.getArray()) {
            if (!themeNode.isObject()) err("themes item is not an object");

            if (!themeNode.hasKey("colorsList")) err("missing colorsList");
            if (!themeNode["colorsList"].isArray()) err("colorsList is not array");
            auto& colorsListNode = themeNode["colorsList"];
            auto& colorsArr = colorsListNode.getArray();

            if (colorsArr.size() < 3) err("colorsList is too short");

            auto& theme = chart.themes.emplace_back();
            loadColor(colorsArr[0], &theme.bgColor);
            loadColor(colorsArr[1], &theme.noteColor);
            loadColor(colorsArr[2], &theme.uiColor);
        }

        struct BpmEvent {
            float64 time, bpm;
        };

        std::vector<BpmEvent> bpms;

        if (!jsonRoot.hasKey("bPM")) err("missing bPM");
        if (!jsonRoot["bPM"].isNumber()) err("bPM is not a number");
        auto baseBpm = jsonRoot["bPM"].getNumber();

        if (!jsonRoot.hasKey("bpmShifts")) err("missing bpmShifts");
        if (!jsonRoot["bpmShifts"].isArray()) err("bpmShifts is not array");
        auto& bpmShiftsNode = jsonRoot["bpmShifts"];

        for (auto& bpmShiftNode : bpmShiftsNode.getArray()) {
            if (!bpmShiftNode.isObject()) err("bpmShifts item is not an object");

            auto& e = bpms.emplace_back();
            
            if (!bpmShiftNode.hasKey("time")) err("missing time");
            if (!bpmShiftNode["time"].isNumber()) err("time is not a number");
            e.time = bpmShiftNode["time"].getNumber();

            if (!bpmShiftNode.hasKey("value")) err("missing value");
            if (!bpmShiftNode["value"].isNumber()) err("value is not a number");
            e.bpm = baseBpm * bpmShiftNode["value"].getNumber();
        }

        auto tick2sec = [&](float64 tick) {
            if (bpms.empty()) return tick * (60 / baseBpm);

            float64 sec = 0.0;

            for (uint64 i = 0; i < bpms.size(); i++) {
                auto& e = bpms[i];

                if (i != bpms.size() - 1) {
                    float64 dur = bpms[i + 1].time - e.time;

                    if (tick >= dur) {
                        sec += dur * (60.0 / e.bpm);
                        tick -= dur;
                    } else {
                        sec += tick * (60.0 / e.bpm);
                        break;
                    }
                } else {
                    sec += tick * (60.0 / e.bpm);
                }
            }

            return sec;
        };

        auto loadKeyPoints = [&](const JsonNode& node, RizKeyPointGroup* dst) {
            if (!node.isArray()) err("keyPoints is not array");

            for (auto& keyPointNode : node.getArray()) {
                if (!keyPointNode.isObject()) err("keyPoints item is not an object");

                auto& keyPoint = dst->create();

                if (!keyPointNode.hasKey("time")) err("missing time");
                if (!keyPointNode["time"].isNumber()) err("time is not number");
                keyPoint.time = tick2sec(keyPointNode["time"].getNumber());

                if (!keyPointNode.hasKey("value")) err("missing value");
                if (!keyPointNode["value"].isNumber()) err("value is not number");
                keyPoint.value = keyPointNode["value"].getNumber();

                if (!keyPointNode.hasKey("easeType")) err("missing easeType");
                if (!keyPointNode["easeType"].isNumber()) err("easeType is not number");
                keyPoint.ease.loadFromType(keyPointNode["easeType"].getNumber());
            }
        };

        auto loadColorPoints = [&](const JsonNode& node, RizColorPointGroup* dst) {
            if (!node.isArray()) err("colorPoints is not array");

            for (auto& colorPointNode : node.getArray()) {
                if (!colorPointNode.isObject()) err("colorPoints item is not an object");

                auto& colorPoint = dst->create();

                if (!colorPointNode.hasKey("time")) err("missing time");
                if (!colorPointNode["time"].isNumber()) err("time is not number");
                colorPoint.time = tick2sec(colorPointNode["time"].getNumber());

                if (!colorPointNode.hasKey("startColor")) err("missing startColor");
                loadColor(colorPointNode["startColor"], &colorPoint.start);

                if (!colorPointNode.hasKey("endColor")) err("missing endColor");
                loadColor(colorPointNode["endColor"], &colorPoint.end);
            }
        };

        if (!jsonRoot.hasKey("challengeTimes")) err("missing challengeTimes");
        if (!jsonRoot["challengeTimes"].isArray()) err("challengeTimes is not array");
        auto& chTimesNode = jsonRoot["challengeTimes"];

        for (auto& chTimeNode : chTimesNode.getArray()) {
            if (!chTimeNode.isObject()) err("challengeTimes item is not an object");

            auto& chTime = chart.challengeTimes.emplace_back();

            if (!chTimeNode.hasKey("checkPoint")) err("missing checkPoint");
            if (!chTimeNode["checkPoint"].isNumber()) err("checkPoint is not a number");
            chTime.checkPoint = chTimeNode["checkPoint"].getNumber();

            if (!chTimeNode.hasKey("start")) err("missing start");
            if (!chTimeNode["start"].isNumber()) err("start is not a number");
            chTime.timeZone.x = tick2sec(chTimeNode["start"].getNumber());

            if (!chTimeNode.hasKey("end")) err("missing end");
            if (!chTimeNode["end"].isNumber()) err("end is not a number");
            chTime.timeZone.y = tick2sec(chTimeNode["end"].getNumber());

            if (!chTimeNode.hasKey("transTime")) err("missing transTime");
            if (!chTimeNode["transTime"].isNumber()) err("transTime is not a number");
            chTime.transTime = chTimeNode["transTime"].getNumber();
        }

        if (!jsonRoot.hasKey("canvasMoves")) err("missing canvasMoves");
        if (!jsonRoot["canvasMoves"].isArray()) err("canvasMoves is not array");
        auto& canvasMovesNode = jsonRoot["canvasMoves"];

        std::unordered_map<int64, uint64> canvasIndexMap;

        for (auto& canvasMoveNode : canvasMovesNode.getArray()) {
            if (!canvasMoveNode.isObject()) err("canvasMoves item is not an object");

            auto& cvMove = chart.canvasMoves.emplace_back();

            if (!canvasMoveNode.hasKey("index")) err("missing index");
            if (!canvasMoveNode["index"].isNumber()) err("index is not a number");
            canvasIndexMap[canvasMoveNode["index"].getNumber()] = chart.canvasMoves.size() - 1;

            if (!canvasMoveNode.hasKey("xPositionKeyPoints")) err("missing xPositionKeyPoints");
            loadKeyPoints(canvasMoveNode["xPositionKeyPoints"], &cvMove.xPositions);

            if (!canvasMoveNode.hasKey("speedKeyPoints")) err("missing speedKeyPoints");
            loadKeyPoints(canvasMoveNode["speedKeyPoints"], &cvMove.speeds);
        }

        if (!jsonRoot.hasKey("cameraMove")) err("missing cameraMove");
        if (!jsonRoot["cameraMove"].isObject()) err("cameraMove is not object");
        auto& cameraMoveNode = jsonRoot["cameraMove"];

        if (!cameraMoveNode.hasKey("xPositionKeyPoints")) err("missing xPositionKeyPoints");
        loadKeyPoints(cameraMoveNode["xPositionKeyPoints"], &chart.cameraMove.xPositions);

        if (!cameraMoveNode.hasKey("scaleKeyPoints")) err("missing scaleKeyPoints");
        loadKeyPoints(cameraMoveNode["scaleKeyPoints"], &chart.cameraMove.scales);

        if (!jsonRoot.hasKey("lines")) err("missing lines");
        if (!jsonRoot["lines"].isArray()) err("lines is not array");
        auto& linesNode = jsonRoot["lines"];

        for (auto& lineNode : linesNode.getArray()) {
            if (!lineNode.isObject()) err("lines item is not an object");

            auto& line = chart.lines.emplace_back();

            if (!lineNode.hasKey("linePoints")) err("missing linePoints");
            if (!lineNode["linePoints"].isArray()) err("linePoints is not array");
            auto& linePointsNode = lineNode["linePoints"];

            for (auto& linePointNode : linePointsNode.getArray()) {
                if (!linePointNode.isObject()) err("linePoints item is not an object");

                auto& linePoint = line.points.create();

                if (!linePointNode.hasKey("time")) err("missing time");
                if (!linePointNode["time"].isNumber()) err("time is not a number");
                linePoint.time = tick2sec(linePointNode["time"].getNumber());

                if (!linePointNode.hasKey("xPosition")) err("missing xPosition");
                if (!linePointNode["xPosition"].isNumber()) err("xPosition is not a number");
                linePoint.xPosition = linePointNode["xPosition"].getNumber();

                if (!linePointNode.hasKey("color")) err("missing color");
                loadColor(linePointNode["color"], &linePoint.color);

                if (!linePointNode.hasKey("easeType")) err("missing easeType");
                if (!linePointNode["easeType"].isNumber()) err("easeType is not a number");
                linePoint.ease.loadFromType(linePointNode["easeType"].getNumber());

                if (!linePointNode.hasKey("canvasIndex")) err("missing canvasIndex");
                if (!linePointNode["canvasIndex"].isNumber()) err("canvasIndex is not a number");
                linePoint.canvasIndex = canvasIndexMap[linePointNode["canvasIndex"].getNumber()];
            }

            if (!lineNode.hasKey("notes")) err("missing notes");
            if (!lineNode["notes"].isArray()) err("notes is not array");
            auto& notesNode = lineNode["notes"];

            for (auto& noteNode : notesNode.getArray()) {
                if (!noteNode.isObject()) err("notes item is not an object");

                auto& note = line.notes.emplace_back();

                if (!noteNode.hasKey("type")) err("missing type");
                if (!noteNode["type"].isNumber()) err("type is not a number");
                note.type = RizNoteTypeHelper::FromInt(noteNode["type"].getNumber());

                if (!noteNode.hasKey("time")) err("missing time");
                if (!noteNode["time"].isNumber()) err("time is not a number");
                note.timeZone = Vec2(tick2sec(noteNode["time"].getNumber()));

                if (note.type == EnumRizNoteType::Hold) {
                    if (!noteNode.hasKey("otherInformations")) err("missing otherInformations");
                    if (!noteNode["otherInformations"].isArray()) err("otherInformations is not array");
                    auto& otherInfosNode = noteNode["otherInformations"];
                    auto& otherInfos = otherInfosNode.getArray();

                    if (otherInfos.size() < 2) err("otherInformations is too short");

                    if (!otherInfos[0].isNumber()) err("otherInformations[0] is not a number");
                    note.timeZone.y = tick2sec(otherInfos[0].getNumber());

                    if (!otherInfos[1].isNumber()) err("otherInformations[1] is not a number");
                    note.tailCanvasIndex = canvasIndexMap[otherInfos[1].getNumber()];
                }
            }

            if (!lineNode.hasKey("judgeRingColor")) err("missing judgeRingColor");
            loadColorPoints(lineNode["judgeRingColor"], &line.ringColors);

            if (!lineNode.hasKey("lineColor")) err("missing lineColor");
            loadColorPoints(lineNode["lineColor"], &line.lineColors);
        }

        return chart;
    }

    RizChart loadRizChartFromData(const Data& data) {
        std::vector<std::string> msgs;

        #define try_(func) \
            try { return func(data); } \
            catch (const std::exception& err) { msgs.push_back(err.what()); }
        
        try_(loadRizChartFromOfficialJson);

        std::string msg = "failures: \n";
        for (auto& m : msgs) msg += m + "\n";
        throw std::runtime_error(msg);
        return {};
        
        #undef try_
    }

    uint64 getRizLineSegCount(float64 dist) {
        uint64 n = 0;
        if (dist < 8.0) n = 3.0 + (dist / 8.0) * 2.0;
        else if (dist < 24.0) n = 5.0 + ((dist - 8.0) / 16.0) * 10.0;
        else if (dist < 400.0) n = 15.0 + ((dist - 24.0) / 376.0) * 17.0;
        else if (dist < 5000.0) n = 32.0 + ((dist - 400.0) / 4600.0) * 13.0;
        else if (dist < 20000.0) n = 45.0 - ((dist - 5000.0) / 15000.0) * 10.0;
        else if (dist < 100000.0) n = 35.0 - ((dist - 20000.0) / 80000.0) * 20.0;
        else n = 15.0 - ((dist - 100000.0) / 900000.0) * 10.0;
        return std::clamp<uint64>(n, 3, 50);
    }

    struct RizCalculateFrameConfig {
        Vec2 screenSize;
    };

    struct RizCalculatedFrame {
        UsingSharedCalculatedObjects;

        struct CalculatedLineRing {
            Vec2 position;
            float64 radius, width;
            Color color;
        };

        struct CalculatedNote {

        };

        using CalculatedObject = std::variant<
            ListSharedCalculatedObjects,
            CalculatedLineRing,
            CalculatedNote
        >;

        std::vector<CalculatedObject> objects;
        std::unordered_map<EnumRizNoteType, uint64> hitsounds;
        
        void culling(std::vector<CalculatedObject>& objects, const Rect& screenRect) noexcept {
            if (SharedCalculatedObjects::sharedCulling(objects, screenRect)) return;

            auto& obj = objects.back();
            
            if (std::holds_alternative<CalculatedNote>(obj)) {
                auto& note = std::get<CalculatedNote>(obj);
                // ...
            }
        }

        void addObject(std::vector<CalculatedObject>& objects, const CalculatedObject& obj, const Rect& screenRect) noexcept {
            objects.push_back(obj);
            culling(objects, screenRect);
        }

        struct Cache {
            struct CalculatedTheme {
                RizTheme theme;
                float64 transProgress;
            };

            struct LinePoint {
                Vec2 position;
                Color color;
                RizEase* easeRef;
                bool connectToNext;
            };

            std::vector<CalculatedTheme> themes;
            std::vector<CalculatedLineRing> lineRings;
            std::vector<LinePoint> linePoints;
            std::vector<CalculatedCapRoundedLineSet::Item> calculatedlines;
            std::vector<CalculatedNote> notes;

            void clear() {
                themes.clear();
                lineRings.clear();
                linePoints.clear();
                calculatedlines.clear();
                notes.clear();
            }
        };

        Cache cache;
    };

    void calculateRizFrame(
        RizChart& chart, float64 time,
        const RizCalculateFrameConfig& config,
        RizCalculatedFrame& frame
    ) {
        frame.objects.clear();
        frame.hitsounds.clear();
        frame.cache.clear();

        Rect screenArea = { 0.0, 0.0, config.screenSize.x, config.screenSize.y };

        frame.cache.themes.push_back({ .theme = chart.themes[0], .transProgress = 1.0 });

        for (auto& chTime : chart.challengeTimes) {
            if (!chTime.timeZone.include(time)) continue;

            float64 k = 3.0;
            auto pIn = 1.0 - std::pow(std::clamp(1.0 - (time - chTime.timeZone.x) / chTime.transTime, 0.0, 1.0), k);
            auto pOut = std::pow(std::clamp((chTime.timeZone.y - time) / chTime.transTime, 0.0, 1.0), k);

            frame.cache.themes.push_back({
                .theme = chart.themes[&chTime - &chart.challengeTimes.front() + 1],
                .transProgress = std::min(pIn, pOut)
            });
        }

        auto lineRingY = config.screenSize.y * chart.options.lineRingY;
        auto lineRingSize = config.screenSize.sum() * chart.options.lineRingSize;

        auto cameraXPosition = chart.cameraMove.xPositions.get(time);
        auto cameraScale = chart.cameraMove.scales.get(time);

        auto xApplyCamera = [&](float64 x) {
            return (x - cameraXPosition) * cameraScale;
        };

        for (auto& line : chart.lines) {
            if (line.points.getRingVisible(time)) {
                auto lineRingX = line.points.getXPosition(time, chart.canvasMoves);
                auto lineRingColor = line.ringColors.get(time);

                lineRingX = xApplyCamera(lineRingX);
                lineRingX = (lineRingX + 0.5) * config.screenSize.x;

                if (lineRingColor.a > 0.0) {
                    auto& ring = frame.cache.lineRings.emplace_back();
                    ring.position = { lineRingX, lineRingY };
                    ring.radius = lineRingSize * cameraScale / 2.0;
                    ring.width = lineRingSize * cameraScale * 0.078125;
                    ring.color = lineRingColor;

                    if (!quadStrictlyIntersectRect(makeQuadFromRectInfo({
                        .position = ring.position,
                        .size = ring.radius * 2.0
                    }).data(), screenArea)) frame.cache.lineRings.pop_back();
                }
            }

            auto lineColor = line.lineColors.get(time);

            for (auto& p : line.points.points) {
                auto& cv = chart.canvasMoves[p.canvasIndex];
                auto cvXPosition = cv.xPositions.get(time);
                auto cvFloorPosition = cv.speeds.get(time);

                Vec2 position = {
                    (xApplyCamera(p.xPosition + cvXPosition) + 0.5) * config.screenSize.x,
                    lineRingY - (p.floorPosition - cvFloorPosition) * cameraScale * config.screenSize.y
                };

                auto color = (lineColor * lineColor.a + p.color * (1.0 - lineColor.a)).setAlpha(p.color.a);

                frame.cache.linePoints.push_back({
                    .position = position,
                    .color = color,
                    .easeRef = &p.ease,
                    .connectToNext = &p != &line.points.points.back()
                });
            }
        }

        if (!frame.cache.linePoints.empty()) {
            for (uint64 i = 0; i < frame.cache.linePoints.size() - 1; i++) {
                auto& p = frame.cache.linePoints[i];
                auto& np = frame.cache.linePoints[i + 1];
                if (!p.connectToNext) continue;

                auto minX = std::min(p.position.x, np.position.x);
                auto maxX = std::max(p.position.x, np.position.x);
                auto minY = std::min(p.position.y, np.position.y);
                auto maxY = std::max(p.position.y, np.position.y);

                if (minX >= config.screenSize.x || maxX <= 0.0 || minY >= config.screenSize.y || maxY <= 0.0) continue;

                auto dist = (p.position - np.position).length();
                auto isLinear = p.easeRef->isLinear || p.position.x == np.position.x || p.position.y == np.position.y;
                uint64 segCount = isLinear ? 1 : getRizLineSegCount(dist);

                auto currPos = p.position;
                auto currColor = p.color;

                for (uint64 j = 1; j < segCount; j++) {
                    auto pg = j / (float64)segCount;

                    Vec2 nextPos = p.position + (np.position - p.position) * Vec2 { p.easeRef->applyValue(pg), pg };
                    auto nextColor = p.color + (np.color - p.color) * pg;

                    frame.cache.calculatedlines.push_back({
                        .p1 = currPos, .p2 = nextPos,
                        .c1 = currColor, .c2 = nextColor
                    });

                    currPos = nextPos;
                    currColor = nextColor;
                }

                frame.cache.calculatedlines.push_back({
                    .p1 = currPos, .p2 = np.position,
                    .c1 = currColor, .c2 = np.color
                });
            }
        }

        std::cout << frame.cache.calculatedlines.size() << '\n';

        for (auto& theme : frame.cache.themes) {
            frame.addObject(frame.objects, RizCalculatedFrame::CalculatedPoly::Make(
                Vec2(0.0), config.screenSize,
                theme.theme.bgColor
            ), screenArea);
            
            frame.objects.push_back(RizCalculatedFrame::CalculatedCapRoundedLineSet {
                .items = frame.cache.calculatedlines.data(),
                .count = frame.cache.calculatedlines.size(),
                .width = lineRingSize * 0.078125 * cameraScale
            });

            for (auto& ring : frame.cache.lineRings) {
                frame.objects.push_back(ring);
            }
        }
    }

    struct RizTakeOverer {
        RizTakeOverer() = default;
        RizTakeOverer(const RizTakeOverer&) = delete;
        RizTakeOverer& operator=(const RizTakeOverer&) = delete;
        RizTakeOverer(RizTakeOverer&&) = default;
        RizTakeOverer& operator=(RizTakeOverer&&) = default;

        static gsp<RizTakeOverer> Make() {
            auto* tor = new RizTakeOverer();
            return gsp<RizTakeOverer>(tor);
        }

        using HitsoundDataLoader = std::function<Data(EnumRizNoteType)>;
        HitsoundDataLoader hitsoundDataLoader;

        gsp<GL::GL33Context> glCtx;
        TakeOvererComponents::SharedComp sharedComp;
        GL::TextManager textManager;
        TakeOvererComponents::AudioManager audioManager;

        RizCalculateFrameConfig calcConfig;
        RizChart chart;
        RizCalculatedFrame calculatedFrame;

        void init() {
            gassert::asrt(!!hitsoundDataLoader, "RizTakeOverer: hitsoundDataLoader is not set");
            gassert::asrt(!!glCtx, "RizTakeOverer: glCtx is not set");

            textManager.glCtx = glCtx;

            sharedComp.check();
            textManager.check();
            audioManager.check();

            loadResources();
        }

        void loadIllustion(const Data& data) {
            auto decoded = sharedComp.textureDecoder(data);
            sharedComp.illustionTexture = glCtx->createTextureFromDecoded(decoded);
        }

        void loadIllustion(const std::string& path) { loadIllustion(Data::MakeFromFile(path)); }

        struct MixBgmConfig {
            float64 musicVol = 1.0, sfxVol = 1.0;
        };

        gsp<DecodedAudio> mixFinalBgm(const RizChart& chart, const MixBgmConfig& config) {
            if (!audioManager.bgmAudio) throw std::runtime_error("bgm is not loaded");

            auto result = audioManager.bgmAudio->copy();
            result->applyVolume(config.musicVol);
            
            // ...

            return result;
        }

        using ChartIniter = std::function<void(RizChart&)>;

        struct LoadChartConfig {
            Data data;
            ChartIniter initer = [](RizChart& chart) { chart.init(); };
        };

        TakeOvererComponents::LoadChartResultInfo loadChart(const LoadChartConfig& config) {
            TakeOvererComponents::LoadChartResultInfo resultInfo {};

            {
                gtime::Timer timer;

                try {
                    chart = loadRizChartFromData(config.data);
                } catch (const std::exception& e) {
                    resultInfo.success = false;
                    resultInfo.error = e.what();
                    return resultInfo;
                }

                resultInfo.createObjectTook = timer.elapsed();
            }

            {
                gtime::Timer timer;
                config.initer(chart);
                resultInfo.initTook = timer.elapsed();
            }

            return resultInfo;
        }

        struct RenderConfig {
            TakeOvererComponents::RenderConfigBase base;
        };

        struct RenderResultInfo {
            TakeOvererComponents::RenderResultInfoBase base;
        };

        RenderResultInfo& render(const RenderConfig& renderConfig) {
            auto t = renderConfig.base.getTime(audioManager);

            {
                gtime::Timer timer;
                calculateRizFrame(chart, t, calcConfig, calculatedFrame);
                renderResultInfoCache.base.calculatedTook = timer.elapsed();
            }
            
            gtime::Timer glOpsTimer;

            using namespace GL;

            glCtx->setViewport(calcConfig.screenSize.x, calcConfig.screenSize.y);
            glCtx->gl.glClearColor(0.0, 0.0, 0.0, 0.0);
            glCtx->gl.glClear(GL_COLOR_BUFFER_BIT);

            auto cvs = GL33Canvas::Make(glCtx.get());

            for (auto& obj : calculatedFrame.objects) {
                if (TakeOvererComponents::renderSharedObject(obj, glCtx, cvs, textManager)) {
                    continue;
                }

                if (std::holds_alternative<RizCalculatedFrame::CalculatedLineRing>(obj)) {
                    auto& ring = std::get<RizCalculatedFrame::CalculatedLineRing>(obj);

                    auto mesh = glCtx->requestMesh(6);
                    mesh.program = glCtx->preloadedPrograms.circle.get();
                    mesh.color = ring.color;
                    mesh.addRectCentered(ring.position, ring.radius + ring.width / 2, { 0.5, 0.5 }, { 0.5, 0.5 });
                    
                    {
                        auto guard = mesh.program->use();
                        mesh.program->getUniformLocation("uRingRadius").setf((ring.radius - ring.width / 2) / (ring.radius + ring.width / 2) / 2.0);
                    }

                    cvs.drawMesh(mesh);
                } else if (std::holds_alternative<RizCalculatedFrame::CalculatedCapRoundedLineSet>(obj)) {
                    auto& lineSet = std::get<RizCalculatedFrame::CalculatedCapRoundedLineSet>(obj);
                    auto halfWidth = lineSet.width / 2.0;

                    auto mesh = glCtx->requestMesh(6 * lineSet.count);
                    mesh.color = GLvec4::White();
                    
                    for (uint64 i = 0; i < lineSet.count; i++) {
                        auto& it = lineSet.items[i];

                        auto dir = it.p2 - it.p1;
                        auto len = dir.length();
                        if (len != 0.0) dir /= len;
                        dir = { -dir.y, dir.x };

                        GLvec2 p1 = it.p1 + dir * halfWidth;
                        GLvec2 p2 = it.p1 - dir * halfWidth;
                        GLvec2 p3 = it.p2 - dir * halfWidth;
                        GLvec2 p4 = it.p2 + dir * halfWidth;

                        *mesh.vnext() = { p1, { 0.5, 0.5 }, it.c1 };
                        *mesh.vnext() = { p2, { 0.5, 0.5 }, it.c1 };
                        *mesh.vnext() = { p3, { 0.5, 0.5 }, it.c2 };

                        *mesh.vnext() = { p1, { 0.5, 0.5 }, it.c1 };
                        *mesh.vnext() = { p4, { 0.5, 0.5 }, it.c2 };
                        *mesh.vnext() = { p3, { 0.5, 0.5 }, it.c2 };
                    }

                    cvs.drawMesh(mesh);
                }
            }

            if (renderConfig.base.flushGl) {
                glCtx->gl.glFlush();
            }

            renderResultInfoCache.base.glOperationsTook = glOpsTimer.elapsed();

            if (!renderConfig.base.disableHitsound) {
                for (auto& [type, count] : calculatedFrame.hitsounds) {
                    audioManager.playSfx(hitsoundAudios.at(type), count);
                }
            }

            return renderResultInfoCache;
        }

        private:
        RenderResultInfo renderResultInfoCache;
        std::unordered_map<EnumRizNoteType, gsp<DecodedAudio>> hitsoundAudios;

        void loadResources() {
            using namespace GL;

            for (const auto type : {
                EnumRizNoteType::Tap,
                EnumRizNoteType::Drag,
                EnumRizNoteType::Hold
            }) {
                auto data = hitsoundDataLoader(type);
                hitsoundAudios[type] = audioManager.decodeAndCheck(data);
            }
        }
    };

    #undef UsingSharedCalculatedObjects
    #undef ListSharedCalculatedObjects

    struct PhiStaticResourceHelpers {
        static PhiTakeOverer::NoteTextureDataLoaderResult noteTextureDataLoader(const PhiTakeOverer::NoteTextureDataLoaderConfig& config) {
            std::unordered_map<EnumPhiNoteType, std::string> nameMap = {
                { EnumPhiNoteType::Tap, "click" },
                { EnumPhiNoteType::Drag, "drag" },
                { EnumPhiNoteType::Flick, "flick" },
                { EnumPhiNoteType::Hold, "hold" }
            };

            auto name = nameMap.at(config.type);
            auto key = std::string("/notes/") + name + (config.isSimul ? "_mh.png" : ".png");
            auto data = Data::MakeFromGrain("geasy_phi/phigros" + key);

            float64 cutPadding = config.isSimul ? 100.0 : 50.0;

            return {
                .encoded = std::move(data),
                .cutPadding = Vec2 { cutPadding, cutPadding },
                .ignoreCutPadding = config.type != EnumPhiNoteType::Hold
            };
        }

        static std::vector<Data> hitEffectDataLoader() {
            std::vector<Data> result;

            for (uint64 i = 0; i < 60; i++) {
                auto key = std::string("/hittexs/") + std::to_string(i + 1) + ".png";
                result.push_back(Data::MakeFromGrain("geasy_phi/phigros" + key));
            }

            return result;
        }

        static Data hitsoundDataLoader(EnumPhiNoteType type) {
            std::unordered_map<EnumPhiNoteType, std::string> nameMap = {
                { EnumPhiNoteType::Tap, "click" },
                { EnumPhiNoteType::Drag, "drag" },
                { EnumPhiNoteType::Flick, "flick" },
                { EnumPhiNoteType::Hold, "click" }
            };

            auto name = nameMap.at(type);
            auto key = std::string("/hitsounds/") + name + ".wav";
            return Data::MakeFromGrain("geasy_phi/phigros" + key);
        }

        static bool getBuiltinShader(const std::string& name, Data& dst) {
            std::unordered_set<std::string> builtinShaders = {
                "chromatic", "circleBlur", "fisheye",
                "glitch", "grayscale", "noise",
                "pixel", "radialBlur", "shockwave", "vignette"
            };

            if (builtinShaders.contains(name)) {
                auto key = std::string("/shaders/") + name + ".glsl";
                dst = Data::MakeFromGrain("geasy_phi/phigros" + key);
                return true;
            }

            return false;
        }

        static PhiTakeOverer::ShaderDataLoader createShaderDataLoaderFromChartDir(const std::function<std::string()>& chartDirProvider) {
            return [=](const std::string& name) -> std::string {
                Data shaderText;

                if (!getBuiltinShader(name, shaderText)) {
                    auto path = PhiStoryboardHelpers::nameToPath(chartDirProvider(), name);
                    if (!Data::MakeFromFile(shaderText, path)) {
                        throw std::runtime_error(std::format("failed to read shader: {}", path));
                    }
                }

                return shaderText.toString();
            };
        }

        static PhiTakeOverer::StoryboardDataLoader createStoryboardDataLoaderFromChartDir(const std::function<std::string()>& chartDirProvider) {
            return [=](const std::string& name) -> Data {
                auto path = PhiStoryboardHelpers::nameToPath(chartDirProvider(), name);
                
                Data data;
                if (!Data::MakeFromFile(data, path)) {
                    throw std::runtime_error(std::format("failed to read storyboard: {}", path));
                }

                return data;
            };
        }

        static Data getFontData() {
            return Data::MakeFromGrain("geasy_phi/phigros/font.ttf");
        }
    };
    
    struct MilStaticResourceHelpers {
        static MilTakeOverer::LineHeadTextureLoaderResult lineHeadTextureLoader() {
            return {
                .encoded = Data::MakeFromGrain("geasy_phi/milthm/line_head.png"),
                .connectPoint = 334.0
            };
        }

        static MilTakeOverer::NoteTextureDataLoaderResult noteTextureDataLoader(const MilNoteTextureDesc& config) {
            auto type = std::get<MilNoteTextureDescAttrType>(config);
            auto isHold = std::get<MilNoteTextureDescAttrIsHold>(config);
            auto isSimul = std::get<MilNoteTextureDescAttrIsSimul>(config);
            auto isAlwaysPerfect = std::get<MilNoteTextureDescAttrIsAlwaysPerfect>(config);
            auto isActive = std::get<MilNoteTextureDescAttrIsActive>(config);

            // 已判定(激活)材质: 所有 Hit 系 hold(标准/ex/alwaysPerfect/simul)统一注册 builtin.hold_active.png
            // (素材仅此一张 active; 各 hold base 图均 4036x~1340 ratio~3.0 与 active 1009x335 同构, 可直接换)
            if (isActive) {
                if (type == EnumMilNoteType::Hit && isHold) {
                    Data data;
                    try {
                        data = Data::MakeFromGrain("geasy_phi/milthm/notes/builtin.hold_active.png");
                    } catch (...) {}
                    if (data.empty()) return { .encoded = {}, .cutPadding = Vec2(668.0), .ignoreCutPadding = false, .scaling = Vec2(1.77) };
                    return {
                        .encoded = std::move(data),
                        .cutPadding = Vec2(167.0),   // 与 hold.png 同构, 按 1009/4036 比例
                        .ignoreCutPadding = false,
                        .scaling = Vec2(1.77)
                    };
                }
                return { .encoded = {}, .cutPadding = Vec2(668.0), .ignoreCutPadding = false, .scaling = Vec2(1.77) };
            }

            // Fracture(闪电音符) 不走皮肤系统, 也无 ex/double 变体, 固定用 notes/fracture.png
            if (type == EnumMilNoteType::Fracture) {
                Data fracData;
                try {
                    fracData = Data::MakeFromGrain("geasy_phi/milthm/notes/fracture.png");
                } catch (...) {}
                return {
                    .encoded = std::move(fracData),
                    .cutPadding = Vec2(668.0),
                    .ignoreCutPadding = true,
                    .scaling = Vec2(1.77)
                };
            }

            std::string name = isAlwaysPerfect ? "ex" : "";

            if (type == EnumMilNoteType::Hit) name += isHold ? "hold" : "tap";
            else name += "drag";

            if (isSimul) name += "_double";

            auto key = std::string("/notes/") + name + ".png";
            Data data;

            try {
                data = Data::MakeFromGrain("geasy_phi/milthm" + key);
            } catch (...) {}

            return {
                .encoded = std::move(data),
                .cutPadding = Vec2(668.0),
                .ignoreCutPadding = !isHold,
                .scaling = Vec2(1.77)
            };
        }

        static Data hitsoundDataLoader(EnumMilNoteType type) {
            std::unordered_map<EnumMilNoteType, std::string> nameMap = {
                { EnumMilNoteType::Hit, "hit" },
                { EnumMilNoteType::Drag, "drag" },
            };

            auto name = nameMap.at(type);
            auto key = std::string("/hitsounds/") + name + ".ogg";
            return Data::MakeFromGrain("geasy_phi/milthm" + key);
        }

        static Data getFontData() {
            return Data::MakeFromGrain("geasy_phi/milthm/font.ttf");
        }

        static Data pauseButtonTextureDataLoader() {
            return Data::MakeFromGrain("geasy_phi/milthm/pause.png");
        }
    };

    struct RizStaticResourceHelpers {
        static Data hitsoundDataLoader(EnumRizNoteType type) {
            std::unordered_map<EnumRizNoteType, std::string> nameMap = {
                { EnumRizNoteType::Tap, "hit" },
                { EnumRizNoteType::Drag, "drag" },
                { EnumRizNoteType::Hold, "hit" }
            };

            auto name = nameMap.at(type);
            auto key = std::string("/hitsounds/") + name + ".wav";
            return Data::MakeFromGrain("geasy_phi/rizline" + key);
        }
    };
}
