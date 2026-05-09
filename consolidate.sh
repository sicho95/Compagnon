#!/bin/bash
# Consolidation Nestor — Option B (cherry-pick)
# À exécuter depuis la racine du repo : bash consolidate.sh

set -e
REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

echo "=== Nestor Consolidation ==="
echo "Repo: $REPO"
echo ""

# Nettoyage lock files si présents
rm -f .git/index.lock .git/HEAD.lock .git/objects/maintenance.lock 2>/dev/null && echo "🧹 Lock files nettoyés" || true

# Config identité git locale
git config user.email "sicho95@gmail.com"
git config user.name "Damien"

echo ""
echo "=== État actuel ==="
git log --oneline -3
echo ""

# Fonction cherry-pick avec gestion conflit
cherry_pick() {
  local commit="$1"
  local label="$2"
  echo "--- Cherry-pick: $label ($commit)"

  if git cherry-pick -x "$commit" 2>&1; then
    echo "✅ OK: $label"
  else
    status=$(git status --porcelain | grep -E "^(AA|UU|DD|AU|UA|DU|UD)" | wc -l)
    if [ "$status" -gt 0 ]; then
      echo "⚠️  Conflit sur $label — résolution automatique (theirs)"
      git checkout --theirs . 2>/dev/null || true
      git add -A
      git -c core.editor=true cherry-pick --continue 2>&1 || {
        echo "❌ Échec --continue sur $label, abandon de ce commit"
        git cherry-pick --abort 2>/dev/null || true
      }
    else
      echo "⚠️  $label déjà appliqué ou vide, passage au suivant"
      git cherry-pick --abort 2>/dev/null || true
    fi
  fi
  echo ""
}

# Étape 3 : cherry-picks dans l'ordre chronologique
cherry_pick 3de9ced "GPS BLE, WiFi scan, ISIN (elastic-banach)"
cherry_pick 6f6ab28 "ESP32 rotation 270° (mystifying-satoshi)"
cherry_pick 6890b8c "LVGL noir AMOLED (romantic-moser)"
cherry_pick 398843b "LVGL noir pur (tender-snyder)"
cherry_pick fe2f304 "app Musique (lucid-kowalevski)"
cherry_pick 76632f7 "agent Maison (gallant-elgamal)"
cherry_pick f6803f3 "hamburger/WiFi (zealous-khorana)"
cherry_pick fe25e43 "meteo/musique real views (blissful-leavitt)"
cherry_pick a1cffe0 "hub initial view (mystifying-snyder)"
cherry_pick 0ca4257 "SPEC.md V4 (flamboyant-yonath)"

echo ""
echo "=== Log final (15 derniers commits) ==="
git log --oneline -15

echo ""
echo "=== Push vers origin/main ==="
git push origin main --force-with-lease
echo ""
echo "✅ Consolidation terminée !"
git log --oneline -5
