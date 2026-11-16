#include "toast_queue.hpp"

#include <algorithm>
#include <filesystem>

namespace spectra::ui
{

float ToastQueue::lifetime_for(ToastKind kind)
{
    return kind == ToastKind::Error ? 0.0f : DEFAULT_LIFETIME;
}

uint64_t ToastQueue::push(ToastKind kind, std::string title, std::string detail)
{
    Toast t;
    t.id       = next_id_++;
    t.kind     = kind;
    t.title    = std::move(title);
    t.detail   = std::move(detail);
    t.lifetime = lifetime_for(kind);
    toasts_.push_back(std::move(t));

    while (toasts_.size() > MAX_VISIBLE)
    {
        auto victim = std::find_if(toasts_.begin(),
                                   toasts_.end(),
                                   [](const Toast& x) { return x.lifetime > 0.0f; });
        if (victim == toasts_.end())
            victim = toasts_.begin();
        toasts_.erase(victim);
    }
    return toasts_.back().id;
}

bool ToastQueue::set_action(uint64_t id, std::string label, std::function<void()> action)
{
    for (auto& t : toasts_)
    {
        if (t.id == id)
        {
            t.action_label = std::move(label);
            t.action       = std::move(action);
            return true;
        }
    }
    return false;
}

void ToastQueue::watch_file_write(std::string path,
                                  std::string success_title,
                                  std::string error_title)
{
    namespace fs = std::filesystem;
    Watch           w;
    std::error_code ec;
    w.existed       = fs::exists(path, ec);
    w.requested     = w.existed ? fs::last_write_time(path, ec) : fs::file_time_type::min();
    w.path          = std::move(path);
    w.success_title = std::move(success_title);
    w.error_title   = std::move(error_title);
    watches_.push_back(std::move(w));
}

void ToastQueue::poll_watches(float dt)
{
    namespace fs = std::filesystem;
    for (auto it = watches_.begin(); it != watches_.end();)
    {
        it->age += dt;
        std::error_code ec;
        bool            written = false;
        if (fs::exists(it->path, ec))
        {
            const auto t = fs::last_write_time(it->path, ec);
            written = !ec && (!it->existed || t != it->requested) && fs::file_size(it->path, ec) > 0
                      && !ec;
        }
        if (written)
        {
            success(it->success_title, folder_detail(it->path));
            it = watches_.erase(it);
        }
        else if (it->age >= WRITE_TIMEOUT)
        {
            error(it->error_title, it->path + " was not written (see the log)");
            it = watches_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ToastQueue::update(float dt)
{
    poll_watches(dt);
    for (auto& t : toasts_)
        t.age += dt;
    toasts_.erase(
        std::remove_if(toasts_.begin(),
                       toasts_.end(),
                       [](const Toast& t) { return t.lifetime > 0.0f && t.age >= t.lifetime; }),
        toasts_.end());
}

bool ToastQueue::dismiss(uint64_t id)
{
    auto it =
        std::find_if(toasts_.begin(), toasts_.end(), [id](const Toast& t) { return t.id == id; });
    if (it == toasts_.end())
        return false;
    toasts_.erase(it);
    return true;
}

bool ToastQueue::activate(uint64_t id)
{
    auto it =
        std::find_if(toasts_.begin(), toasts_.end(), [id](const Toast& t) { return t.id == id; });
    if (it == toasts_.end())
        return false;
    auto action = std::move(it->action);
    toasts_.erase(it);
    if (action)
        action();
    return true;
}

bool ToastQueue::needs_frames() const
{
    if (!watches_.empty())
        return true;
    return std::any_of(toasts_.begin(),
                       toasts_.end(),
                       [](const Toast& t) { return t.lifetime > 0.0f || t.age < FADE_IN; });
}

std::string ToastQueue::folder_detail(const std::string& path)
{
    const std::filesystem::path p(path);
    std::string                 folder = p.parent_path().string();
    if (folder.empty())
        return path;
    // Keep the tail, which names the folder; the card is narrow.
    constexpr size_t MAX_CHARS = 32;
    if (folder.size() > MAX_CHARS)
        folder = "..." + folder.substr(folder.size() - (MAX_CHARS - 3));
    return "in " + folder;
}

}   // namespace spectra::ui
