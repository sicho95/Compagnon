/**
 * MIGRÉ — Ce fichier n'est plus utilisé.
 *
 * Les clés API sont désormais saisies directement dans les Réglages
 * de chaque application (Nestor, Bourse, Météo, Musique) et stockées
 * dans localStorage via src/core/settings-store.js
 *
 * La synchronisation vers l'ESP32 est automatique à chaque connexion BLE
 * via src/sync/key-sync.js
 *
 * Pour migrer vos anciennes clés :
 *   1. Ouvrez l'app → menu ☰ → Réglages
 *   2. Saisissez vos clés dans l'onglet correspondant
 *   3. Sauvegardez — elles seront automatiquement poussées vers l'ESP32
 */
export {};
