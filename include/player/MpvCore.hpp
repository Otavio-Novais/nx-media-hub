#pragma once

// libmpv wrapper rendering into an OpenGL framebuffer that VideoView draws as a textured quad.
// Render/event approach adapted from wiliwili's MPVCore (https://github.com/xfangfang/wiliwili, GPL-3.0).

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <glad/glad.h>

#include <borealis/core/geometry.hpp>
#include <string>
#include <vector>

#include "models/StreamSource.hpp"

struct MpvTrack {
    int64_t id = 0;
    std::string label;
    bool selected = false;
};

class MpvCore {
public:
    // Creates mpv on first use: call from the main thread after the window (GL context) exists.
    static MpvCore& instance();

    // startAt: resume position in seconds (0 = from the beginning)
    void loadStream(const StreamSource& source, double startAt = 0);
    void stop();
    void togglePause();
    void seekRelative(double seconds);

    // type: "audio" or "sub"
    std::vector<MpvTrack> getTracks(const std::string& type);
    // property: "aid" or "sid"; id 0 disables the track
    void selectTrack(const std::string& property, int64_t id);

    // Must be called inside a view's draw(): the quad lands below the nanovg UI of the frame.
    void draw(brls::Rect rect, float alpha);

    // Playback state, updated on the main thread
    bool idle         = true;
    bool paused       = false;
    bool loading      = false;
    bool buffering    = false;
    double position   = 0;  // seconds
    double duration   = 0;  // seconds
    double cached     = 0;  // seconds of video buffered ahead
    std::string hwdec;
    std::string error;

    // The stream passed to the last loadStream() is showing video. Unlike !loading, it is not fooled by the
    // previous stream ending (its END_FILE arrives after the new loadStream())
    bool started() const { return fileLoaded && !loading; }

private:
    MpvCore();
    void initVideo();
    void clean();
    void setFrameSize(brls::Rect rect);
    void handleEvents();
    void command(std::vector<std::string> args);

    static void onUpdate(void* self);
    static void onWakeup(void* self);

    mpv_handle* mpv                  = nullptr;
    mpv_render_context* renderCtx    = nullptr;
    std::string defaultUserAgent;
    void addSubtitles();

    // External subtitles are downloaded and converted to UTF-8 by the app (the Switch libmpv has no
    // iconv), then added as memory:// once both they and the file are ready
    int streamGeneration = 0;  // ignores subtitle downloads of a previous stream
    bool fileLoaded      = false;
    std::vector<SubtitleTrack> readySubtitles;  // url = "memory://<utf-8 text>" (or the original URL)

    brls::Rect rect;
    GLint defaultFramebuffer = 0;
    GLuint framebuffer       = 0;
    GLuint texture           = 0;
    GLuint program           = 0;
    GLuint vao = 0, vbo = 0, ebo = 0;
    mpv_opengl_fbo fbo{0, 1920, 1080, 0};
    int flipY = 1;
    mpv_render_param renderParams[3] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    // x, y, z, u, v for each corner of the quad (updated in setFrameSize)
    float vertices[20] = {1, 1, 0, 1, 1, 1, -1, 0, 1, 0, -1, -1, 0, 0, 0, -1, 1, 0, 0, 1};
};
