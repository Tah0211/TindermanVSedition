#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
python/grok_bridge.py（emotionをbuild.jsonに保存しない版）
- LLM 返答の emotion は 8大感情キー（英語）に強制正規化して C 側に返す
- build.json には affection 等は保存するが、emotion は保存しない
- emotion のフォールバック（直前維持）は logs/chat.json の直近 assistant emotion を参照
- build.json が壊れていても、最低限のキーをサルベージして “正しいJSON” に書き直せる
- APIキーは環境変数のみ（直書きデフォルトなし）
"""

import json
import os
import sys
import re
from typing import Dict, Any, List, Optional, Tuple

import requests

# ============================================================
# path
# ============================================================

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(BASE_DIR)

BUILD_PATH = os.path.join(ROOT_DIR, "build.json")
CHAT_LOG_PATH = os.path.join(ROOT_DIR, "logs", "chat.json")
PROMPT_DIR = os.path.join(ROOT_DIR, "assets", "prompts")
DEBUG_LOG_PATH = os.path.join(ROOT_DIR, "logs", "debug_llm.log")

# ============================================================
# API
# ============================================================

API_BASE = os.getenv("XAI_API_BASE", "https://api.x.ai/v1")
API_MODEL = os.getenv("XAI_API_MODEL", "grok-4-1-fast-reasoning")

# 必須：環境変数で設定（例：export XAI_API_KEY="xxxxx"）
API_KEY = os.getenv("XAI_API_KEY", "xai-0We0HBC9bxwag9Pnr2f7KbQOU3v5zMomrCTVDieeWFkt6kj3m13GBgO25w58OiQXAqO6YrlRdLVkAuQA ").strip()

# ============================================================
# emotion (strict)
# ============================================================

EMOTION_KEYS = [
    "trust",
    "joy",
    "anticipation",
    "surprise",
    "fear",
    "sadness",
    "anger",
    "disgust",
]

# 日本語や揺れを 8キーに寄せる（保険）
EMOTION_MAP = {
    # trust
    "穏やか": "trust", "安心": "trust", "落ち着き": "trust", "信頼": "trust", "やさしい": "trust", "優しい": "trust",
    # joy
    "嬉しい": "joy", "うれしい": "joy", "喜び": "joy", "楽しい": "joy", "にこにこ": "joy",
    # anticipation
    "興味津々": "anticipation", "興味": "anticipation", "期待": "anticipation", "わくわく": "anticipation", "ドキドキ": "anticipation",
    # surprise
    "驚き": "surprise", "びっくり": "surprise", "吃驚": "surprise",
    # fear
    "不安": "fear", "少し不安": "fear", "こわい": "fear", "怖い": "fear", "緊張": "fear",
    # sadness
    "悲しい": "sadness", "かなしい": "sadness", "寂しい": "sadness", "さみしい": "sadness", "落ち込む": "sadness",
    # anger
    "怒り": "anger", "いらだち": "anger", "ムカつく": "anger",
    # disgust
    "嫌悪": "disgust", "嫌": "disgust", "拒否": "disgust", "うんざり": "disgust",
}

def normalize_emotion(raw: Any, fallback: str) -> str:
    """
    - 8キーならそのまま
    - 大文字小文字/空白は正規化
    - 日本語等は辞書でマップ
    - 不明なら fallback（neutralは使わない）
    """
    if isinstance(raw, str):
        e = raw.strip().lower()
    else:
        e = ""

    if e in EMOTION_KEYS:
        return e

    if isinstance(raw, str):
        mapped = EMOTION_MAP.get(raw.strip())
        if mapped in EMOTION_KEYS:
            return mapped

    if isinstance(raw, str):
        low = raw.lower()
        for k in EMOTION_KEYS:
            if k in low:
                return k

    return fallback if fallback in EMOTION_KEYS else "trust"

# ============================================================
# util
# ============================================================

def read_text_safe(path: str) -> str:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read()
    except Exception:
        return ""

def read_json_safe(path: str, default: Any) -> Any:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return default

def write_json_safe(path: str, obj: Any):
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)

def clamp(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, v))

# ============================================================
# debug log
# ============================================================

def debug_log(title: str, content: str):
    d = os.path.dirname(DEBUG_LOG_PATH)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(DEBUG_LOG_PATH, "a", encoding="utf-8") as f:
        f.write("\n" + "=" * 60 + "\n")
        f.write(f"[{title}]\n")
        f.write(content)
        f.write("\n")

# ============================================================
# build.json (emotion は保持しない)
# ============================================================

def _salvage_build_from_broken_json(text: str, default: Dict[str, Any]) -> Dict[str, Any]:
    """
    build.json が壊れて json.load できない場合でも、
    よく使うキーだけは雑に拾って “できるだけ元を保ったまま” 復旧する。
    """
    obj = dict(default)

    # string: "key": "value"
    def pick_str(key: str):
        m = re.search(rf'"{re.escape(key)}"\s*:\s*"([^"]*)"', text)
        if m:
            obj[key] = m.group(1)

    # int: "key": 123
    def pick_int(key: str):
        m = re.search(rf'"{re.escape(key)}"\s*:\s*(-?\d+)', text)
        if m:
            try:
                obj[key] = int(m.group(1))
            except Exception:
                pass

    # bool: "key": true/false
    def pick_bool(key: str):
        m = re.search(rf'"{re.escape(key)}"\s*:\s*(true|false)', text, flags=re.IGNORECASE)
        if m:
            obj[key] = (m.group(1).lower() == "true")

    pick_str("girl_id")
    pick_int("affection")
    pick_int("phase")
    pick_int("turn_in_phase")
    pick_bool("core_used")

    # 他に必要ならここに追加
    return obj

def load_build() -> Dict[str, Any]:
    default: Dict[str, Any] = {
        "girl_id": "himari",
        "affection": 30,
        "phase": 0,
        "turn_in_phase": 0,
        "core_used": False,
    }

    # まず正規JSONとして読む
    obj = read_json_safe(BUILD_PATH, None)
    if isinstance(obj, dict):
        for k, v in default.items():
            obj.setdefault(k, v)
        # emotion はこのスクリプトでは扱わない（残ってても放置）
        return obj

    # 壊れている場合はサルベージして復旧
    raw = read_text_safe(BUILD_PATH)
    if raw:
        obj = _salvage_build_from_broken_json(raw, default)
    else:
        obj = dict(default)

    # ここで “正しいJSON” に書き直しておく（以後の事故防止）
    try:
        write_json_safe(BUILD_PATH, obj)
    except Exception:
        pass

    return obj

def save_build(obj: Dict[str, Any]):
    # emotion は保存しない（キーがあってもそのまま書くかどうかは好みだが、
    # ここでは「buildを丸ごと保存」するので、他が使ってるキーは保持される）
    write_json_safe(BUILD_PATH, obj)

# ============================================================
# chat log -> last emotion fallback
# ============================================================

def load_last_emotion_from_chat_log() -> str:
    """
    logs/chat.json の末尾から、assistant の emotion を探して返す。
    無ければ trust。
    """
    obj = read_json_safe(CHAT_LOG_PATH, {"messages": []})
    msgs = obj.get("messages", [])
    if not isinstance(msgs, list):
        return "trust"

    # 末尾から探す
    for m in reversed(msgs):
        if not isinstance(m, dict):
            continue
        if m.get("role") == "assistant":
            e = m.get("emotion")
            if isinstance(e, str):
                e2 = e.strip().lower()
                if e2 in EMOTION_KEYS:
                    return e2
            # 旧形式などで assistant 内に埋め込まれてた場合も救う
            if "content" in m and isinstance(m["content"], str):
                low = m["content"].lower()
                for k in EMOTION_KEYS:
                    if k in low:
                        return k

    return "trust"

# ============================================================
# character / system prompt builder
# ============================================================

def load_character(girl_id: str) -> Dict[str, Any]:
    path = os.path.join(PROMPT_DIR, f"{girl_id}.json")
    return read_json_safe(path, {})

def build_system_prompt(char: Dict[str, Any], build: Dict[str, Any]) -> str:
    name = char.get("name", "Unknown")

    surface = "、".join(char.get("personality", {}).get("surface", []))
    core = "、".join(char.get("personality", {}).get("core", []))
    rules = "。".join(char.get("personality", {}).get("rule", []))

    affection = int(build.get("affection", 30))

    band_desc = ""
    for k, v in (char.get("affection_bands") or {}).items():
        if "-" in k:
            lo, hi = map(int, k.split("-"))
            if lo <= affection <= hi:
                band_desc = f"現在の関係性：{v['label']}。口調：{v['tone']}。踏み込み：{v['allowed_depth']}。"
        elif k.endswith("+"):
            if affection >= int(k[:-1]):
                band_desc = f"現在の関係性：{v['label']}。口調：{v['tone']}。踏み込み：{v['allowed_depth']}。"

    core_event = char.get("core_event", {})
    theme = core_event.get("theme", "")
    succ = core_event.get("success_condition", [])
    fail = core_event.get("failure_condition", [])

    emotion_rule = (
        "emotion は必ず次の英語キーのいずれか1つのみを返してください（日本語は禁止）：\n"
        "- trust\n- joy\n- anticipation\n- surprise\n- fear\n- sadness\n- anger\n- disgust\n"
        "複数/配列/併記は禁止。"
    )

    return f"""
