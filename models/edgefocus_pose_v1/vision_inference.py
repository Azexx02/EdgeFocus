"""
EdgeFocus 视觉推理模块
行为层调用此模块获取每帧视觉事实

确保行为层和训练用的预处理/后处理完全一致

用法:
    from vision_inference import EdgeFocusVision
    vision = EdgeFocusVision("edgefocus_pose_v1.onnx")
    result = vision.process_frame(frame)  # frame是BGR numpy数组
"""
import numpy as np
import cv2
import onnxruntime as ort


class EdgeFocusVision:
    def __init__(self, onnx_path="edgefocus_pose_v1.onnx"):
        self.sess = ort.InferenceSession(onnx_path)
        self.kpt_names = [
            'nose', 'left_eye', 'right_eye', 'left_ear', 'right_ear',
            'left_shoulder', 'right_shoulder', 'left_elbow', 'right_elbow',
            'left_wrist', 'right_wrist'
        ]

    def _letterbox(self, img, size=256):
        h, w = img.shape[:2]
        r = min(size / h, size / w)
        nw, nh = int(w * r), int(h * r)
        img = cv2.resize(img, (nw, nh))
        dw, dh = (size - nw) / 2, (size - nh) / 2
        top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
        left, right = int(round(dw - 0.1)), int(round(dw + 0.1))
        img = cv2.copyMakeBorder(img, top, bottom, left, right,
                                  cv2.BORDER_CONSTANT, value=(114, 114, 114))
        return img, r, (dw, dh)

    def process_frame(self, frame):
        """
        处理单帧, 返回视觉事实

        Args:
            frame: BGR numpy数组 (来自cv2.imread或cv2.VideoCapture)

        Returns:
            dict: {
                person_conf: float,          # 人体检测置信度
                bbox: {x, y, w, h},         # 原图坐标
                keypoints: {name: {x, y, conf}},  # 原图坐标
                quality: float,              # 画面质量 0-1
            }
        """
        h, w = frame.shape[:2]
        frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        # 预处理: letterbox + 0-1归一化 + RGB + NCHW
        img_lb, ratio, (dw, dh) = self._letterbox(frame_rgb, 256)
        img_in = (img_lb.astype(np.float32) / 255.0).transpose(2, 0, 1)[np.newaxis]

        # 推理
        output = self.sess.run(None, {'images': img_in})[0]
        pred = output[0].T  # [1344, 56]

        # 找最高置信度检测
        conf = pred[:, 4]
        best_idx = conf.argmax()
        person_conf = float(conf[best_idx])
        bbox_256 = pred[best_idx, :4]
        kpts_256 = pred[best_idx, 5:].reshape(17, 3)

        # 反算bbox到原图
        cx = float((bbox_256[0] - dw) / ratio)
        cy = float((bbox_256[1] - dh) / ratio)
        bw = float(bbox_256[2] / ratio)
        bh = float(bbox_256[3] / ratio)

        # 反算关键点到原图
        keypoints = {}
        for i, name in enumerate(self.kpt_names):
            keypoints[name] = {
                "x": round(float((kpts_256[i, 0] - dw) / ratio), 2),
                "y": round(float((kpts_256[i, 1] - dh) / ratio), 2),
                "conf": round(float(kpts_256[i, 2]), 4)
            }

        # 画面质量
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        brightness = np.mean(gray) / 255.0
        blur = cv2.Laplacian(gray, cv2.CV_64F).var()
        blur_score = min(blur / 500.0, 1.0)
        bright_score = 1.0 - abs(brightness - 0.5) * 2
        quality = float(round(bright_score * 0.5 + blur_score * 0.5, 4))

        return {
            "person_conf": round(person_conf, 4),
            "bbox": {
                "x": round(cx - bw / 2, 2),
                "y": round(cy - bh / 2, 2),
                "w": round(bw, 2),
                "h": round(bh, 2)
            },
            "keypoints": keypoints,
            "quality": quality,
        }


# ========== 测试 ==========
if __name__ == "__main__":
    import sys
    img_path = sys.argv[1] if len(sys.argv) > 1 else "test.jpg"

    vision = EdgeFocusVision("edgefocus_pose_v1.onnx")
    frame = cv2.imread(img_path)
    result = vision.process_frame(frame)

    print("=== 模型输出 ===")
    print(f"person_conf: {result['person_conf']}")
    print(f"bbox: {result['bbox']}")
    print(f"quality: {result['quality']}")
    print(f"\nkeypoints:")
    for name, kp in result['keypoints'].items():
        print(f"  {name:15s}: x={kp['x']:7.1f}, y={kp['y']:7.1f}, conf={kp['conf']:.4f}")
