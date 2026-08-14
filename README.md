# Resona-Deck-Recommend

组卡 HTTP 服务的纯 C++20 实现。直接链接
[`sekai-deck-recommend-cpp`](https://github.com/Resonalofi/sekai-deck-recommend-cpp)
库。



接口：

```text
POST /deck/recommend
POST /{jp|cn|tw}/cache/reload
Header: X-Resona-Secret
```

## 本地构建

依赖 CMake 3.20+、C++20 编译器和 OpenSSL 3（仅 Linux）。默认目录结构为：

```text
parent/
├── Resona-Deck-Recommend/
└── sekai-deck-recommend-cpp/
```

Linux：

```bash
cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSEKAI_DECK_RECOMMEND_SOURCE_DIR=../sekai-deck-recommend-cpp
cmake --build build/release --parallel
./build/release/resona-deck-recommend \
  --config ./config.toml \
  --data ../sekai-deck-recommend-cpp/data
```

Windows（Developer PowerShell）：

```powershell
cmake -S . -B build/native `
  "-DSEKAI_DECK_RECOMMEND_SOURCE_DIR=..\sekai-deck-recommend-cpp"
cmake --build build/native --config Release --parallel
.\build\native\Release\resona-deck-recommend.exe `
  --config .\config.toml `
  --data ..\sekai-deck-recommend-cpp\data
```

从 `config.example.toml` 创建本机配置。

## Docker

镜像通过 BuildKit named context 同时接收服务和 Core 两个仓库：

```bash
docker buildx build --load \
  --build-context core=../sekai-deck-recommend-cpp \
  -t resona-deck-recommend:local .
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

- `ci.yml`：在 Linux x86_64、Windows x86_64 和 macOS arm64 runner 上编译原生程序，并
  上传三个独立构建产物。
- `release.yml`：推送 `v*` tag 或手动触发后编译并上传 Linux x86_64、Windows x86_64 和
  macOS arm64 release 程序。

