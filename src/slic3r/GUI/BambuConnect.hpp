#ifndef slic3r_BambuConnect_hpp_
#define slic3r_BambuConnect_hpp_

#include <string>

namespace Slic3r { namespace GUI {

class Plater;

/** Printing through Bambu Connect, Bambu Lab's supported path for third-party
 *  slicers on printers with Authorization Control firmware (no Developer Mode
 *  needed). See https://wiki.bambulab.com/en/software/third-party-integration */
namespace BambuConnect {

// Official page with the current installers (direct file links change per version).
constexpr const char* kDownloadUrl = "https://wiki.bambulab.com/en/software/bambu-connect#download";

/** encodeURIComponent: keep A-Z a-z 0-9 - _ . ! ~ * ' ( ), percent-encode the rest (UTF-8). */
std::string encode_uri_component(const std::string& s);

/** bambu-connect://import-file?path=...&name=...&version=1.0.0 */
std::string import_url(const std::string& absolute_path, const std::string& display_name);

/** Should prints go through Bambu Connect? True for Bambu printers unless the
 *  user chose "Print plate" (LAN / Developer Mode) in the print button menu. */
bool preferred();

/** Is the Bambu Connect app present (/Applications or ~/Applications)? */
bool installed();

/** Export the current plate's sliced file and open it in Bambu Connect.
 *  Shows a message and returns false when the plate is not sliced, the export
 *  fails, or Bambu Connect is missing (with a link to download it). */
bool send_current_plate(Plater* plater);

/** Open Bambu Connect (for printer controls), or its download page if missing. */
void open_app();

} // namespace BambuConnect

}} // namespace

#endif
