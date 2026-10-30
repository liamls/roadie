# Synchro audio / vidéo

Comment la guitare (carte son) et l'image (webcam) restent calées.

- La guitare passe par la carte son, l'image par la webcam : deux horloges, deux latences. Roadie horodate chaque bloc audio avec l'horodatage hôte de CoreAudio (`hostTimeNs`, `mach_absolute_time`) et chaque image avec son PTS AVFoundation converti sur la même horloge hôte (`CMSyncConvertTime` depuis l'horloge de session). Le placement de la vidéo dans l'export découle directement de ces deux horodatages (`Take::getVideoOffsetSeconds`).
- Ce que l'estimation ne voit pas (latence réelle des convertisseurs, marge de sécurité CoreAudio) est **mesuré** par la calibration : Roadie écoute 5 s simultanément l'entrée de la carte son et le micro de la caméra (horodaté par AVFoundation comme les images) ; la même attaque entendue des deux côtés donne le décalage (`SyncAnalysis::measureLag`, précision < 1 ms en test). Il est mémorisé et appliqué à chaque prise.
- Un clap n'est pas capté par une entrée guitare (micro de guitare) : d'où la consigne « accord étouffé bien sec », que la carte son reçoit par la guitare et que le micro de la caméra entend acoustiquement.
