#pragma once

#include <borealis.hpp>

// brls::Image loaded from an http(s) URL. Downloads are shared by URL and run on a small worker pool
// (newest requests first); finished images always land in brls::TextureCache, even if the view that
// asked for them is gone, so a recreated tab shows them immediately.
class RemoteImage : public brls::Image {
public:
    RemoteImage();
    ~RemoteImage() override;

    void setImageFromUrl(const std::string& url);

    // Called by the loader on the main thread once `url` is in the texture cache
    void applyCached(const std::string& url);

    static brls::View* create();

private:
    std::string url;
};
