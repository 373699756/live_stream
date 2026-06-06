#include "media_codec.h"

#include <string>

namespace live_stream {
namespace media_codec {
namespace {

template <typename Predicate>
bool AnyNalUnit(const H264NalUnitList &units, Predicate predicate) {
    for (const H264NalUnit &unit : units) {
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

void ExtractH264ParameterSetsFromUnits(const H264NalUnitList &units,
                                       std::string *sps,
                                       std::string *pps,
                                       bool *has_sps,
                                       bool *has_pps) {
    bool local_has_sps = false;
    bool local_has_pps = false;
    for (const H264NalUnit &unit : units) {
        if (unit.type == kH264NalTypeSps) {
            AssignNalPayload(unit.data, unit.size, sps);
            local_has_sps = true;
        } else if (unit.type == kH264NalTypePps) {
            AssignNalPayload(unit.data, unit.size, pps);
            local_has_pps = true;
        }
    }
    if (has_sps != nullptr) {
        *has_sps = local_has_sps;
    }
    if (has_pps != nullptr) {
        *has_pps = local_has_pps;
    }
}

}  // namespace

bool H264NalUnitList::Add(const H264NalUnit &unit) {
    if (count >= kMaxNalUnitsPerFrame) {
        overflow = true;
        return false;
    }
    units[count++] = unit;
    return true;
}

bool IsH264ParameterSetNal(uint8_t nal_type) {
    return nal_type == kH264NalTypeSps || nal_type == kH264NalTypePps;
}

bool IsH264IdrNal(uint8_t nal_type) {
    return nal_type == kH264NalTypeIdr;
}

bool ParseH264AnnexBNalUnits(const uint8_t *data,
                             size_t size,
                             H264NalUnitList *units) {
    if (units == nullptr) {
        return false;
    }
    *units = H264NalUnitList{};
    class H264ListBuilder final : public IAnnexBNalUnitSink {
    public:
        explicit H264ListBuilder(H264NalUnitList *units) : units_(units) {}

        bool OnAnnexBNalUnit(const AnnexBNalUnit &unit, bool last) override {
            (void)last;
            return units_->Add({unit.data, unit.size, unit.h264_type});
        }

    private:
        H264NalUnitList *units_ = nullptr;
    } builder(units);
    return ForEachAnnexBNalUnit(data, size, &builder) && !units->overflow;
}

bool HasH264ParameterSets(const H264NalUnitList &units) {
    return AnyNalUnit(units, [](const H264NalUnit &unit) {
        return IsH264ParameterSetNal(unit.type);
    });
}

bool HasCompleteH264ParameterSets(const H264NalUnitList &units) {
    bool has_sps = false;
    bool has_pps = false;
    for (const H264NalUnit &unit : units) {
        if (unit.type == kH264NalTypeSps) {
            has_sps = true;
        } else if (unit.type == kH264NalTypePps) {
            has_pps = true;
        }
        if (has_sps && has_pps) {
            return true;
        }
    }
    return false;
}

bool HasH264KeyFrame(const H264NalUnitList &units) {
    return AnyNalUnit(units, [](const H264NalUnit &unit) {
        return IsH264IdrNal(unit.type);
    });
}

H264ParameterSets ExtractH264ParameterSets(const H264NalUnitList &units) {
    H264ParameterSets parameter_sets;
    ExtractH264ParameterSetsFromUnits(units, &parameter_sets.sps,
                                      &parameter_sets.pps, nullptr, nullptr);
    return parameter_sets;
}

void ExtractH264ParameterSets(const H264NalUnitList &units,
                              std::string *sps,
                              std::string *pps,
                              bool *has_sps,
                              bool *has_pps) {
    ExtractH264ParameterSetsFromUnits(units, sps, pps, has_sps, has_pps);
}

}  // namespace media_codec
}  // namespace live_stream
