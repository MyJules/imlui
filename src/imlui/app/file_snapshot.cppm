// What the Engine's hot reload compares: the modification times of the watched resources, taken every 400 ms, and the
// rule that says whether two of them differ by a change the script must be reloaded for.
export module imlui.app.file_snapshot;

import std;

import imlui.resources.mount;

export namespace imlui {

/// Watched resource names and their modification times (nullopt: missing, or in an immutable mount).
using FileSnapshot = std::map<std::string, std::optional<ResourceTime>>;

/// Whether the resources changed from snapshot `before` (its scan started at `beforeTakenAt`) to `after`, given the
/// names `loaded` that the Lua state read or loaded (the snapshots hold every `.lua` file of the mounts, plus those):
///  * a name in both whose time differs changed;
///  * a `.lua` file in `before` only was removed: a change. Any other name in `before` only is no longer watched
///    (the script stopped reading it, see Engine::reloadNow), not a change;
///  * a name in `after` only is a new `.lua` file (a change) or a name the state read since `before` was taken. Its
///    time when it was read isn't known, so it counts as changed when its time is at or after `beforeTakenAt`: an edit
///    made since then may have come after the read (a file first read while the script reloads and edited before
///    the next scan, or one a running script read). At worst that reloads once more than needed.
[[nodiscard]] bool resourcesChanged(const FileSnapshot& before, const FileSnapshot& after,
                                    const std::set<std::string>& loaded, ResourceTime beforeTakenAt);

} // namespace imlui

module :private;

namespace imlui {

bool resourcesChanged(const FileSnapshot& before, const FileSnapshot& after, const std::set<std::string>& loaded,
                      ResourceTime beforeTakenAt) {
    for (const auto& [name, time] : after) {
        const auto it = before.find(name);
        if (it != before.end()) {
            if (it->second != time) return true;
        } else if (!loaded.contains(name) || (time && *time >= beforeTakenAt)) {
            return true;
        }
    }
    for (const auto& [name, time] : before)
        if (!after.contains(name) && name.ends_with(".lua")) return true;
    return false;
}

} // namespace imlui
