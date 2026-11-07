#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct SteamFriend {
    uint64_t id = 0;
    std::wstring name;
    std::wstring game;
    std::wstring avatar_url;
    int status = 0; // 0=offline, >0=online; game nonempty=in-game.
};

struct FriendsResult {
    std::vector<SteamFriend> friends;
    std::wstring error;
    bool no_presence = false; // Aurelia listed friends, but Steam has sent neither names nor statuses yet.
};

FriendsResult fetchSteamFriends(uint64_t steam_id);
// A friend's avatar, cached in `cache` by SteamID. Without a URL (Aurelia's roster has none): the Steam
// client's own avatar cache (`steam_root`\config\avatarcache), else the public profile's avatarMedium.
std::vector<uint8_t> fetchFriendAvatar(const SteamFriend& friend_info,
                                        const std::filesystem::path& cache, const std::filesystem::path& steam_root = {});
// <avatarMedium> of a steamcommunity.com profile's ?xml=1 page; empty unless an https Steam CDN address.
std::wstring profileAvatarUrl(std::string_view xml);
// The presence a public profile's ?xml=1 page shows: <onlineState> (online, in-game, offline), the game
// of <inGameInfo> and the avatar. `known` is false without an onlineState (no page, an error page).
struct ProfilePresence {
    bool known = false;
    int status = 0; // As SteamFriend::status: 0 offline, 1 online.
    std::wstring game, avatar_url;
};
ProfilePresence profilePresence(std::string_view xml);
// In game first, then online, then offline; by name within each.
void sortFriends(std::vector<SteamFriend>& friends);
