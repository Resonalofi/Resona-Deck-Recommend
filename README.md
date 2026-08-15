# Resona-Deck-Recommend

组卡 HTTP 服务的纯 C++20 实现。
[`sekai-deck-recommend-cpp`](https://github.com/Resonalofi/sekai-deck-recommend-cpp)
库。

接口：

```text
GET  /healthz
POST /deck/recommend
POST /{jp|cn|tw}/cache/reload
Header: X-Resona-Secret
```

`GET /healthz` 不需要 secret，返回 `ok`。

## 本地构建

依赖 CMake 3.20+、C++20 编译器和 OpenSSL 3（仅 Linux）。

```bash
git clone https://github.com/Resonalofi/Resona-Deck-Recommend.git
cd Resona-Deck-Recommend
git submodule update --init
git -C sekai-deck-recommend-cpp submodule update --init 3rdparty/json
```

Linux：

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
./build/release/resona-deck-recommend \
  --config ./config.toml \
  --data ./sekai-deck-recommend-cpp/data
```

Windows（Developer PowerShell）：

```powershell
cmake -S . -B build/native
cmake --build build/native --config Release --parallel
.\build\native\Release\resona-deck-recommend.exe `
  --config .\config.toml `
  --data .\sekai-deck-recommend-cpp\data
```

从 `config.example.toml` 创建本机配置。

Core 更新后，把 submodule 指到新 commit 再提交：

```bash
git -C sekai-deck-recommend-cpp fetch
git -C sekai-deck-recommend-cpp checkout <commit>
git add sekai-deck-recommend-cpp
git commit -m "Bump sekai-deck-recommend-cpp"
```

## Docker

直接编即可：

```bash
docker buildx build --load -t resona-deck-recommend:local .
```

容器内必须监听 `0.0.0.0`；对外暴露范围由宿主的 publish 地址控制。先复制
`config.docker.example.toml` 并设置 secret、主数据源，再运行：

```bash
docker run --rm --name resona-deck-recommend \
  -p 127.0.0.1:23457:23457 \
  --mount type=bind,src="$PWD/config.docker.toml",dst=/etc/resona-deck-recommend/config.toml,readonly \
  --mount type=volume,src=resona-deck-data,dst=/var/lib/resona-deck-recommend \
  ghcr.io/resonalofi/resona-deck-recommend:latest
```

回源下载会写入
`/var/lib/resona-deck-recommend`，因此该目录应使用持久卷。

```bash
docker compose up -d
```

## CI/CD


- `ci.yml`：在 Linux x86_64、Windows x86_64 和 macOS arm64 runner 上编译原生程序，并上传三个独立构建产物。
- `release.yml`：推送 `v*` tag 后编译三个平台包，生成 SHA256，创建 GitHub Release。手动触发只编包，不发 Release。
- `docker.yml`：在 amd64 / arm64 原生机上构建镜像，合并成多架构 manifest 推到 `ghcr.io`。`main` 上再 `docker compose pull && up --no-build`，并请求 `http://127.0.0.1:23457/healthz`。
- Dependabot 每周检查 GitHub Actions 和 Docker 基础镜像。

`main` 自动部署需要 GitHub Environment `production` 和这些 secrets：`DEPLOY_HOST`、`DEPLOY_USER`、`DEPLOY_SSH_KEY`，可选 `DEPLOY_PORT`、`DEPLOY_DIR`（默认 `/opt/Resona-Deck-Recommend`）、`CONFIG_TOML`
