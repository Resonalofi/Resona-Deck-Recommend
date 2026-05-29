from typing import Optional

from pydantic import BaseModel

from app.enums import Server, LiveType, CalTar, DefaultImage


class BonusCards(BaseModel):
    force: bool = False
    max_mr: bool = False
    max_skill: bool = False
    canvas: bool = False


class CardConfigEntry(BaseModel):
    disable: bool = False
    level_max: bool = True
    episode_read: bool = True
    master_max: bool = False
    skill_max: bool = False
    canvas: bool = False


class RequireCard(BaseModel):
    card_id: int
    level_max: bool = True
    episode_read: bool = True
    master_max: bool = False
    skill_max: bool = False
    canvas: bool = False


class RecommendRequest(BaseModel):
    server: Server
    live_type: LiveType = LiveType.multi
    force_wl: bool = False
    character_id: Optional[int] = None
    music_id: int
    music_diff: str
    event_id: Optional[int] = None
    event_attr: Optional[str] = None
    event_unit: Optional[str] = None
    cal_tar: CalTar = CalTar.score
    tar_bonus_list: list[int] = []
    real_skill: Optional[int] = None
    multi_live_teammate_power: int = 250000
    require_cards: list[RequireCard] = []
    require_characters: list[int] = []
    card_config: dict[str, CardConfigEntry] = {}
    bonus_cards: Optional[BonusCards] = None
    user_data: dict


class RecommendCard(BaseModel):
    card_id: int
    master_rank: int
    skill_level: int
    skill_score_up: int
    default_image: DefaultImage
    canvas: bool


class RecommendDeck(BaseModel):
    total_power: int
    score: float
    multi_live_score_up: float
    cards: list[RecommendCard]
    event_bonus_rate: float
    support_deck_bonus_rate: float
    source: str


class RecommendResponse(BaseModel):
    decks: list[RecommendDeck]
    durations: dict[str, float]
    queue_wait: float


class ReloadResponse(BaseModel):
    status: str
