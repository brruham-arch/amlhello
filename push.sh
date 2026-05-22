#!/data/data/com.termux/files/usr/bin/bash
# push.sh — commit + push + watch CI dari Termux
# Usage: bash push.sh [pesan commit]

MSG="${1:-build: $(date '+%H%M%S')}"

cd "$(dirname "$0")"
git add -A
git commit -m "$MSG" || { echo "Nothing to commit"; exit 0; }
git push

echo ""
echo "Pushed. Menunggu CI..."
sleep 3

# Cek apakah gh tersedia
if command -v gh &>/dev/null; then
    RUN_ID=$(gh run list --limit 1 --json databaseId -q '.[0].databaseId')
    echo "Run ID: $RUN_ID"
    gh run watch "$RUN_ID"
    echo ""
    echo "=== Download artifact ==="
    gh run download "$RUN_ID" -n AMLHello-armeabi-v7a -D ./out/
    echo "File: $(ls out/)"
else
    echo "gh tidak ada. Install: pkg install gh"
    echo "Pantau di: https://github.com/$(git remote get-url origin | sed 's/.*github.com[:/]//' | sed 's/.git$//')/actions"
fi
