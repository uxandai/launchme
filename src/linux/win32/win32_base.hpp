#pragma once
// Linux only. The Windows-shaped surface the launcher's window, input and drawing code is written
// against (src/app, src/ui, src/main.cpp): the base types, COM's IUnknown and GUIDs. The
// implementations behind it are real ports: Direct2D and DirectWrite on Impeller, WIC on
// image_codec and the window manager on SDL3 (src/linux/gfx, src/linux/window). Only the parts
// the launcher uses exist; nothing here pretends to be Wine. docs/linux-port.md, phase 3.
#ifdef _WIN32
#error "src/linux/win32 is the Linux build's Windows surface; the Windows build uses the SDK."
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <sys/types.h>
#include <type_traits>

// Calling conventions and annotations: nothing on x86-64 Linux.
#define WINAPI
#define CALLBACK
#define STDMETHODCALLTYPE
#define APIENTRY
#define __stdcall
#define __cdecl
#define IFACEMETHODIMP HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type) type STDMETHODCALLTYPE
#define IFACEMETHODIMP_(type) type STDMETHODCALLTYPE
#define STDMETHOD(name) virtual HRESULT STDMETHODCALLTYPE name
#define STDMETHOD_(type, name) virtual type STDMETHODCALLTYPE name
#define PURE = 0
#define DECLSPEC_UUID(x)
#define DECLSPEC_NOVTABLE
#define MIDL_INTERFACE(x) struct
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Outptr_
#define _COM_Outptr_
#define _In_reads_(x)
#define _Out_writes_(x)

using BYTE = uint8_t;
using BOOLEAN = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using UINT = unsigned int;
using UINT8 = uint8_t;
using UINT16 = uint16_t;
using UINT32 = uint32_t;
using UINT64 = uint64_t;
using INT = int;
using INT8 = int8_t;
using INT16 = int16_t;
using INT32 = int32_t;
using INT64 = int64_t;
// long, as the SDK declares it (code writes 1L for a LONG); 64 bits here, which nothing relies on.
using LONG = long;
using ULONG = unsigned long;
using SHORT = int16_t;
using USHORT = uint16_t;
using LONGLONG = int64_t;
using ULONGLONG = unsigned long long; // crt_compat.hpp declares the same type.
using DWORD64 = uint64_t;
using BOOL = int;
using FLOAT = float;
using CHAR = char;
using WCHAR = wchar_t;
using INT_PTR = std::intptr_t;   // platform/launch.hpp declares the same type.
using UINT_PTR = std::uintptr_t;
using LONG_PTR = std::intptr_t;
using ULONG_PTR = std::uintptr_t;
using DWORD_PTR = std::uintptr_t;
using SIZE_T = size_t;
using SSIZE_T = ssize_t;
using WPARAM = UINT_PTR;
using LPARAM = LONG_PTR;
using LRESULT = LONG_PTR;
using HRESULT = int32_t;
using ATOM = WORD;
using LPVOID = void*;
using LPCVOID = const void*;
using PVOID = void*;
using LPWSTR = wchar_t*;
using PWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;
using PCWSTR = const wchar_t*;
using LPSTR = char*;
using LPCSTR = const char*;
using LPBYTE = BYTE*;
using LPDWORD = DWORD*;
using LPLONG = LONG*;
using LCID = DWORD;
using LANGID = WORD;
using COLORREF = DWORD;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define INFINITE 0xFFFFFFFFu

// Window and handle types: distinct opaque pointers as STRICT Windows headers declare them.
// HWND matches platform/launch.hpp's `void*` (the shared launch code passes it through).
using HWND = void*;
struct HINSTANCE__; using HINSTANCE = HINSTANCE__*; using HMODULE = HINSTANCE;
struct HICON__; using HICON = HICON__*;
struct HCURSOR__; using HCURSOR = HCURSOR__*;
struct HMENU__; using HMENU = HMENU__*;
struct HMONITOR__; using HMONITOR = HMONITOR__*;
struct HDC__; using HDC = HDC__*;
struct HBITMAP__; using HBITMAP = HBITMAP__*;
struct HBRUSH__; using HBRUSH = HBRUSH__*;
struct HFONT__; using HFONT = HFONT__*;
struct HGDIOBJ__; using HGDIOBJ = HGDIOBJ__*;
struct HKEY__; using HKEY = HKEY__*;
struct HDROP__; using HDROP = HDROP__*;
struct HGLOBAL__; using HGLOBAL = HGLOBAL__*;
struct HRSRC__; using HRSRC = HRSRC__*;
struct HRAWINPUT__; using HRAWINPUT = HRAWINPUT__*;
struct HPOWERNOTIFY__; using HPOWERNOTIFY = HPOWERNOTIFY__*;
struct HWINEVENTHOOK__; using HWINEVENTHOOK = HWINEVENTHOOK__*;
using HLOCAL = HGLOBAL;

