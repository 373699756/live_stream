#include "media_codec.h"

#include <string>

namespace live_stream {
namespace media_codec {
namespace {

template <typename Predicate>
bool AnyNalUnit(const H265NalUnitList &units, Predicate predicate) {
    for (const H265NalUnit &unit : units) {
        if (predicate(unit)) {
            return true;
        }
    }
    return false;
}

void AssignNalPayload(const uint8_t *data, size_t size, std::string *out) {
    if (out != nullptr) {
        out->assign(reinterpret_cast<const char *>(data), size);
    }
}

void ExtractH265ParameterSetsFromUnits(const H265NalUnitList &units,
                                       std::string *vps,
                                       std::string *sps,
                                       std::string *pps,
                                       bool *has_vps,
                                       bool *has_sps,
                                       bool *has_pps) {
    bool local_has_vps = false;
    bool local_has_sps = false;
    bool local_has_pps = false;
    for (const H265NalUnit &unit : units) {
        if (unit.type == kH265NalTypeVps) {
            AssignNalPayload(unit.data, unit.size, vps);
            local_has_vps = true;
        } else if (unit.type == kH265NalTypeSps) {
            AssignNalPayload(unit.data, unit.size, sps);
            local_has_sps = true;
        } else if (unit.type == kH265NalTypePps) {
            AssignNalPayload(unit.data, unit.size, pps);
            local_has_pps = true;
        }
    }
    if (has_vps != nullptr) {
        *has_vps = local_has_vps;
    }
    if (has_sps != nullptr) {
        *has_sps = local_has_sps;
    }
    if (has_pps != nullptr) {
        *has_pps = local_has_pps;
    }
}

}  // namespace

bool H265NalUnitList::Add(const H265NalUnit &unit) {
    if (count >= kMaxNalUnitsPerFrame) {
        overflow = true;
        return false;
    }
    units[count++] = unit;
    return true;
}

bool IsH265ParameterSetNal(uint8_t nal_type) {
    return nal_type == kH265NalTypeVps || nal_type == kH265NalTypeSps ||
           nal_type == kH265NalTypePps;
}

bool IsH265IdrNal(uint8_t nal_type) {
    return nal_type == kH265NalTypeIdrWRadl ||
           nal_type == kH265NalTypeIdrNLp ||
           nal_type == kH265NalTypeCra;
}

bool ParseH265AnnexBNalUnits(const uint8_t *data,
                             size_t size,
                             H265NalUnitList *units) {
    if (units == nullptr) {
        return false;
    }
    *units = H265NalUnitList{};
    class H265ListBuilder final : public IAnnexBNalUnitSink {
    public:
        explicit H265ListBuilder(H265NalUnitList *units) : units_(units) {}

        bool OnAnnexBNalUnit(const AnnexBNalUnit &unit, bool last) override {
            (void)last;
            if (unit.size <= 1) {
                return true;
            }
            return units_->Add({unit.data, unit.size, unit.h265_type});
        }

    private:
        H265NalUnitList *units_ = nullptr;
    } builder(units);
    return ForEachAnnexBNalUnit(data, size, &builder) && !units->overflow &&
           !units->empty();
}

bool HasH265ParameterSets(const H265NalUnitList &units) {
    return AnyNalUnit(units, [](const H265NalUnit &unit) {
        return IsH265ParameterSetNal(unit.type);
    });
}

bool HasCompleteH265ParameterSets(const H265NalUnitList &units) {
    bool has_vps = false;
    bool has_sps = false;
    bool has_pps = false;
    for (const H265NalUnit &unit : units) {
        if (unit.type == kH265NalTypeVps) {
            has_vps = true;
        } else if (unit.type == kH265NalTypeSps) {
            has_sps = true;
        } else if (unit.type == kH265NalTypePps) {
            has_pps = true;
        }
        if (has_vps && has_sps && has_pps) {
            return true;
        }
    }
    return false;
}

bool HasH265KeyFrame(const H265NalUnitList &units) {
    return AnyNalUnit(units, [](const H265NalUnit &unit) {
        return IsH265IdrNal(unit.type);
    });
}

H265ParameterSets ExtractH265ParameterSets(const H265NalUnitList &units) {
    H265ParameterSets parameter_sets;
    ExtractH265ParameterSetsFromUnits(units, &parameter_sets.vps,
                                      &parameter_sets.sps, &parameter_sets.pps,
                                      nullptr, nullptr, nullptr);
    return parameter_sets;
}

void ExtractH265ParameterSets(const H265NalUnitList &units,
                              std::string *vps,
                              std::string *sps,
                              std::string *pps,
                              bool *has_vps,
                              bool *has_sps,
                              bool *has_pps) {
    ExtractH265ParameterSetsFromUnits(units, vps, sps, pps, has_vps, has_sps,
                                      has_pps);
}

}  // namespace media_codec
}  // namespace live_stream
