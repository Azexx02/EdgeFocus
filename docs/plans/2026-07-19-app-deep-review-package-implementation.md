# EdgeFocus APP 深度审查包实施计划

1. 创建独立 staging 目录，按白名单复制当前 APP 源码、资源、测试、构建配置与文档，不使用 `git archive`。
2. 复制三份 APP 需求和三份主机接口文档，不复制主机源码。
3. 在 staging 根目录生成 `REVIEW_GUIDE.md`、`PACKAGE_CONTENTS.md` 和 `SOURCE_STATE.md`。
4. 仅对 staging 中的 Markdown 文档替换本机绝对路径、旧 ADB 地址和设备标识；源码保持字节一致。
5. 执行敏感信息、符号链接、禁止二进制和必要文件扫描，发现问题即停止。
6. 生成候选 ZIP，解压后以 JDK 17 / Android SDK 34 运行单元测试、clean Debug 构建和仪器测试编译。
7. 把验证结论写入 `VALIDATION.md`，生成覆盖除自身外所有文件的 SHA-256 manifest。
8. 生成最终 ZIP，再次解压，验证 manifest、隐私、二进制白名单和目录完整性。
9. 交付 ZIP 的绝对路径、SHA-256、文件数和大小。

