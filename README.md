# Resona-Deck-Recommend

PJSK 组卡服务

## 配置

`location` 读不到时按顺序尝试 `fallback` 中的 url。

## 运行

```bash
uv sync
uv run main.py
```


## 接口

所有接口需带请求头 `X-Resona-Secret`。

### `POST /deck/recommend`

请求体见 `RecommendRequest`，响应见 `RecommendResponse`：

```json
{
  "decks": [ ... ],
  "durations": {"dfs": 0.12, "ga": 0.34},
  "queue_wait": 0.05
}
```

- 每个 deck 含 `source` 字段，标记该卡组来自哪些算法（如 `dfs+ga`）。
- `durations`：各算法计算耗时（秒）。
- `queue_wait`：请求等待空闲 worker 的排队耗时（秒）。

### `POST /{server}/cache/reload`

使该服 masterdata / music_metas generation 失效，并在下次请求时重新加载。
