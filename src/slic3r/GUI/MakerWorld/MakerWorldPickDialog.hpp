#ifndef slic3r_MakerWorldPickDialog_hpp_
#define slic3r_MakerWorldPickDialog_hpp_

#include "MakerWorldTypes.hpp"

#include <wx/dialog.h>
#include <vector>

class wxListBox;

namespace Slic3r { namespace GUI {

class MakerWorldPickDialog : public wxDialog
{
public:
    MakerWorldPickDialog(wxWindow* parent, const std::vector<MakerWorldCandidate>& candidates);

    bool has_selection() const { return m_selected_index >= 0; }
    MakerWorldCandidate selected_candidate() const;

private:
    std::vector<MakerWorldCandidate> m_candidates;
    int                              m_selected_index{-1};
    wxListBox*                       m_list{nullptr};
};

}} // namespace

#endif
