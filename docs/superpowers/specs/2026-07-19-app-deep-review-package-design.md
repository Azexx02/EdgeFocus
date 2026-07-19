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

- `APP_REQUIREMENTS_ROOT` 中明确允许的三个文件复制到 `references/app_requirements/`：
  - `app-host-alignment-v3.md`
  - `app-host-redesign-handoff-2026-07-16.md`
  - `app-target-architecture.md`

### 主机接口参考

- 主机仓库 `docs/integration/` 中明确允许的三个文件复制到 `references/host_integration/`：
  - `app-band-protocol-v2.1.md`
  - `app-host-alignment-v3.md`
  - `app-update-handoff-2026-07-15.md`
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

所有复制操作使用明确白名单，并递归拒绝 `build/`、`.gradle/`、`.idea/`、`.cxx/`、日志、抓包、测试生成物、APK/AAB/DEX、原生二进制、签名文件和符号链接。发现链接、越界路径或未知二进制时打包失败，不跟随链接。

允许的二进制仅限：

- `gradle/wrapper/gradle-wrapper.jar`。
- `app/src/` 下属于 Android 源资源的 PNG/JPG/JPEG/WEBP 字体或其他明确资源文件。

除此之外的 JAR、SO、DLL、EXE、APK、AAB、DEX、密钥库和编译产物一律拒绝。

## 隐私清理

- APP 源码、资源、测试和构建配置按原字节复制，不做内容重写。
- 文档副本在 staging 中进行机械清理，将用户名、本机绝对路径、旧无线 ADB 地址/端口和设备标识替换为符号占位符；不回写原仓库。
- 对 staging 和最终解压副本递归扫描本机绝对路径、IP/端口、ADB/mDNS 设备标识、令牌、私钥、API Key、签名文件和敏感文件名。
- 任意命中必须人工判定并清理；不能确认安全时打包失败。

## 快照和可追溯性

- 源码通过文件复制生成，不依赖 `git archive`，从而包含当前未提交的新文件和修改。
- `REVIEW_GUIDE.md` 记录打包时 HEAD、主机参考仓库 HEAD 和 dirty 状态，但使用符号来源名，不记录本机路径。
- `SOURCE_STATE.md` 记录 APP 的完整 `git status --short --untracked-files=all` 路径清单、包含/排除决定和已删除文件；不复制 `.git`。`_workspace/` 调试产物明确列为排除类别，可压缩汇总而不逐项展开。
- APP 源码 dirty 文件必须逐字节存在；排除的 dirty 文件必须在 `SOURCE_STATE.md` 解释原因。文档若因隐私清理发生变化，在清单中标为 `sanitized-copy`，不声称字节一致。
- `FILE_MANIFEST.sha256` 记录包内每个文件的相对路径、字节数、SHA-256 和类别。需求/主机参考还记录符号来源、来源相对文件名、来源仓库 HEAD/dirty 状态以及清理前 SHA-256。
- ZIP 生成后展开到新的校验目录，逐项比较 manifest 与实际内容，拒绝缺失、多余或哈希不符。
- 对最终解压目录再次执行敏感信息、敏感文件名、符号链接、路径越界和禁止二进制扫描。
- 从解压目录使用 JDK 17、Android SDK 34 运行 `gradlew.bat testDebugUnitTest` 和 `gradlew.bat clean assembleDebug`，结果写入 `VALIDATION.md`。验证生成的 `build/` 产物不回装进 ZIP。
- 对最终 ZIP 计算 SHA-256，并在交付信息中提供文件数、未压缩大小和压缩大小。

本包只携带 Windows `gradlew.bat`，因此声明为 Windows 构建复现包。构建需要 JDK 17、Android SDK 34 和首次解析依赖时的网络；不声称可直接在 Unix 上执行 wrapper。

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

- ZIP 可正常解压，内容和文件名中均不含绝对本机路径、旧无线 ADB 地址或设备标识。
- 当前新增的 `PrecheckConfirmationPolicy`、`BoardRemainingTimePolicy` 及其测试必须存在。
- APP 主源码、资源和构建脚本完整。
- 需求材料与主机接口文档各自位于独立目录。
- `REVIEW_GUIDE.md` 可作为网页版 ChatGPT 的首个阅读入口。
- 不包含 `.git`、构建缓存、APK、调试截图/XML 或完整主机源码。
- manifest 与解压内容完全一致，敏感扫描和禁止二进制扫描通过。
- 解压副本的单元测试与 clean Debug 构建通过，并在 `VALIDATION.md` 留有可复核结论。
