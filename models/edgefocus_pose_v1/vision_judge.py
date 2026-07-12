"""
EdgeFocus 视觉判断模块
行为层调用此模块获取每帧视觉事实

用法:
    from vision_judge import VisionJudge
    judge = VisionJudge("vision_config.json")
    result = judge.process_frame(person_conf=0.85, keypoints={...}, bbox_h=400)
"""
import json

class VisionJudge:
    def __init__(self, config_path="vision_config.json"):
        with open(config_path, 'r', encoding='utf-8') as f:
            cfg = json.load(f)
        t = cfg["thresholds"]
        self.AWAY_THRESH = t["presence_conf_below_away"]
        self.UNCERTAIN_THRESH = t["presence_conf_below_uncertain"]
        self.HEAD_DOWN_THRESH = t["head_down_score_above"]
        self.MIN_VISIBLE = t["keypoints_min_visible"]

    def calc_features(self, person_conf, keypoints, bbox_h):
        """
        计算视觉特征

        Args:
            person_conf: 人体检测置信度 (0-1)
            keypoints: dict, 每个key是关键点名, value是 {"x", "y", "conf"}
            bbox_h: bbox高度 (像素)

        Returns:
            dict: {presence_conf, head_down_score, head_valid}
        """
        # 可见关键点数
        keypoint_names = ["nose", "left_eye", "right_eye", "left_shoulder", "right_shoulder"]
        visible = sum(1 for k in keypoint_names
                      if keypoints.get(k, {}).get("conf", 0) > 0.25)
        visible_ratio = visible / len(keypoint_names)

        # presence_conf
        presence_conf = person_conf * visible_ratio

        # head_down_score
        head_down_score = 0
        head_valid = False
        nose = keypoints.get("nose", {})
        ls = keypoints.get("left_shoulder", {})
        rs = keypoints.get("right_shoulder", {})
        nc = nose.get("conf", 0)
        lsc = ls.get("conf", 0)
        rsc = rs.get("conf", 0)

        if nc >= 0.25 and (lsc >= 0.25 or rsc >= 0.25):
            head_valid = True
            if lsc >= 0.25 and rsc >= 0.25:
                sy = (ls["y"] + rs["y"]) / 2
            elif lsc >= 0.25:
                sy = ls["y"]
            else:
                sy = rs["y"]
            gap = sy - nose["y"]
            expected = bbox_h * 0.30
            head_down_score = max(0, min(1, 1 - gap / max(expected, 1)))

        return {
            "presence_conf": round(presence_conf, 4),
            "head_down_score": round(head_down_score, 4),
            "head_valid": head_valid,
        }

    def judge_frame(self, person_conf, keypoints, bbox_h):
        """
        单帧视觉事实 (不输出行为结论)

        Returns:
            dict: {
                presence: "PRESENT" / "AWAY" / "UNCERTAIN",
                presence_conf: float,
                head_state: "NORMAL" / "DOWN" / "INVALID",
                head_down_score: float,
            }
        """
        feat = self.calc_features(person_conf, keypoints, bbox_h)

        # presence
        pc = feat["presence_conf"]
        if pc < self.AWAY_THRESH:
            presence = "AWAY"
        elif pc < self.UNCERTAIN_THRESH:
            presence = "UNCERTAIN"
        else:
            presence = "PRESENT"

        # head_state
        if not feat["head_valid"]:
            head_state = "INVALID"
        elif feat["head_down_score"] > self.HEAD_DOWN_THRESH:
            head_state = "DOWN"
        else:
            head_state = "NORMAL"

        return {
            "presence": presence,
            "presence_conf": feat["presence_conf"],
            "head_state": head_state,
            "head_down_score": feat["head_down_score"],
        }


