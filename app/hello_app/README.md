# Contest 2026 Team 499 (不着急的INTP) - HTTPS Web Service App

本目录映射到 openvela `packages/demos/contest2026_499_hello_app`，为全志 F1C100s 平台专属的高性能静态 Web / HTTPS 服务应用。

## 功能特性

1. **TLS / HTTPS 安全加密传输**：
   - 基于 Mbed TLS 3.4.0 硬件/软件加密协议栈；
   - 默认内嵌 10.0.0.2 自签证书与私钥，同时支持从 SD 卡根目录（`/mnt/server.crt` 和 `/mnt/server.key`）动态加载自定义证书；
   - 绑定端口 443（`https://10.0.0.2/`）。

2. **SD 卡全静态网站宿主（支持 v86-toy 等复杂 Web 应用）**：
   - 以 SD 卡挂载点 `/mnt` 作为 Web 根目录；
   - 支持主流 MIME 类型映射（`.html`, `.js`, `.mjs`, `.css`, `.wasm`, `.wasm.gz`, `.png`, `.svg` 等）；
   - 支持 Single Page Application (SPA) 路由 Fallback 到 `/mnt/index.html`；
   - 具备路径穿透（`..`）安全过滤机制。

3. **高性能流式分块传输与断点续传**：
   - 完美支持 HTTP `Range: bytes=start-end` 请求（返回 HTTP 206 Partial Content），满足浏览器高效按需流式加载超大 WASM 镜像（如 Linux 系统镜像）；
   - 注入现代浏览器隔离响应头：
     - `Cross-Origin-Opener-Policy: same-origin`
     - `Cross-Origin-Embedder-Policy: require-corp`
     - `Cross-Origin-Resource-Policy: same-origin`
     允许前端网页利用 `SharedArrayBuffer` 与多线程 WebAssembly；
   - 内置 8 秒 Socket 超时保护机制，防止慢连接或异常挂起耗尽套接字资源。

## 注册指令

系统生成两个 NSH 命令行工具：
- `httpsd`：后台/前台启动 HTTPS Web 服务器；系统启动脚本 `/etc/init.d/rcS` 在网络初始化就绪后自动拉起 `httpsd &`。
- `hello_app`：团队应用入口，展示队伍信息并调用 HTTPS 服务。
