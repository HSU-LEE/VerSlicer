#include "ConfigOptionRead.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

float config_get_float(const DynamicPrintConfig& cfg, const std::string& key, float fallback)
{
    if (!cfg.has(key))
        return fallback;
    const ConfigOption* opt = cfg.option(key);
    if (!opt)
        return fallback;
    if (auto* f = dynamic_cast<const ConfigOptionFloat*>(opt))
        return float(f->value);
    if (auto* fs = dynamic_cast<const ConfigOptionFloats*>(opt)) {
        if (fs->values.empty())
            return fallback;
        return float(fs->get_at(0));
    }
    if (auto* i = dynamic_cast<const ConfigOptionInt*>(opt))
        return float(i->value);
    if (auto* is = dynamic_cast<const ConfigOptionInts*>(opt)) {
        if (is->values.empty())
            return fallback;
        return float(is->get_at(0));
    }
    if (auto* p = dynamic_cast<const ConfigOptionPercent*>(opt))
        return float(p->value);
    if (auto* ps = dynamic_cast<const ConfigOptionPercents*>(opt)) {
        if (ps->values.empty())
            return fallback;
        return float(ps->get_at(0));
    }
    return fallback;
}

int config_get_int(const DynamicPrintConfig& cfg, const std::string& key, int fallback)
{
    if (!cfg.has(key))
        return fallback;
    const ConfigOption* opt = cfg.option(key);
    if (!opt)
        return fallback;
    if (auto* i = dynamic_cast<const ConfigOptionInt*>(opt))
        return int(i->value);
    if (auto* is = dynamic_cast<const ConfigOptionInts*>(opt)) {
        if (is->values.empty())
            return fallback;
        return int(is->get_at(0));
    }
    return fallback;
}

bool config_get_bool(const DynamicPrintConfig& cfg, const std::string& key, bool fallback)
{
    if (!cfg.has(key))
        return fallback;
    const ConfigOption* opt = cfg.option(key);
    if (!opt)
        return fallback;
    if (auto* b = dynamic_cast<const ConfigOptionBool*>(opt))
        return b->value;
    if (auto* bs = dynamic_cast<const ConfigOptionBools*>(opt)) {
        if (bs->values.empty())
            return fallback;
        return bs->get_at(0);
    }
    return fallback;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