// HRESULT
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_NOTIMPL ((HRESULT)0x80004001)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_POINTER ((HRESULT)0x80004003)
#define E_ABORT ((HRESULT)0x80004004)
#define E_FAIL ((HRESULT)0x80004005)
#define E_UNEXPECTED ((HRESULT)0x8000FFFF)
#define E_ACCESSDENIED ((HRESULT)0x80070005)
#define E_HANDLE ((HRESULT)0x80070006)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT)(((x) & 0x0000FFFF) | 0x80070000)))

#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define MAKELONG(a, b) ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb) (LOBYTE(rgb))
#define GetGValue(rgb) (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) (LOBYTE((rgb) >> 16))
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define ZeroMemory(p, n) std::memset((p), 0, (n))
#define CopyMemory(d, s, n) std::memcpy((d), (s), (n))

struct POINT { LONG x, y; };
using LPPOINT = POINT*;
struct POINTS { SHORT x, y; };
struct SIZE { LONG cx, cy; };
struct RECT { LONG left, top, right, bottom; };
using LPRECT = RECT*;
using LPCRECT = const RECT*;
struct FILETIME { DWORD dwLowDateTime, dwHighDateTime; };
struct SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; };
union LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; };
union ULARGE_INTEGER { struct { DWORD LowPart; DWORD HighPart; }; ULONGLONG QuadPart; };

// GUIDs and COM's base interface. __uuidof(T) names an interface type; each interface declares
// its identifier as `static constexpr GUID lm_iid`.
struct GUID {
    uint32_t Data1; uint16_t Data2; uint16_t Data3; uint8_t Data4[8];
    friend constexpr bool operator==(const GUID& a, const GUID& b) {
        if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3) return false;
        for (int i = 0; i < 8; ++i) if (a.Data4[i] != b.Data4[i]) return false;
        return true;
    }
};
using IID = GUID;
using CLSID = GUID;
using REFIID = const IID&;
using REFGUID = const GUID&;
using REFCLSID = const CLSID&;
inline bool IsEqualGUID(REFGUID a, REFGUID b) { return a == b; }
inline bool IsEqualIID(REFIID a, REFIID b) { return a == b; }
#define __uuidof(T) (T::lm_iid)
#define LM_IID(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
    static constexpr GUID lm_iid{a, b, c, {d0, d1, d2, d3, d4, d5, d6, d7}}

struct IUnknown {
    LM_IID(0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
    template <class Q> HRESULT QueryInterface(Q** object) { return QueryInterface(__uuidof(Q), reinterpret_cast<void**>(object)); }
protected:
    virtual ~IUnknown() = default;
};

namespace lm_com {
template <class T> constexpr const GUID& iidOf(T**) { return T::lm_iid; }
template <class T> void** ppv(T** pointer) { return reinterpret_cast<void**>(pointer); }

// A reference-counted object implementing interfaces I...: QueryInterface answers each of them,
// their bases (each interface's `Base` alias) and IUnknown.
template <class... I>
class Object : public I... {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (riid == IUnknown::lm_iid) { *object = static_cast<IUnknown*>(static_cast<First*>(this)); AddRef(); return S_OK; }
        if ((find<I>(riid, object) || ...)) { AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (!left) delete this;
        return left;
    }
protected:
    virtual ~Object() = default;
private:
    template <class F, class...> struct Head { using type = F; };
    using First = typename Head<I...>::type;
    template <class Interface>
    bool find(REFIID riid, void** object) {
        return match<Interface, Interface>(riid, object);
    }
    template <class Interface, class Current>
    bool match(REFIID riid, void** object) {
        if (riid == Current::lm_iid) { *object = static_cast<Current*>(static_cast<Interface*>(this)); return true; }
        if constexpr (requires { typename Current::Base; }) {
            if constexpr (!std::is_same_v<typename Current::Base, IUnknown>) return match<Interface, typename Current::Base>(riid, object);
        }
        return false;
    }
    std::atomic<ULONG> references_{1};
};
}
#define IID_PPV_ARGS(pp) ::lm_com::iidOf(pp), ::lm_com::ppv(pp)