あなたは{name}です。

表の性格：
{surface}

内面：
{core}

行動ルール：
{rules}

{band_desc}

核心テーマ：
「{theme}」

成功条件：
- {" / ".join(succ)}

失敗条件：
- {" / ".join(fail)}

以下を必ず守ってください：
- キャラクターとして振る舞う
- 日本語で話す
- 返答は JSONのみ（前後に文章を付けない）
- 形式：{{"message": "...", "heart_delta": int, "emotion": string}}
- heart_delta は -10〜+10 の整数（目安）
- {emotion_rule}
""".strip()

# ============================================================
# LLM
# ============================================================

def call_llm(messages: List[Dict[str, str]]) -> str:
    if not API_KEY:
        raise RuntimeError("XAI_API_KEY が未設定です。環境変数 XAI_API_KEY を設定してください。")

    res = requests.post(
        API_BASE + "/chat/completions",
        headers={
            "Authorization": f"Bearer {API_KEY}",
            "Content-Type": "application/json"
        },
        json={
            "model": API_MODEL,
            "messages": messages,
            "temperature": 0.7
        },
        timeout=45
    )
    res.raise_for_status()
    return res.json()["choices"][0]["message"]["content"]

def _extract_json_object(raw: str) -> Optional[str]:
    s = raw.find("{")
    e = raw.rfind("}")
    if s == -1 or e == -1 or e < s:
        return None
    return raw[s:e+1]

def parse_llm_reply(raw: str, emotion_fallback: str) -> Dict[str, Any]:
    raw_json = _extract_json_object(raw)
    if not raw_json:
        return {"text": raw.strip(), "heart_delta": 0, "emotion": emotion_fallback}

    try:
        data = json.loads(raw_json)
    except Exception:
        return {"text": raw.strip(), "heart_delta": 0, "emotion": emotion_fallback}

    text = data.get("message", data.get("text", ""))
    if not isinstance(text, str):
        text = str(text)

    try:
        hd = int(data.get("heart_delta", 0))
    except Exception:
        hd = 0
    hd = clamp(hd, -10, 10)

    emotion = normalize_emotion(data.get("emotion"), emotion_fallback)

    return {"text": text, "heart_delta": hd, "emotion": emotion}

# ============================================================
# main
# ============================================================

def main():
    debug_log("TURN", "=" * 60)

    if len(sys.argv) < 2:
        print(json.dumps(
            {"text": "…", "heart_delta": 0, "emotion": "trust", "affection": 0},
            ensure_ascii=False
        ))
        return

    user_text = sys.argv[1]
    debug_log("USER INPUT", user_text)

    build = load_build()
    char = load_character(build.get("girl_id", "himari"))

    system_prompt = build_system_prompt(char, build)
    debug_log("SYSTEM PROMPT", system_prompt)

    messages: List[Dict[str, str]] = []
    messages.append({"role": "system", "content": system_prompt})

    log_obj = read_json_safe(CHAT_LOG_PATH, {"messages": []})
    log = log_obj.get("messages", [])
    if not isinstance(log, list):
        log = []

    for m in log:
        if not isinstance(m, dict):
            continue
        role = m.get("role", "")
        content = m.get("content", "")
        if role in ("user", "assistant") and isinstance(content, str):
            messages.append({"role": role, "content": content})

    messages.append({"role": "user", "content": user_text})

    debug_log("MESSAGES SENT", json.dumps(messages, ensure_ascii=False, indent=2))

    raw = call_llm(messages)
    debug_log("RAW LLM RESPONSE", raw)

    # ★ fallback は build.json ではなく chat log の直前 emotion
    emotion_fallback = load_last_emotion_from_chat_log()
    parsed = parse_llm_reply(raw, emotion_fallback)

    debug_log(
        "PARSED RESULT",
        f"text={parsed['text']}\nheart_delta={parsed['heart_delta']}\nemotion={parsed['emotion']}\nfallback={emotion_fallback}"
    )

    # build 更新（emotion は保存しない）
    build["affection"] = max(0, int(build.get("affection", 30)) + int(parsed["heart_delta"]))
    save_build(build)

    # ログ更新（assistant は text だけを content に入れる）
    log.append({"role": "user", "content": user_text})
    log.append({
        "role": "assistant",
        "content": parsed["text"],
        "emotion": parsed["emotion"],
        "heart_delta": parsed["heart_delta"]
    })
    write_json_safe(CHAT_LOG_PATH, {"messages": log})

    print(json.dumps({
        "text": parsed["text"],
        "heart_delta": parsed["heart_delta"],
        "emotion": parsed["emotion"],
        "affection": build["affection"]
    }, ensure_ascii=False))

if __name__ == "__main__":
    main()
