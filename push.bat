@echo off
REM --- 1. 変更を全部ステージング ---
git add .

REM --- 2. コミットメッセージを引数から受け取る（なければデフォルト） ---
set msg=%1
if "%msg%"=="" set msg=update

git commit -m "%msg%"

REM --- 3. main ブランチへ push ---
git push origin Study_Eth_perf
