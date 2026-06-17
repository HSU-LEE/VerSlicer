#ifndef slic3r_OllamaIntentRules_hpp_
#define slic3r_OllamaIntentRules_hpp_

#include <optional>
#include <string>

namespace Slic3r { namespace GUI {

/** Shared user-intent heuristics for normalizer, validator, chat panel, and pipeline recovery. */
namespace OllamaIntentRules {

bool contains_support_intent(const std::string& s);
bool contains_midair_or_failure_intent(const std::string& s);
bool contains_adhesion_intent(const std::string& s);
bool contains_strength_intent(const std::string& s);
bool contains_explicit_infill_intent(const std::string& s);
bool contains_flip_intent(const std::string& s);
bool contains_rotate_intent(const std::string& s);
bool contains_placement_intent(const std::string& s);
bool contains_disable_brim_intent(const std::string& s);
bool contains_brim_intent(const std::string& s);
bool contains_durability_intent(const std::string& s);
bool contains_file_intent(const std::string& s);

bool user_wants_delete(const std::string& user);
bool user_wants_plate_arrange(const std::string& user);
bool describes_print_quality_symptom(const std::string& s);

std::optional<double> parse_z_rotation_degrees(const std::string& s);
std::string           extract_first_url_from_text(const std::string& text);

} // namespace OllamaIntentRules

}} // namespace

#endif
