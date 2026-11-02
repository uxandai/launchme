#include "etw_access.hpp"
#include <windows.h>
#include <sddl.h>
#include <lmaccess.h>
#include <lmapibuf.h>
#include <lmerr.h>
#include <shellapi.h>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "netapi32.lib")

namespace etw_access {
namespace {
constexpr wchar_t log_users_sid[] = L"S-1-5-32-559";

struct LocalFreeDeleter { void operator()(void* p) const { if (p) LocalFree(p); } };
using LocalPtr = std::unique_ptr<void, LocalFreeDeleter>;
struct HandleCloser { void operator()(HANDLE h) const { if (h) CloseHandle(h); } };
using Handle = std::unique_ptr<void, HandleCloser>;

Handle processToken() {
    HANDLE token = nullptr;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    return Handle(token);
}

std::wstring userSid() {
    const auto token = processToken();
    if (!token) return {};
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (!size) return {};
    std::vector<unsigned char> buffer(size);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size)) return {};
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text)) return {};
    LocalPtr owner(text);
    return text;
}

// Whether `sid` is a member of the group by the account database (not by this process's token).
bool groupHasMember(const std::wstring& group_sid, const std::wstring& member_sid) {
    PSID group = nullptr;
    if (!ConvertStringSidToSidW(group_sid.c_str(), &group)) return false;
    LocalPtr group_owner(group);
    wchar_t name[256]{}, domain[256]{};
    DWORD name_size = 256, domain_size = 256;
    SID_NAME_USE use{};
    if (!LookupAccountSidW(nullptr, group, name, &name_size, domain, &domain_size, &use)) return false;
    LOCALGROUP_MEMBERS_INFO_0* members = nullptr;
    DWORD read = 0, total = 0;
    DWORD_PTR resume = 0;
    bool found = false;
    NET_API_STATUS status;
    do {
        status = NetLocalGroupGetMembers(nullptr, name, 0, reinterpret_cast<LPBYTE*>(&members),
                                         MAX_PREFERRED_LENGTH, &read, &total, &resume);
        if (status != NERR_Success && status != ERROR_MORE_DATA) break;
        for (DWORD i = 0; members && i < read && !found; ++i) {
            LPWSTR text = nullptr;
            if (ConvertSidToStringSidW(members[i].lgrmi0_sid, &text)) {
                LocalPtr text_owner(text);
                found = member_sid == text;
            }
        }
        if (members) NetApiBufferFree(members);
        members = nullptr;
    } while (status == ERROR_MORE_DATA && !found);
    return found;
}

bool tokenHas(const wchar_t* sid_text) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(sid_text, &sid)) return false;
    LocalPtr owner(sid);
    BOOL member = FALSE;
    return CheckTokenMembership(nullptr, sid, &member) && member;
}
}

State state() {
    // Elevated administrators may trace; so may any token that already carries the group.
    if (tokenHas(L"S-1-5-32-559") || tokenHas(L"S-1-5-32-544")) {
        // An administrator group that is only a deny-only SID in a filtered token does not count:
        // CheckTokenMembership honours that, so a true result is a usable one.
        return State::allowed;
    }
    const auto sid = userSid();
    if (sid.empty()) return State::unknown;
    return groupHasMember(log_users_sid, sid) ? State::pending_sign_in : State::missing;
}

bool grant() {
    const auto sid = userSid();
    if (sid.empty()) return false;
    // The group's name is localised ("Użytkownicy dzienników wydajności"), so it is resolved from
    // its well-known SID. The user is passed by SID too; both come from the system, not from text.
    const std::wstring command =
        L"-NoProfile -NonInteractive -WindowStyle Hidden -Command \"Add-LocalGroupMember -SID '" +
        std::wstring(log_users_sid) + L"' -Member (New-Object Security.Principal.SecurityIdentifier '" + sid +
        L"') -ErrorAction Stop\"";
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = L"powershell.exe";
    info.lpParameters = command.c_str();
    info.nShow = SW_HIDE;
    // Returns once the prompt is answered; the change itself lands a moment later, and state()
    // shows it the next time the settings page is built.
    if (!ShellExecuteExW(&info)) return false; // Also when the prompt was declined.
    if (info.hProcess) CloseHandle(info.hProcess);
    return true;
}

}
