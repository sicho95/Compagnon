#include "brain.h"

void brain_init() {
  // TODO : init mémoire court-terme, chargement profil utilisateur
  Serial.println("[BRAIN] Initialisé");
}

void brain_tick() {
  // TODO : tâches périodiques (agenda, rappels, surveillance réseau)
}

const char* brain_process(const char* input) {
  // TODO : pipeline de traitement IA
  // 1. Analyse intention
  // 2. Appel API LLM via WiFi (ou traitement local)
  // 3. Retourne réponse
  return "[brain] non implémenté";
}