# ========== 行为层时间窗口判断 ==========
class BehaviorLayer:
    """
    行为层: 用滑动窗口判断最终状态
    行为层不关心视觉细节, 只接收 VisionJudge 的输出
    """
    def __init__(self, config_path="vision_config.json"):
        with open(config_path, 'r', encoding='utf-8') as f:
            cfg = json.load(f)
        w = cfg["window_ms"]
        self.AWAY_CONFIRM = w["away_confirm_ms"]
        self.HEAD_DOWN_CONFIRM = w["head_down_confirm_ms"]
        self.HYSTERESIS = w["state_hysteresis_ms"]

        self.away_start_ms = None
        self.head_down_start_ms = None
        self.last_state = "FOCUSED"
        self.last_switch_ms = 0

    def update(self, timestamp_ms, vision_result):
        """
        每帧调用

        Args:
            timestamp_ms: 当前帧时间戳 (ms)
            vision_result: VisionJudge.judge_frame() 的输出

        Returns:
            str: "FOCUSED" / "AWAY" / "HEAD_DOWN" / "LOW_CONFIDENCE"
        """
        presence = vision_result["presence"]
        head = vision_result["head_state"]

        # 低置信度
        if vision_result["presence_conf"] < 0.10:
            return "LOW_CONFIDENCE"

        # 离位计时
        if presence == "AWAY":
            if self.away_start_ms is None:
                self.away_start_ms = timestamp_ms
            elif timestamp_ms - self.away_start_ms >= self.AWAY_CONFIRM:
                if self.last_state != "AWAY" and timestamp_ms - self.last_switch_ms > self.HYSTERESIS:
                    self.last_state = "AWAY"
                    self.last_switch_ms = timestamp_ms
                return "AWAY"
        else:
            self.away_start_ms = None

        # 趴桌计时
        if head == "DOWN":
            if self.head_down_start_ms is None:
                self.head_down_start_ms = timestamp_ms
            elif timestamp_ms - self.head_down_start_ms >= self.HEAD_DOWN_CONFIRM:
                if self.last_state != "HEAD_DOWN" and timestamp_ms - self.last_switch_ms > self.HYSTERESIS:
                    self.last_state = "HEAD_DOWN"
                    self.last_switch_ms = timestamp_ms
                return "HEAD_DOWN"
        else:
            self.head_down_start_ms = None

        # 正常
        if self.last_state != "FOCUSED" and timestamp_ms - self.last_switch_ms > self.HYSTERESIS:
            self.last_state = "FOCUSED"
            self.last_switch_ms = timestamp_ms
        return "FOCUSED"


# ========== 测试 ==========
if __name__ == "__main__":
    judge = VisionJudge("vision_config.json")
    behavior = BehaviorLayer("vision_config.json")

    # 模拟数据
    test_frames = [
        (0,    0.85, {"nose": {"x": 300, "y": 100, "conf": 0.9},
                      "left_shoulder": {"x": 200, "y": 300, "conf": 0.8},
                      "right_shoulder": {"x": 400, "y": 300, "conf": 0.8}}, 400),
        (1000, 0.85, {"nose": {"x": 300, "y": 100, "conf": 0.9},
                      "left_shoulder": {"x": 200, "y": 300, "conf": 0.8},
                      "right_shoulder": {"x": 400, "y": 300, "conf": 0.8}}, 400),
        (2000, 0.85, {"nose": {"x": 300, "y": 250, "conf": 0.9},
                      "left_shoulder": {"x": 200, "y": 300, "conf": 0.8},
                      "right_shoulder": {"x": 400, "y": 300, "conf": 0.8}}, 400),
    ]

    print("时间   presence        head_state  行为状态")
    print("-" * 60)
    for ts, pc, kpts, bh in test_frames:
        vr = judge.judge_frame(pc, kpts, bh)
        state = behavior.update(ts, vr)
        print(f"{ts:5d}  {vr['presence']:10s} {vr['presence_conf']:.3f}  "
              f"{vr['head_state']:10s} {vr['head_down_score']:.3f}  {state}")
