#include "media_codec.h"

#include <cstdint>
#include <string>

int main() {
    const uint8_t h264[] = {
        0x00,
        0x00,
        0x00,
        0x01,
        0x67,
        0x42,
        0x00,
        0x1f,
        0x00,
        0x00,
        0x01,
        0x68,
        0xce,
        0x06,
        0x00,
        0x00,
        0x01,
        0x65,
        0x88,
        0x84,
    };

    live_stream::media_codec::H264NalUnitList units;
    if (!live_stream::media_codec::ParseH264AnnexBNalUnits(
            h264, sizeof(h264), &units)) {
        return 1;
    }
    if (units.count != 3 ||
        !live_stream::media_codec::HasH264ParameterSets(units) ||
        !live_stream::media_codec::HasCompleteH264ParameterSets(units) ||
        !live_stream::media_codec::HasH264Keyframe(units)) {
        return 2;
    }

    std::string sps;
    std::string pps;
    bool has_sps = false;
    bool has_pps = false;
    live_stream::media_codec::ExtractH264ParameterSets(
        units, &sps, &pps, &has_sps, &has_pps);
    if (!has_sps || !has_pps || sps.empty() || pps.empty()) {
        return 3;
    }

    if (units.units[0].type != 7 || units.units[1].type != 8 ||
        units.units[2].type != 5) {
        return 5;
    }

    const uint8_t h265[] = {
        0x00,
        0x00,
        0x00,
        0x01,
        0x46,
        0x01,
        0x50,
        0x00,
        0x00,
        0x01,
        0x40,
        0x01,
        0x0c,
        0x00,
        0x00,
        0x01,
        0x42,
        0x01,
        0x0c,
        0x00,
        0x00,
        0x01,
        0x44,
        0x01,
        0x0c,
        0x00,
        0x00,
        0x01,
        0x26,
        0x01,
        0x02,
    };

    live_stream::media_codec::H265NalUnitList h265_units;
    if (!live_stream::media_codec::ParseH265AnnexBNalUnits(
            h265, sizeof(h265), &h265_units)) {
        return 6;
    }
    if (h265_units.count != 5 ||
        !live_stream::media_codec::HasH265ParameterSets(h265_units) ||
        !live_stream::media_codec::HasCompleteH265ParameterSets(h265_units) ||
        !live_stream::media_codec::HasH265Keyframe(h265_units)) {
        return 7;
    }

    std::string vps;
    std::string h265_sps;
    std::string h265_pps;
    bool has_vps = false;
    bool has_h265_sps = false;
    bool has_h265_pps = false;
    live_stream::media_codec::ExtractH265ParameterSets(
        h265_units, &vps, &h265_sps, &h265_pps, &has_vps, &has_h265_sps,
        &has_h265_pps);
    if (!has_vps || !has_h265_sps || !has_h265_pps || vps.empty() ||
        h265_sps.empty() || h265_pps.empty()) {
        return 8;
    }

    if (h265_units.units[0].type != 35 || h265_units.units[1].type != 32 ||
        h265_units.units[2].type != 33 || h265_units.units[3].type != 34 ||
        h265_units.units[4].type != 19) {
        return 9;
    }
    return 0;
}
