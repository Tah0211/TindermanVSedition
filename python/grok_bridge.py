#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
grok_bridge.py（方式A：二段階プロンプト固定版 / TINDERMAN VS Edition）
"""

import json
import os
import sys
from typing import Dict, Any, List

# ============================================================
# requests (externals)
# ============================================================
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
EXTERNALS = os.path.join(BASE_DIR, "externals")
if EXTERNALS not in sys.path:
    sys.path.insert(0, EXTERNALS)

import requests


# ============================================================
# API
# ============================================================
API_KEY = "xai-p1EuChyZo2SzJVQSeG9fPOpXnMkEZsJmH09Zyux089PxZqYwi8ElztfT3VW6zMppYOaP2ZV258jsMFvP"  # ← あなたのキー
API_BASE = "https://api.x.ai/v1"
API_MODEL = "grok-4-1-fast-reasoning"


# ============================================================
# Paths
# ============================================================
ROOT_DIR = os.path.dirname(BASE_DIR)
BUILD_PATH = os.path.join(ROOT_DIR, "build.json")
LOGS_DIR = os.path.join(ROOT_DIR, "logs")
CHAT_LOG_PATH = os.path.join(LOGS_DIR, "chat.json")
PROMPT_DIR = os.path.join(ROOT_DIR, "assets", "prompts")


# ============================================================
# JSON utility
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
    default = {"girl_id": "himari", "affection": 0, "stats": {}, "skills": []}
    obj = read_json_safe(BUILD_PATH, default)
    for k, v in default.items():
        obj.setdefault(k, v)
    return obj


def save_build(obj: Dict[str, Any]) -> None:
    write_json_safe(BUILD_PATH, obj)


# ============================================================
# prompts (himari.json / kiritan.json / sayo.json...)
# ============================================================
def load_system_prompt(girl_id: str) -> str:
    path = os.path.join(PROMPT_DIR, f"{girl_id}.json")
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
            if isinstance(data, dict) and "system" in data:
                return data["system"]
            return str(data)
    except Exception:
        return "You MUST reply ONLY with JSON."


# ============================================================
# log
# ============================================================
def load_chat_log() -> List[Dict[str, Any]]:
    data = read_json_safe(CHAT_LOG_PATH, {"messages": []})
    msgs = data.get("messages", [])
    return msgs if isinstance(msgs, list) else []


def save_chat_log(msgs: List[Dict[str, Any]]) -> None:
    write_json_safe(CHAT_LOG_PATH, {"messages": msgs})


# ============================================================
# Grok API
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

    response = requests.post(url, json=payload, headers=headers, timeout=45)
    response.raise_for_status()

    data = response.json()
    return data["choices"][0]["message"]["content"]


# ============================================================
# parse LLM reply
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
    girl_id = build.get("girl_id", "himari")
    affection = int(build.get("affection", 0))

    system_prompt = load_system_prompt(girl_id)
    past = load_chat_log()

    # ==========================================================
    # 方式A：system + user（二段プロンプト）
    # ==========================================================
    messages = [
        {
            "role": "system",
            "content": "You MUST strictly obey the following user instructions."
        },
        {
            "role": "user",
            "content": system_prompt
        }
    ]

    # 過去ログを追加
    for m in past:
        messages.append(m)

    # 今回のユーザー入力
    messages.append({"role": "user", "content": user_text})

    # ==========================================================
    # LLM 呼び出し
    # ==========================================================
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

    # ログ保存
    past.append({"role": "user", "content": user_text})
    past.append({
        "role": "assistant",
        "content": ai_text,
        "emotion": emotion,
        "heart_delta": hd
    })
    save_chat_log(past)

    # C 側へ返す
    print(json.dumps({
        "text": ai_text,
        "heart_delta": hd,
        "emotion": emotion,
        "affection": affection
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
