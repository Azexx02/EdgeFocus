# 模型许可

本仓库中的 `models/**/*.nb` 权重文件（包括 `edgefocus_pose_v1_1.nb`、`yolo11n_pose_256_float.nb`，以及后续由 EdgeFocus 训练/量化流程从 YOLO11n-Pose 导出的权重）是 **Ultralytics YOLO11n-Pose** 的衍生物，按 **GNU Affero General Public License v3.0（AGPL-3.0）** 分发。

- 完整许可证文本：[`LICENSES/AGPL-3.0.txt`](LICENSES/AGPL-3.0.txt)
- 上游项目：<https://github.com/ultralytics/ultralytics>
- 如果你要再分发这些权重或其衍生物，需要遵守 AGPL-3.0（包括向网络用户提供对应源码的义务）。
- 如果你的使用场景不能接受 AGPL-3.0（例如闭源商用），请从仓库中移除 `.nb` 文件，或替换为许可证兼容的模型。

仓库其余原创代码、文档与硬件设计按 **Apache License 2.0** 分发，见 [`LICENSE`](LICENSE)。

> 模型与代码是分开授权的：`.nb` 文件不属于 Apache-2.0 内容，请勿把它们当作 Apache-2.0 素材再分发。
