#include "player/MpvCore.hpp"

#include <borealis.hpp>
#include <clocale>
#include <cmath>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "api/HttpClient.hpp"
#include "core/Background.hpp"
#include "core/TextEncoding.hpp"

namespace {

const char* VERTEX_SHADER =
    "#version 150 core\n"
    "in vec3 aPos;\n"
    "in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 1.0);\n"
    "    TexCoord = aTexCoord;\n"
    "}\n";

const char* FRAGMENT_SHADER =
    "#version 150 core\n"
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D ourTexture;\n"
    "uniform float Alpha;\n"
    "void main() {\n"
    "    FragColor = texture(ourTexture, TexCoord);\n"
    "    FragColor.a = Alpha;\n"
    "}\n";

enum Observed : uint64_t { CORE_IDLE = 1, PAUSE, TIME_POS, DURATION, HWDEC, PAUSED_FOR_CACHE, CACHE_DURATION };

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        brls::Logger::error("mpv: shader compile failed: {}", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void* getProcAddress(void*, const char* name) { return (void*)glfwGetProcAddress(name); }

void checkError(int status, const char* what) {
    if (status < 0) brls::Logger::error("mpv: {} failed: {}", what, mpv_error_string(status));
}

}  // namespace

MpvCore& MpvCore::instance() {
    static MpvCore core;
    return core;
}

MpvCore::MpvCore() {
    // mpv requires the C numeric locale
    setlocale(LC_NUMERIC, "C");
    mpv = mpv_create();
    if (!mpv) brls::fatal("mpv: could not create handle");

    mpv_set_option_string(mpv, "vo", "libmpv");
    mpv_set_option_string(mpv, "idle", "yes");
    mpv_set_option_string(mpv, "keep-open", "yes");
    mpv_set_option_string(mpv, "osd-level", "0");
    mpv_set_option_string(mpv, "ytdl", "no");
    mpv_set_option_string(mpv, "audio-channels", "stereo");
    mpv_set_option_string(mpv, "hr-seek", "yes");
    mpv_set_option_string(mpv, "reset-on-next-file", "speed,pause");
    mpv_set_option_string(mpv, "video-timing-offset", "0");
    // Prefer Portuguese tracks when the stream offers them
    mpv_set_option_string(mpv, "alang", "pt-BR,pt,por,en,eng");
    mpv_set_option_string(mpv, "slang", "pt-BR,pt,por");

    // Network cache: the Switch Wi-Fi is unstable
    mpv_set_option_string(mpv, "cache", "yes");
    mpv_set_option_string(mpv, "demuxer-max-bytes", "64MiB");
    mpv_set_option_string(mpv, "demuxer-max-back-bytes", "32MiB");
    // Low values break some live streams
    mpv_set_option_string(mpv, "demuxer-lavf-analyzeduration", "0.4");
    mpv_set_option_string(mpv, "demuxer-lavf-probescore", "24");

#ifdef __SWITCH__
    // No fontconfig on the Switch: libass loads the fonts from romfs and uses the system font
    // (romfs:/font/font.ttf, see CMakeLists.txt) for subtitles
    mpv_set_option_string(mpv, "sub-font-provider", "none");
    mpv_set_option_string(mpv, "sub-fonts-dir", "romfs:/font");
    mpv_set_option_string(mpv, "osd-fonts-dir", "romfs:/font");
    mpv_set_option_string(mpv, "sub-font", "nintendo_udsg-r_std_003");
    // The OpenGL libmpv build has no nvtegra interop (only the deko3d one does): decode on NVDEC and copy
    // the frames back, otherwise mpv silently falls back to software decoding
    mpv_set_option_string(mpv, "hwdec", "nvtegra-copy");
    mpv_set_option_string(mpv, "vd-lavc-dr", "no");
    mpv_set_option_string(mpv, "vd-lavc-threads", "4");
    // Recommended by wiliwili to avoid random crashes
    mpv_set_option_string(mpv, "opengl-glfinish", "yes");
    // Cheaper scaling: the Tegra GPU is shared with the UI
    if (mpv_client_api_version() >= MPV_MAKE_VERSION(2, 2)) {
        mpv_set_option_string(mpv, "profile", "fast");
    } else {
        mpv_set_option_string(mpv, "scale", "bilinear");
        mpv_set_option_string(mpv, "dscale", "bilinear");
        mpv_set_option_string(mpv, "dither", "no");
        mpv_set_option_string(mpv, "correct-downscaling", "no");
        mpv_set_option_string(mpv, "linear-downscaling", "no");
        mpv_set_option_string(mpv, "sigmoid-upscaling", "no");
        mpv_set_option_string(mpv, "hdr-compute-peak", "no");
    }
#else
    mpv_set_option_string(mpv, "hwdec", "auto-safe");
#endif

    if (mpv_initialize(mpv) < 0) {
        mpv_terminate_destroy(mpv);
        brls::fatal("mpv: could not initialize");
    }
    // Warnings from everything, plus the decoder choice (vd) to confirm hardware decoding
    mpv_set_option_string(mpv, "msg-level", "all=warn,vd=v");
    mpv_request_log_messages(mpv, "v");

    checkError(mpv_observe_property(mpv, CORE_IDLE, "core-idle", MPV_FORMAT_FLAG), "observe");
    checkError(mpv_observe_property(mpv, PAUSE, "pause", MPV_FORMAT_FLAG), "observe");
    checkError(mpv_observe_property(mpv, TIME_POS, "time-pos", MPV_FORMAT_DOUBLE), "observe");
    checkError(mpv_observe_property(mpv, DURATION, "duration", MPV_FORMAT_DOUBLE), "observe");
    checkError(mpv_observe_property(mpv, HWDEC, "hwdec-current", MPV_FORMAT_STRING), "observe");
    checkError(mpv_observe_property(mpv, PAUSED_FOR_CACHE, "paused-for-cache", MPV_FORMAT_FLAG), "observe");
    checkError(mpv_observe_property(mpv, CACHE_DURATION, "demuxer-cache-duration", MPV_FORMAT_DOUBLE), "observe");

    int advancedControl = 1;
    mpv_opengl_init_params glInit{getProcAddress, nullptr};
    mpv_render_param params[]{{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
                              {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                              {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl},
                              {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&renderCtx, mpv, params) < 0) {
        mpv_terminate_destroy(mpv);
        brls::fatal("mpv: could not create the OpenGL render context");
    }

    char* ua = mpv_get_property_string(mpv, "user-agent");
    if (ua) {
        defaultUserAgent = ua;
        mpv_free(ua);
    }
    char* version = mpv_get_property_string(mpv, "mpv-version");
    brls::Logger::info("mpv: {}", version ? version : "?");
    mpv_free(version);

    mpv_set_wakeup_callback(mpv, onWakeup, this);
    mpv_render_context_set_update_callback(renderCtx, onUpdate, this);
    brls::Application::getExitDoneEvent()->subscribe([this] { clean(); });

    initVideo();
}

void MpvCore::initVideo() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &defaultFramebuffer);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (int)brls::Application::windowWidth, (int)brls::Application::windowHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        brls::Logger::error("mpv: video framebuffer is incomplete");
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
    fbo.fbo = (int)framebuffer;

    GLuint vs = compileShader(GL_VERTEX_SHADER, VERTEX_SHADER);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
    program   = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    unsigned int indices[] = {0, 1, 3, 1, 2, 3};
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    GLuint aPos = glGetAttribLocation(program, "aPos");
    glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(aPos);
    GLuint aTexCoord = glGetAttribLocation(program, "aTexCoord");
    glVertexAttribPointer(aTexCoord, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(aTexCoord);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void MpvCore::clean() {
    if (renderCtx) {
        mpv_render_context_free(renderCtx);
        renderCtx = nullptr;
    }
    if (mpv) {
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
    }
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (texture) glDeleteTextures(1, &texture);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
    framebuffer = texture = vbo = ebo = vao = program = 0;
    brls::Application::getPlatform()->disableScreenDimming(false, "", "");
}

void MpvCore::loadStream(const StreamSource& source) {
    error.clear();
    position = duration = 0;
    fileLoaded          = false;
    readySubtitles.clear();
    int generation = ++streamGeneration;
    if (!source.subtitles.empty()) {
        runInBackground([generation, subtitles = source.subtitles]() mutable {
            for (auto& sub : subtitles) {
                HttpResponse response = HttpClient::get(sub.url);
                if (response.ok() && !response.body.empty())
                    sub.url = "memory://" + TextEncoding::toUtf8(response.body);
                else
                    brls::Logger::warning("subtitle {} failed, letting mpv try it", sub.url);
            }
            brls::sync([generation, subtitles] {
                MpvCore& core = MpvCore::instance();
                if (generation != core.streamGeneration) return;
                core.readySubtitles = subtitles;
                if (core.fileLoaded) core.addSubtitles();
            });
        });
    }

    // These properties persist across files: always overwrite them
    mpv_set_property_string(mpv, "referrer", source.referer.c_str());
    mpv_set_property_string(mpv, "user-agent",
                            source.userAgent.empty() ? defaultUserAgent.c_str() : source.userAgent.c_str());

    // A node list avoids escaping commas inside header values
    std::vector<std::string> fields;
    for (const auto& [key, value] : source.headers) fields.push_back(key + ": " + value);
    std::vector<mpv_node> items(fields.size());
    for (size_t i = 0; i < fields.size(); i++) {
        items[i].format   = MPV_FORMAT_STRING;
        items[i].u.string = const_cast<char*>(fields[i].c_str());
    }
    mpv_node_list list{(int)items.size(), items.data(), nullptr};
    mpv_node node;
    node.format = MPV_FORMAT_NODE_ARRAY;
    node.u.list = &list;
    checkError(mpv_set_property(mpv, "http-header-fields", MPV_FORMAT_NODE, &node), "set http-header-fields");

    brls::Logger::info("mpv: loading {}", source.url);
    command({"loadfile", source.url, "replace"});
}

void MpvCore::stop() {
    command({"stop"});
    streamGeneration++;
    readySubtitles.clear();
    // Clear the last frame so the next video does not start with it
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
}

void MpvCore::addSubtitles() {
    // Selects the first Portuguese subtitle (or the first one when there is none)
    size_t selected = 0;
    for (size_t i = 0; i < readySubtitles.size(); i++) {
        const std::string& lang = readySubtitles[i].language;
        if (lang.rfind("pt", 0) == 0 || lang.rfind("por", 0) == 0) {
            selected = i;
            break;
        }
    }
    for (size_t i = 0; i < readySubtitles.size(); i++) {
        const auto& sub = readySubtitles[i];
        command({"sub-add", sub.url, i == selected ? "select" : "auto", sub.language, sub.language});
    }
    readySubtitles.clear();
}

void MpvCore::togglePause() { command({"cycle", "pause"}); }

void MpvCore::seekRelative(double seconds) { command({"seek", std::to_string(seconds), "relative"}); }

std::vector<MpvTrack> MpvCore::getTracks(const std::string& type) {
    std::vector<MpvTrack> tracks;
    mpv_node node;
    if (mpv_get_property(mpv, "track-list", MPV_FORMAT_NODE, &node) < 0) return tracks;
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; i++) {
            const mpv_node& item = node.u.list->values[i];
            if (item.format != MPV_FORMAT_NODE_MAP) continue;
            MpvTrack track;
            std::string trackType, title, lang;
            for (int j = 0; j < item.u.list->num; j++) {
                std::string key      = item.u.list->keys[j];
                const mpv_node& v    = item.u.list->values[j];
                if (key == "type" && v.format == MPV_FORMAT_STRING) trackType = v.u.string;
                else if (key == "id" && v.format == MPV_FORMAT_INT64) track.id = v.u.int64;
                else if (key == "title" && v.format == MPV_FORMAT_STRING) title = v.u.string;
                else if (key == "lang" && v.format == MPV_FORMAT_STRING) lang = v.u.string;
                else if (key == "selected" && v.format == MPV_FORMAT_FLAG) track.selected = v.u.flag;
            }
            if (trackType != type) continue;
            if (!title.empty() && !lang.empty()) track.label = title + " (" + lang + ")";
            else if (!title.empty()) track.label = title;
            else if (!lang.empty()) track.label = lang;
            else track.label = "#" + std::to_string(track.id);
            tracks.push_back(track);
        }
    }
    mpv_free_node_contents(&node);
    return tracks;
}

void MpvCore::selectTrack(const std::string& property, int64_t id) {
    command({"set", property, id == 0 ? "no" : std::to_string(id)});
}

void MpvCore::command(std::vector<std::string> args) {
    std::vector<const char*> argv;
    for (auto& arg : args) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    checkError(mpv_command_async(mpv, 0, argv.data()), args[0].c_str());
}

void MpvCore::setFrameSize(brls::Rect r) {
    rect = r;
    if (std::isnan(rect.getWidth()) || std::isnan(rect.getHeight())) return;
    int drawWidth  = (int)(rect.getWidth() * brls::Application::windowScale);
    int drawHeight = (int)(rect.getHeight() * brls::Application::windowScale);
    if (drawWidth == 0 || drawHeight == 0) return;

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, drawWidth, drawHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    fbo.w = drawWidth;
    fbo.h = drawHeight;

    // rect (UI coordinates) -> normalized device coordinates
    float minX = rect.getMinX() / brls::Application::contentWidth * 2 - 1;
    float minY = 1 - rect.getMinY() / brls::Application::contentHeight * 2;
    float maxX = rect.getMaxX() / brls::Application::contentWidth * 2 - 1;
    float maxY = 1 - rect.getMaxY() / brls::Application::contentHeight * 2;
    vertices[0] = maxX, vertices[1] = minY;
    vertices[5] = maxX, vertices[6] = maxY;
    vertices[10] = minX, vertices[11] = maxY;
    vertices[15] = minX, vertices[16] = minY;
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Resizing clears the texture: render again so a paused video does not turn black
    mpv_render_context_render(renderCtx, renderParams);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
    glViewport(0, 0, (GLsizei)brls::Application::windowWidth, (GLsizei)brls::Application::windowHeight);
    mpv_render_context_report_swap(renderCtx);
}

void MpvCore::draw(brls::Rect area, float alpha) {
    if (!renderCtx) return;
    if (!(rect == area)) setFrameSize(area);

    glUseProgram(program);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(vao);
    if (alpha < 1) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    glUniform1f(glGetUniformLocation(program, "Alpha"), alpha);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// Called from an mpv thread: hop to the main thread, which owns the GL context
void MpvCore::onUpdate(void*) {
    brls::sync([] {
        MpvCore& core = MpvCore::instance();
        if (!core.renderCtx) return;
        if (!(mpv_render_context_update(core.renderCtx) & MPV_RENDER_UPDATE_FRAME)) return;
        mpv_render_context_render(core.renderCtx, core.renderParams);
        glBindFramebuffer(GL_FRAMEBUFFER, core.defaultFramebuffer);
        glViewport(0, 0, (GLsizei)brls::Application::windowWidth, (GLsizei)brls::Application::windowHeight);
        mpv_render_context_report_swap(core.renderCtx);
    });
}

void MpvCore::onWakeup(void*) {
    brls::sync([] { MpvCore::instance().handleEvents(); });
}

void MpvCore::handleEvents() {
    while (mpv) {
        mpv_event* event = mpv_wait_event(mpv, 0);
        switch (event->event_id) {
            case MPV_EVENT_NONE:
                return;
            case MPV_EVENT_LOG_MESSAGE: {
                auto* log = (mpv_event_log_message*)event->data;
                if (log->log_level <= MPV_LOG_LEVEL_WARN) brls::Logger::warning("mpv [{}] {}", log->prefix, log->text);
                else brls::Logger::info("mpv [{}] {}", log->prefix, log->text);
                break;
            }
            case MPV_EVENT_START_FILE:
                loading = true;
                break;
            case MPV_EVENT_FILE_LOADED:
                fileLoaded = true;
                addSubtitles();
                break;
            case MPV_EVENT_PLAYBACK_RESTART:
                loading = false;
                break;
            case MPV_EVENT_END_FILE: {
                loading   = false;
                auto* end = (mpv_event_end_file*)event->data;
                if (end->reason == MPV_END_FILE_REASON_ERROR) {
                    error = mpv_error_string(end->error);
                    brls::Logger::error("mpv: playback error: {}", error);
                }
                break;
            }
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* prop = (mpv_event_property*)event->data;
                if (!prop->data) break;
                switch (event->reply_userdata) {
                    case CORE_IDLE:
                        idle = *(int*)prop->data;
                        // Keep the screen on while a video is actually playing
                        brls::Application::getPlatform()->disableScreenDimming(!idle, "Playing video", "nx-media-hub");
                        break;
                    case PAUSE:
                        paused = *(int*)prop->data;
                        break;
                    case TIME_POS:
                        position = *(double*)prop->data;
                        break;
                    case DURATION:
                        duration = *(double*)prop->data;
                        break;
                    case HWDEC:
                        hwdec = *(char**)prop->data;
                        brls::Logger::info("mpv: hwdec-current = {}", hwdec);
                        break;
                    case PAUSED_FOR_CACHE:
                        buffering = *(int*)prop->data;
                        if (buffering) brls::Logger::warning("mpv: buffering (cache {:.1f}s)", cached);
                        break;
                    case CACHE_DURATION:
                        cached = *(double*)prop->data;
                        break;
                }
                break;
            }
            default:
                break;
        }
    }
}
