#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
grok_bridge.py（requests対応 完全版 / TINDERMAN VS Edition）

・APIキー直書き（xai-XXXX）
・Python 3.8〜3.12 互換
・外部 requests を python/externals から読み込む
・build.json / chat.json の読み書き
・assets/prompts/<girl>.json を読み込み
・xAI Grok API（/chat/completions）で会話生成
・C 側へ {text, heart_delta, emotion, affection} を1行のJSONで返す
"""

import json
import os
import sys
from typing import Dict, Any, List

# ============================================================
# ★ requests を externals から読み込む
# ============================================================
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
EXTERNALS = os.path.join(BASE_DIR, "externals")
if EXTERNALS not in sys.path:
    sys.path.insert(0, EXTERNALS)

import requests  # ← これで self-contained requests が読み込まれる


# ============================================================
# APIキー
# ============================================================
API_KEY = "xai-p1EuChyZo2SzJVQSeG9fPOpXnMkEZsJmH09Zyux089PxZqYwi8ElztfT3VW6zMppYOaP2ZV258jsMFvP"  # ← 本物を貼る
API_BASE = "https://api.x.ai/v1"
API_MODEL = "grok-4-1-fast-reasoning"


# ============================================================
# パス
# ============================================================
ROOT_DIR = os.path.dirname(BASE_DIR)
BUILD_PATH = os.path.join(ROOT_DIR, "build.json")
LOGS_DIR = os.path.join(ROOT_DIR, "logs")
CHAT_LOG_PATH = os.path.join(LOGS_DIR, "chat.json")
PROMPT_DIR = os.path.join(ROOT_DIR, "assets", "prompts")


# ============================================================
# JSON util
# ============================================================
def read_json_safe(path: str, default: Any) -> Any:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return default


def write_json_safe(path: str, obj: Any) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)


def clamp(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, v))


# ============================================================
# build.json
# ============================================================
def load_build() -> Dict[str, Any]:
    default = {"girl_id": "", "affection": 0, "stats": {}, "skills": []}
    obj = read_json_safe(BUILD_PATH, default)
    for k, v in default.items():
        obj.setdefault(k, v)
    return obj


def save_build(obj: Dict[str, Any]) -> None:
    write_json_safe(BUILD_PATH, obj)


# ============================================================
# prompts
# ============================================================
def load_system_prompt(girl_id: str) -> str:
    path = os.path.join(PROMPT_DIR, f"{girl_id}.json")
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        return (
            "You are the heroine in TINDERMAN VS Edition. "
            "Reply ONLY in JSON: {\"text\":\"...\",\"heart_delta\":1,\"emotion\":\"joy\"}"
        )

    if isinstance(data, dict):
        if "system" in data:
            return data["system"]
        if "prompt" in data:
            return data["prompt"]

    if isinstance(data, str):
        return data

    return (
        "You are the heroine in TINDERMAN VS Edition. "
        "Return ONLY JSON."
    )


# ============================================================
# chat log
# ============================================================
def load_chat_log() -> List[Dict[str, Any]]:
    data = read_json_safe(CHAT_LOG_PATH, {"messages": []})
    msgs = data.get("messages", [])
    return msgs if isinstance(msgs, list) else []


def save_chat_log(msgs: List[Dict[str, Any]]) -> None:
    write_json_safe(CHAT_LOG_PATH, {"messages": msgs})


# ============================================================
# Grok API（requests版 → Cloudflare回避）
# ============================================================
def call_llm(messages: List[Dict[str, str]]) -> str:

    url = API_BASE + "/chat/completions"
    headers = {
        "Authorization": f"Bearer {API_KEY}",
        "Content-Type": "application/json",
        "Accept": "application/json",
        "User-Agent": "TindermanVS/1.0"
    }

    payload = {
        "model": API_MODEL,
        "messages": messages,
        "temperature": 0.7
    }

    try:
        response = requests.post(
            url,
            json=payload,
            headers=headers,
            timeout=45
        )
        response.raise_for_status()
        data = response.json()
        return data["choices"][0]["message"]["content"]

    except Exception as e:
        raise RuntimeError(str(e))


# ============================================================
# LLM返答 JSON抽出
# ============================================================
def parse_llm_reply(raw: str) -> Dict[str, Any]:
    s = raw.find("{")
    e = raw.rfind("}")
    raw_json = raw[s:e+1] if s != -1 and e != -1 else raw

    try:
        data = json.loads(raw_json)
    except Exception:
        return {"text": raw, "heart_delta": 0, "emotion": "neutral"}

    return {
        "text": data.get("text", raw),
        "heart_delta": clamp(int(data.get("heart_delta", 0)), -10, 10),
        "emotion": data.get("emotion", "neutral")
    }


# ============================================================
# main
# ============================================================
def main() -> None:

    if len(sys.argv) < 2:
        print(json.dumps({
            "text": "（エラー：入力なし）",
            "heart_delta": 0,
            "emotion": "neutral",
            "affection": 0
        }, ensure_ascii=False))
        return

    user_text = sys.argv[1]

    build = load_build()
    girl_id = build.get("girl_id") or "himari"
    affection = int(build.get("affection", 0))

    system_prompt = load_system_prompt(girl_id)
    past = load_chat_log()

    messages = [{"role": "system", "content": system_prompt}]
    for m in past:
        if isinstance(m.get("role"), str) and isinstance(m.get("content"), str):
            messages.append(m)

    messages.append({"role": "user", "content": user_text})

    try:
        raw = call_llm(messages)
        parsed = parse_llm_reply(raw)
    except Exception as e:
        print(json.dumps({
            "text": f"接続エラー：{e}",
            "heart_delta": 0,
            "emotion": "neutral",
            "affection": affection
        }, ensure_ascii=False))
        return

    ai_text = parsed["text"]
    hd = parsed["heart_delta"]
    emotion = parsed["emotion"]
    affection = max(0, affection + hd)

    build["affection"] = affection
    save_build(build)

    past.append({"role": "user", "content": user_text})
    past.append({
        "role": "assistant",
        "content": ai_text,
        "emotion": emotion,
        "heart_delta": hd
    })
    save_chat_log(past)

    print(json.dumps({
        "text": ai_text,
        "heart_delta": hd,
        "emotion": emotion,
        "affection": affection
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
