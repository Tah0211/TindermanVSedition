#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NPROC=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

cleanup() { tput cnorm 2>/dev/null; }
trap cleanup EXIT

select_menu() {
  local prompt="$1"
  local selected="$2"
  shift 2
  local options=("$@")
  local count=${#options[@]}

  tput civis
  echo "$prompt"

  # 描画
  draw_menu() {
    for i in "${!options[@]}"; do
      tput el
      if [[ $i -eq $selected ]]; then
        printf "  > %s\n" "${options[$i]}"
      else
        printf "    %s\n" "${options[$i]}"
      fi
    done
  }

  draw_menu

  while true; do
    read -rsn1 key
    case "$key" in
      $'\x1b')
        read -rsn2 key
        case "$key" in
          '[A'|'[D') selected=$(( (selected - 1 + count) % count )) ;;
          '[B'|'[C') selected=$(( (selected + 1) % count )) ;;
        esac
        ;;
      '') break ;;
    esac
    # 再描画
    tput cuu "$count"
    draw_menu
  done

  tput cnorm
  MENU_RESULT=$selected
}

echo "=================================="
echo "      TINDERMAN -VS EDITION-      "
echo "=================================="
echo ""

# 起動対象の選択 (デフォルト: クライアント = index 0)
select_menu "起動対象を選択してください:" 0 "クライアント" "サーバー"
target=$MENU_RESULT
echo ""

case $target in
  1) # サーバー
    echo "--- サーバー起動 ---"
    echo "ホスト名: $(hostname)"
    IP=$(ipconfig getifaddr en0 2>/dev/null || hostname -I 2>/dev/null | awk '{print $1}' || echo "取得できません")
    echo "IPアドレス: $IP"
    echo ""

    read -p "ポート番号 [5000]: " port
    port=${port:-5000}

    echo ""
    echo "サーバーを起動します (ポート: $port)..."
    ./server/server --port "$port"
    ;;
  0) # クライアント
    echo "--- クライアント起動 ---"
    read -p "接続先ホスト名 [localhost]: " host
    host=${host:-localhost}
    read -p "ポート番号 [5000]: " port
    port=${port:-5000}

    echo ""
    echo "クライアントを起動します (接続先: $host:$port)..."
    ./tvse --hostname "$host" --port "$port"
    ;;
esac
