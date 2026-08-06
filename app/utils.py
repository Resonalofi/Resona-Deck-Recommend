import orjson

from app.enums import Algorithm, CalTar
from app.schemas import BonusCards


def algorithms(cal_tar: CalTar) -> list[Algorithm]:
    if cal_tar is CalTar.bonus:
        return [Algorithm.dfs]
    return [Algorithm.dfs, Algorithm.ga]


def build_event_cards_config(master_bytes: dict[str, bytes], event_id: int | None, bonus_cards: BonusCards | None) -> list[dict]:
    if not (bonus_cards and bonus_cards.force):
        return []
    event_cards = orjson.loads(master_bytes["eventCards"])
    return [
        {
            "card_id": int(bc["cardId"]),
            "level_max": True,
            "episode_read": True,
            "master_max": bonus_cards.max_mr,
            "skill_max": bonus_cards.max_skill,
            "canvas": bonus_cards.canvas,
        }
        for bc in event_cards
        if bc["eventId"] == event_id and bc["bonusRate"] == 20.0
    ]
