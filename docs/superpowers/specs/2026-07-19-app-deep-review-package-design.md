# EdgeFocus APP 深度审查包设计

日期：2026-07-19

## 目的

生成一份可直接上传到网页版 ChatGPT 深度研究的 ZIP。审查对象必须是当前工作区真实版本，包括尚未提交但已构建和实机测试的 APP 修改。用户会另外提供完整主机源码包，因此本包只携带 APP 理解所必需的主机接口契约，不重复打包主机源码。

## 包含内容

### APP 可构建源码

- `app/` 中除 `build/` 外的源码、资源、单元测试、仪器测试、Room schema 和 ProGuard 配置。
- 根目录 `build.gradle.kts`、`settings.gradle.kts`、`gradle.properties`、`gradlew.bat`、`.gitignore`。
- `gradle/wrapper/`，包括 wrapper 配置和构建所需 JAR。

### APP 文档

- `docs/` 当前实际存在的架构、规格、计划和交接文档。
- 不恢复或伪造工作区中已删除的旧架构文档。

### 产品适配需求

- `C:/Users/Aze/Desktop/APP/新app迭代必看/` 中的 Markdown 需求与目标架构材料，复制到包内 `references/app_requirements/`。

### 主机接口参考

- 主机仓库 `docs/integration/` 中与 APP、BLE、手环和状态同步有关的 Markdown 文档，复制到 `references/host_integration/`。
- 不包含主机 C++/Python 源码、构建目录、模型或运行产物。

### 审查入口

包根目录新增 `REVIEW_GUIDE.md`，内容包括：

- 产品和运行环境概述。
- APP 与主机的职责边界。
- 当前代码快照、最近提交和未提交修改说明。
- 已知故障历史和近期修复。
- 建议深度检查的问题清单。
- 构建方式和测试基线。
- 明确要求审查者区分“代码证据”“推断”和“需要主机源码验证的事项”。

包根目录新增 `PACKAGE_CONTENTS.md`，解释目录结构、包含/排除范围以及两个源码包如何配合阅读。

## 排除内容

- `.git/`、`.gradle/`、`.idea/`。
- `local.properties` 和任何本机 SDK 绝对路径。
- `app/build/`、APK、DEX、编译缓存和测试生成物。
- `_workspace/` 中的截图、UI XML、日志和临时调试材料。
- 私钥、签名文件、访问令牌、设备配对数据和 ADB 信息。
- 完整主机源码。

## 快照和可追溯性

- 源码通过文件复制生成，不依赖 `git archive`，从而包含当前未提交的新文件和修改。
- `REVIEW_GUIDE.md` 记录打包时 HEAD 和工作区为 dirty，但不复制 `.git`。
- ZIP 生成后重新展开到临时校验目录，检查必要入口、源码、测试和参考文档存在。
- 对 ZIP 计算 SHA-256，并在交付信息中提供文件数、未压缩大小和压缩大小。

## 推荐审查重点

1. BLE GATT 服务发现、自动重连、队列串行化、分块响应与超时。
2. 本地模式与 EdgeFocus 主机模式的状态隔离。
3. `IDLE/PRECHECK/FOCUSING/PAUSED/COMPLETED` 映射与迟到状态防回退。
4. 预检确认、云台自动搜索/手动租约/故障降级的竞态。
5. 主机权威剩余时间与 APP 平滑倒计时同步。
6. 前台应用分类、通知频率和 Android/MIUI 后台限制。
7. Compose 生命周期、单例控制器、协程取消与闪退风险。
8. NFC 横向控制面板和主 APP 前台身份。
9. 数据库、事件 ACK、幂等序列号与覆盖安装兼容性。
10. 安全、隐私、权限、日志泄漏和发布构建风险。

## 验收标准

- ZIP 可正常解压且不含绝对本机路径配置。
- 当前新增的 `PrecheckConfirmationPolicy`、`BoardRemainingTimePolicy` 及其测试必须存在。
- APP 主源码、资源和构建脚本完整。
- 需求材料与主机接口文档各自位于独立目录。
- `REVIEW_GUIDE.md` 可作为网页版 ChatGPT 的首个阅读入口。
- 不包含 `.git`、构建缓存、APK、调试截图/XML 或完整主机源码。

