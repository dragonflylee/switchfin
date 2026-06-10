#pragma once

#include <string>

/// Posters de genres Kometa (Default-Images) pour les cartes de l'onglet
/// « Genres » : Plex n'expose AUCUN thumb sur /library/sections/{key}/genre
/// (vérifié serveur 2026-06-10 : Directory = fastKey/key/title/type seulement).
///
/// Source : https://github.com/Kometa-Team/Default-Images (branche master,
/// dossier genre/, 275 posters 2:3 en 2000x3000) — le set d'images par défaut
/// de Kometa, doc : https://kometa.wiki/en/latest/defaults/both/genre/
/// ⚠️ Le dépôt n'a PAS de licence déclarée (api.github.com → license: null) ;
/// les images sont chargées à la volée, rien n'est redistribué dans l'app.
namespace GenreImage {

/// URL ABSOLUE du poster Kometa pour un titre de genre Plex, ou "" si aucun
/// poster ne correspond (la carte garde alors son placeholder, aucune requête
/// n'est émise — le set de fichiers connus est embarqué, donc jamais de 404).
///
/// L'URL retournée passe par le transcodeur photo du serveur Plex
/// (/photo/:/transcode?url=<raw.githubusercontent.com/...>) : il télécharge
/// et redimensionne l'original 2000x3000 (~600 Ko, soit une texture RGBA de
/// 24 Mo — rédhibitoire sur Switch) vers la même boîte 325 que les autres
/// affiches (vérifié serveur 2026-06-10 : 200, JPEG 325x488 ~15 Ko, y compris
/// noms avec espace/&/+/apostrophe).
///
/// La correspondance est insensible à la casse et couvre les libellés
/// français/italiens des agents Plex (« Comédie » → Comedy, « Science-Fiction »
/// → Science Fiction, « Dramma » → Drama…).
std::string posterUrl(const std::string& title);

}  // namespace GenreImage
