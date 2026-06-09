#pragma once
#include <cstdint>

namespace edgefocus {

struct BehaviorConfig {
    // 头部偏移确认时间 (8秒)
    int64_t looking_away_confirm_ms = 8000;
    
    // 低头宽限时间 (12秒，允许低头写字)
    int64_t head_down_grace_ms = 12000;
    
    // 闭眼/疲劳确认时间 (15秒)
    int64_t sleepy_confirm_ms = 15000;
    
    // 趴卧确认时间 (10秒)
    int64_t slumped_confirm_ms = 10000;
    
    // 手机使用确认时间 (2秒)
    int64_t phone_confirm_ms = 2000;
    
    // 离位确认时间 (3秒)
    int64_t away_confirm_ms = 3000;
    
    // 画面低置信度比例阈值
    float low_confidence_ratio = 0.4f;
    
    // 状态恢复迟滞时间 (3秒，避免来回抖动)
    int64_t state_hysteresis_ms = 3000;
};

} // namespace edgefocus
