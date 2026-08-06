import time
import asyncio
import functools

from concurrent.futures import ThreadPoolExecutor

import orjson

from fastapi import APIRouter, Depends

from app.api.deps import get_cache, get_engine, get_pool
from app.core.config import settings
from app.core.security import require_secret
from app.enums import Server
from app.services.cache import MasterdataCache
from app.services.calc import DeckRecommendEngine, cal_deck_recommend
from app.schemas import RecommendRequest, RecommendResponse, ReloadResponse
from app.utils import algorithms, build_event_cards_config

router = APIRouter(dependencies=[Depends(require_secret)])


@router.post("/deck/recommend", response_model=RecommendResponse)
async def recommend(
    req: RecommendRequest,
    cache: MasterdataCache = Depends(get_cache),
    pool: ThreadPoolExecutor = Depends(get_pool),
    engine: DeckRecommendEngine = Depends(get_engine),
) -> RecommendResponse:

    generation = cache.generation_for(req.server)
    master_bytes = (
        await cache.get_master_bytes(req.server)
        if engine.needs_masterdata(req.server.value, generation)
        else None
    )
    music_metas_bytes = (
        await cache.get_musicmetas_bytes(req.server)
        if engine.needs_musicmetas(req.server.value, generation)
        else None
    )
    if req.bonus_cards and req.bonus_cards.force:
        event_cards_bytes = (
            master_bytes["eventCards"]
            if master_bytes is not None
            else await cache.get_event_cards_bytes(req.server)
        )
        event_cards_config_list = build_event_cards_config(
            {"eventCards": event_cards_bytes}, req.event_id, req.bonus_cards
        )
    else:
        event_cards_config_list = []

    runner = functools.partial(
        cal_deck_recommend,
        user_data_bytes=orjson.dumps(req.user_data),
        server=req.server.value,
        live_type=req.live_type.value,
        music_id=req.music_id,
        music_diff=req.music_diff,
        event_id=req.event_id,
        event_attr=req.event_attr,
        event_unit=req.event_unit,
        force_wl=req.force_wl,
        world_bloom_character_id=req.character_id,
        challenge_live_character_id=req.character_id,
        cal_tar=req.cal_tar.value,
        tar_bonus_list=req.tar_bonus_list,
        alg_list=[alg.value for alg in algorithms(req.cal_tar)],
        master_bytes=master_bytes,
        music_metas_bytes=music_metas_bytes,
        multi_live_teammate_power=req.multi_live_teammate_power,
        default_timeout_ms=settings.worker.default_timeout_ms,
        wl_timeout_ms=settings.worker.wl_timeout_ms,
        real_skill=req.real_skill,
        require_cards=[card.model_dump() for card in req.require_cards],
        event_cards_config_list=event_cards_config_list,
        require_characters=req.require_characters,
        card_config={rarity: cfg.model_dump() for rarity, cfg in req.card_config.items()},
        master_generation=generation,
        music_generation=generation,
        master_source_identity=cache.source_identity_for(req.server),
        music_source_identity=cache.music_source_identity_for(req.server),
        engine=engine,
    )

    loop = asyncio.get_running_loop()
    start = time.perf_counter()
    decks, durations = await loop.run_in_executor(pool, runner)
    queue_wait = (time.perf_counter() - start) - sum(durations.values())

    return RecommendResponse(decks=decks, durations=durations, queue_wait=queue_wait)


@router.post("/{server}/cache/reload", response_model=ReloadResponse)
async def reload_cache(server: Server, cache: MasterdataCache = Depends(get_cache)) -> ReloadResponse:
    await cache.reload(server)
    return ReloadResponse(status="ok")
