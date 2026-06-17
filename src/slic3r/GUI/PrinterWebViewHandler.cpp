#include "PrinterWebViewHandler.hpp"

#include "PrinterWebView.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"

namespace Slic3r {
namespace GUI {

PrinterWebViewHandler::PrinterWebViewHandler(PrinterWebView& owner)
    : m_owner(owner)
{
}

PrinterWebViewHandler::~PrinterWebViewHandler() = default;

void PrinterWebViewHandler::on_loaded(wxWebViewEvent& evt)
{
    evt.Skip();
}

void PrinterWebViewHandler::on_script_message(wxWebViewEvent& evt)
{
    evt.Skip();
}

PrinterWebView& PrinterWebViewHandler::owner() const { return m_owner; }

wxWebView* PrinterWebViewHandler::browser() const { return m_owner.m_browser; }

std::unique_ptr<PrinterWebViewHandler> create_printer_webview_handler(PrinterWebView& /*owner*/)
{
    return nullptr;
}

} // namespace GUI
} // namespace Slic3r
