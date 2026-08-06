import time
import threading
from contextlib import nullcontext
from typing import Optional

from sekai_deck_recommend_cpp import (
    SekaiDeckRecommend,
    DeckRecommendCardConfig,
    DeckRecommendOptions,
    DeckRecommendSingleCardConfig,
)

from app.enums import DefaultImage
from app.schemas import RecommendCard, RecommendDeck


class DeckRecommendEngine:
    def __init__(self) -> None:
        self._deckers: dict[int, SekaiDeckRecommend] = {}
        self._lock = threading.Lock()
        self._master_generations: dict[tuple[str, int], int] = {}
        self._music_generations: dict[tuple[str, int], int] = {}

    def needs_masterdata(self, server: str, variant: int, generation: int) -> bool:
        with self._lock:
            return self._master_generations.get((server, variant)) != generation

    def needs_musicmetas(self, server: str, variant: int, generation: int) -> bool:
        with self._lock:
            return self._music_generations.get((server, variant)) != generation


def cal_deck_recommend(
    user_data_bytes: bytes,
    server: str,
    live_type: str,
    music_id: int,
    music_diff: str,
    event_id: Optional[int],
    event_attr: Optional[str],
    event_unit: Optional[str],
    force_wl: bool,
    world_bloom_character_id: Optional[int],
    challenge_live_character_id: Optional[int],
    cal_tar: str,
    tar_bonus_list: list[int],
    alg_list: list[str],
    master_bytes: dict[str, bytes] | None,
    music_metas_bytes: bytes | None,
    multi_live_teammate_power: int,
    default_timeout_ms: int,
    wl_timeout_ms: int,
    real_skill: Optional[int],
    require_cards: list[dict],
    event_cards_config_list: list[dict],
    require_characters: list[int],
    card_config: dict[str, dict],
    master_generation: int = 0,
    master_variant: int = 1,
    music_generation: int = 0,
    engine: DeckRecommendEngine | None = None,
) -> tuple[list[RecommendDeck], dict[str, float]]:

    decker = SekaiDeckRecommend() if engine is None else None
    with (nullcontext() if engine is None else engine._lock):
        if engine is not None:
            decker = engine._deckers.get(master_variant)
            if decker is None:
                decker = SekaiDeckRecommend()
                engine._deckers[master_variant] = decker
            master_key = (server, master_variant)
            if engine._master_generations.get(master_key) != master_generation:
                if master_bytes is None:
                    raise RuntimeError("masterdata payload is required for a new generation")
                decker.update_masterdata_from_strings(master_bytes, server)
                engine._master_generations[master_key] = master_generation
            if engine._music_generations.get(master_key) != music_generation:
                if music_metas_bytes is None:
                    raise RuntimeError("music metas payload is required for a new generation")
                decker.update_musicmetas_from_string(music_metas_bytes, server)
                engine._music_generations[master_key] = music_generation
        else:
            if master_bytes is None or music_metas_bytes is None:
                raise RuntimeError("masterdata and music metas payloads are required")
            decker.update_masterdata_from_strings(master_bytes, server)
            decker.update_musicmetas_from_string(music_metas_bytes, server)

        options = DeckRecommendOptions()
        options.target = cal_tar
        options.region = server
        options.user_data_str = user_data_bytes

        options.live_type = live_type

        options.music_id = music_id
        options.music_diff = music_diff

        options.timeout_ms = default_timeout_ms

        if event_cards_config_list:
            options.single_card_configs = [DeckRecommendSingleCardConfig.from_dict(card) for card in event_cards_config_list]
            options.fixed_cards = [card["card_id"] for card in require_cards] + [card["card_id"] for card in event_cards_config_list]
        elif require_cards:
            options.single_card_configs = [DeckRecommendSingleCardConfig.from_dict(card) for card in require_cards]
            options.fixed_cards = [card["card_id"] for card in require_cards]
        elif require_characters:
            options.fixed_characters = require_characters

        if live_type == "multi":
            options.multi_live_teammate_score_up = real_skill
            options.multi_live_teammate_power = multi_live_teammate_power

        if tar_bonus_list:
            options.target_bonus_list = tar_bonus_list
            options.limit = max(1, 6 // len(tar_bonus_list))
        else:
            options.limit = 6
            options.rarity_1_config = DeckRecommendCardConfig.from_dict(card_config.get('1'))
            options.rarity_2_config = DeckRecommendCardConfig.from_dict(card_config.get('2'))
            options.rarity_3_config = DeckRecommendCardConfig.from_dict(card_config.get('3'))
            options.rarity_4_config = DeckRecommendCardConfig.from_dict(card_config.get('4'))
            options.rarity_birthday_config = DeckRecommendCardConfig.from_dict(card_config.get('birthday'))

        if live_type == "challenge":
            options.challenge_live_character_id = challenge_live_character_id
            options.fixed_characters = None
        elif force_wl:
            options.event_id = event_id
            options.world_bloom_character_id = world_bloom_character_id
            options.timeout_ms = wl_timeout_ms
        else:
            options.event_id = event_id
            options.event_attr = event_attr
            options.event_unit = event_unit

        durations: dict[str, float] = {}
        result_list = []

        for alg in alg_list:
            options.algorithm = alg
            start_time = time.perf_counter()
            result = decker.recommend(options)
            durations[alg] = time.perf_counter() - start_time
            result_list.append((alg, result))

    # 合并去重，记录每个卡组来自哪些算法
    decks = []
    sources_map: dict[str, str] = {}
    for alg, result in result_list:
        for deck in result.decks:
            key = f"{deck.total_power}_{deck.score}_{deck.cards[0].card_id}"
            if key not in sources_map:
                sources_map[key] = alg
                decks.append(deck)
            else:
                sources_map[key] += f"+{alg}"

    def deck_key(deck):
        if cal_tar == "power":
            return deck.total_power
        if cal_tar == "skill":
            return deck.multi_live_score_up
        if cal_tar == "bonus":
            return (deck.event_bonus_rate, deck.score)
        return (deck.score, deck.multi_live_score_up)

    decks.sort(key=deck_key, reverse=True)

    return [
        RecommendDeck(
            total_power=deck.total_power,
            score=deck.score,
            multi_live_score_up=deck.multi_live_score_up,
            cards=[
                RecommendCard(
                    card_id=c.card_id,
                    master_rank=c.master_rank,
                    skill_level=c.skill_level,
                    skill_score_up=c.skill_score_up,
                    default_image=DefaultImage(c.default_image),
                    canvas=c.has_canvas_bonus,
                )
                for c in deck.cards
            ],
            event_bonus_rate=deck.event_bonus_rate,
            support_deck_bonus_rate=deck.support_deck_bonus_rate,
            source=sources_map[f"{deck.total_power}_{deck.score}_{deck.cards[0].card_id}"],
        )
        for deck in decks[:6]
    ], durations
