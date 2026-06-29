# MKS DLC32 Firmware (Cartesian / Laser)
Modifié par Maddoc.lab's

Firmware personnalisé pour carte **MKS DLC32** basé sur **Grbl_ESP32**, avec adaptations orientées laser et interface MKS.

## Avertissement sécurité

Ce projet pilote un laser.

- Toujours porter des lunettes de protection adaptées a la longueur d'onde du module.
- Ne jamais laisser la machine sans surveillance.
- Tester d'abord a faible puissance.
- Prevoir un arret d'urgence facilement accessible.

## Base du projet

Ce firmware est un fork / une adaptation de Grbl_ESP32.

- Projet source: Grbl_ESP32
- Cible principale: MKS DLC32 V2.1
- Build system: PlatformIO

## Materiel cible

- Carte: MKS DLC32 (profil machine Cartesian)
- Environnement PlatformIO: `mks_dlc32_v2_1`
- Port serie exemple: `/dev/ttyUSB1`

## Fonctions et adaptations principales

- Correctif memoire LVGL pour eviter le crash du menu GRBL.
- Internationalisation UI (FR/EN) de plusieurs messages auparavant en dur.
- Reglage laser/pointer avec conservation du mode gravure rapide.
- Ajustements des valeurs par defaut laser (frequence et plage de puissance).
- Ajout d'un bouton pause physique (feed hold) avec anti-rebond.

## Comportement laser (important)

Le mode GRBL `$32=1` est conserve pour la gravure.

Pour les menus de controle/pointer:

- activation laser depuis l'UI: bascule temporaire en `$32=0`
- extinction laser depuis l'UI: retour automatique en `$32=1`

Cela permet d'allumer le laser hors deplacement pour le placement de l'origine, tout en gardant le mode laser GRBL pendant la gravure.

## Valeur S du menu laser (persistante)

La puissance S utilisee par le menu laser est sauvegardee en NVS et restauree au demarrage.

## Brochage specifique actuel (profil machine)

Dans le profil machine actif:

- `LASER_OUTPUT_PIN = GPIO_NUM_32`
- `SPINDLE_OUTPUT_PIN = GPIO_NUM_32`
- `COOLANT_FLOOD_PIN = GPIO_NUM_22`

Verifier le fichier de profil machine avant flash si vous adaptez le cablage.

## Compilation

Depuis le dossier `Firmware`:

```bash
platformio run -e mks_dlc32_v2_1
```

## Flash

```bash
platformio run -e mks_dlc32_v2_1 --target upload --upload-port /dev/ttyUSB1
```

## Monitor serie

```bash
platformio device monitor --port /dev/ttyUSB1
```

## Parametres GRBL utiles (exemples)

- `$32=1` : mode laser (grave)
- `$32=0` : mode spindle classique (tests pointer hors mouvement)
- `$X` : unlock si machine en ALARM

## Licence

Ce projet derive de Grbl_ESP32 et reste distribue sous licence **GNU GPL v3.0** (ou compatible selon les obligations amont).

Conserver les en-tetes de licence/copyright des fichiers d'origine.

## Credits

- Grbl / Grbl_ESP32 maintainers
- MKS ecosystem contributors
- Adaptations et integration: auteur de ce fork
