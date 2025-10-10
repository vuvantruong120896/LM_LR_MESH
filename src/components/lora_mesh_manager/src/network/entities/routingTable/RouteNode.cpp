#include "RouteNode.h"
#include "../../../core/BuildOptions.h"
#include <algorithm>
#include <cstdint>

float RouteNode::calculateLinkQuality() {
    // Normalize RSSI score (0.0 - 1.0)
    // RSSI range: -120 dBm (very poor) to -30 dBm (excellent)
    // Reference: -80 dBm is "good"
    float rssiScore = 0.0f;
    if (receivedRSSI != 0) {
        // Map RSSI to 0-1 scale
        // -30 dBm -> 1.0 (excellent)
        // -80 dBm -> 0.5 (reference good)
        // -120 dBm -> 0.0 (very poor)
        float normalizedRSSI = (receivedRSSI - RSSI_MIN_THRESHOLD) / 
                               (float)(RSSI_REFERENCE_GOOD - RSSI_MIN_THRESHOLD);
        rssiScore = std::max(0.0f, std::min(1.0f, normalizedRSSI));
    } else {
        // No RSSI data (multi-hop route) - use neutral score
        rssiScore = 0.5f;
    }

    // Normalize SNR score (0.0 - 1.0)
    // SNR range: -20 dB (very poor) to +15 dB (excellent)
    // Reference: +5 dB is "good"
    float snrScore = 0.0f;
    if (receivedSNR != 0 || receivedRSSI != 0) {
        // Map SNR to 0-1 scale
        // +15 dB -> 1.0 (excellent)
        // +5 dB -> 0.5 (reference good)
        // -20 dB -> 0.0 (very poor)
        float snrMin = -20.0f;
        float snrMax = 15.0f;
        float normalizedSNR = (receivedSNR - snrMin) / (snrMax - snrMin);
        snrScore = std::max(0.0f, std::min(1.0f, normalizedSNR));
    } else {
        // No SNR data (multi-hop route) - use neutral score
        snrScore = 0.5f;
    }

    // Normalize hop count score (0.0 - 1.0)
    // Lower hop count = better score
    // 1 hop -> 1.0 (best)
    // MAX_HOP_COUNT -> 0.0 (worst)
    float hopScore = 1.0f - ((float)networkNode.metric / (float)(MAX_HOP_COUNT + 1));
    hopScore = std::max(0.0f, std::min(1.0f, hopScore));

    // Calculate weighted composite score
    float compositeScore = (LINK_QUALITY_RSSI_WEIGHT * rssiScore) +
                          (LINK_QUALITY_SNR_WEIGHT * snrScore) +
                          (LINK_QUALITY_HOP_WEIGHT * hopScore);

    return compositeScore;
}
