#ifndef slic3r_MakerWorldPrintOfferDialog_hpp_
#define slic3r_MakerWorldPrintOfferDialog_hpp_

#include "MakerWorldTypes.hpp"

#include "../GUI_Utils.hpp" // DPIDialog

#include <wx/timer.h>

#include <atomic>
#include <memory>
#include <vector>

class Button;
class wxPanel;
class wxScrolledWindow;
class wxStaticBitmap;
class wxStaticText;
class wxWindow;

namespace Slic3r { namespace GUI {

/**
 * Top MakerWorld hits as selectable cards (thumbnail + title + author) with a
 * countdown that auto-confirms the CURRENT selection (default: candidate #1).
 * Keyboard: 1..3 select, Enter confirms, Esc cancels.
 */
class MakerWorldPrintOfferDialog : public DPIDialog
{
public:
    static constexpr int kDefaultCountdownSec = 10;

    MakerWorldPrintOfferDialog(wxWindow* parent, const std::vector<MakerWorldCandidate>& candidates);
    ~MakerWorldPrintOfferDialog() override;

    bool has_selection() const { return m_selected_index >= 0; }
    int  selected_index() const { return m_selected_index; }
    MakerWorldCandidate selected_candidate() const;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    struct CandidateCard
    {
        wxPanel*        frame{nullptr}; // 1px border panel; colored by selection
        wxStaticBitmap* thumb{nullptr};
    };

    wxPanel* build_candidate_card(wxWindow* parent, const MakerWorldCandidate& c, int index);
    void select(int index);
    void confirm();
    void on_timer(wxTimerEvent& evt);
    void on_char_hook(wxKeyEvent& evt);
    void update_confirm_label();
    void start_thumbnail_fetch(int index, const std::string& url);

    std::vector<MakerWorldCandidate>   m_candidates;
    std::vector<CandidateCard>         m_cards;
    int                                m_selected_index{-1};
    Button*                            m_confirm_btn{nullptr};
    wxScrolledWindow*                  m_scroll{nullptr};
    wxTimer                            m_timer;
    int                                m_seconds_left{kDefaultCountdownSec};
    std::shared_ptr<std::atomic<bool>> m_alive; // guards async thumbnail callbacks
};

}} // namespace

#endif
