#include "pdf_pages.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

#include <thread>

namespace pdf_pages {
bool available() { return true; }

void render(const std::filesystem::path& file, const std::vector<int>& indices, uint32_t width,
            const std::function<bool(int, Page)>& deliver) {
    // A thread of its own in the multithreaded apartment: whatever apartment the caller's worker is
    // in, waiting on the asynchronous calls is allowed there. The document is read once for them all.
    std::thread([&] {
        std::vector<int> left = indices;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
            using namespace winrt::Windows;
            Data::Pdf::PdfDocument document{nullptr};
            {
                const auto storage = Storage::StorageFile::GetFileFromPathAsync(std::filesystem::absolute(file).wstring()).get();
                document = Data::Pdf::PdfDocument::LoadFromFileAsync(storage).get();
            }
            const int count = static_cast<int>(document.PageCount());
            while (!left.empty()) {
                const int index = left.front();
                left.erase(left.begin());
                Page result;
                result.count = count;
                try {
                    if (index >= 0 && index < count) {
                        const auto page = document.GetPage(static_cast<uint32_t>(index));
                        Storage::Streams::InMemoryRandomAccessStream stream;
                        Data::Pdf::PdfPageRenderOptions options;
                        options.DestinationWidth(width);
                        page.RenderToStreamAsync(stream, options).get();
                        if (const auto size = static_cast<uint32_t>(stream.Size())) {
                            Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
                            reader.LoadAsync(size).get();
                            result.png.resize(size);
                            reader.ReadBytes(result.png);
                        }
                    }
                } catch (const winrt::hresult_error& error) {
                    result.png.clear();
                    result.error = std::wstring(error.message());
                } catch (...) {
                    result.png.clear();
                    result.error = L"page not drawn";
                }
                if (!deliver(index, std::move(result))) return;
            }
        } catch (const winrt::hresult_error& error) {
            // The document itself: every page left is unreadable.
            for (const int index : left) { Page none; none.error = std::wstring(error.message()); if (!deliver(index, std::move(none))) return; }
        } catch (...) {
            for (const int index : left) { Page none; none.error = L"document not read"; if (!deliver(index, std::move(none))) return; }
        }
    }).join();
}

Page render(const std::filesystem::path& file, int index, uint32_t width) {
    Page result;
    render(file, {index}, width, [&](int, Page page) { result = std::move(page); return true; });
    return result;
}
}
