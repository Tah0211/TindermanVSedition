#!/bin/bash

make clean
echo "クリーン完了"

make all
echo "クライアントビルド完了"

make server
echo "サーバービルド完了"