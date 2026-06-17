#ifndef slic3r_MakerWorldTypes_hpp_
#define slic3r_MakerWorldTypes_hpp_

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct MakerWorldSearchContext
{
    std::string locale;
    std::string country_code;
    std::string printer_model;
    bool        user_logged_in{false};
    bool        network_agent_ok{false};
    bool        plugin_search_available{false};
    bool        plugin_download_available{false};
};

struct MakerWorldCandidate
{
    std::string design_id;
    std::string model_id;    // alphanumeric MakerWorld model id (e.g. US6dca49ba027a40)
    std::string profile_id;  // print profile / instance profile id for iot download API
    std::string title;
    std::string author;
    std::string cover_url;
    std::string license;
    std::string download_url;
    std::string filename;
    int         download_count{0};
    bool        login_required{false};
};

struct MakerWorldSearchResult
{
    bool                              ok{false};
    std::string                       error;
    std::vector<MakerWorldCandidate>  candidates;
    int                               latency_ms{0};
    std::string                       source; // "plugin", "search_service", "http_search", "staffpick_filter", "merged"
};

struct MakerWorldImportPayload
{
    bool        ok{false};
    std::string error;
    std::string download_info; // url&name= for Plater::request_model_download
    std::string detail_page_url;
};

}} // namespace

#endif
