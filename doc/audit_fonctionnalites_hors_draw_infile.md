# Audit des fonctionnalites inactives ou inachevees (hors draw infile)

Date: 2026-05-02  
Perimetre: Grbl_Esp32/src (hors module draw infile)  
Objectif: connaitre l'existant inactif/inacheve pour eventuelle activation ou finalisation plus tard.

## Comment lire ce document
- Statut Inactif: present mais desactive ou non branche par configuration/usage.
- Statut Partiel: present, mais comportement non termine ou TODO/FIXME explicite.
- Ce document ne propose pas de suppression. Il sert de base de decision.

## 1) GCode

- Element: Delimiteur de programme %
- Statut: Partiel
- Preuves:
  - Grbl_Esp32/src/GCode.cpp:94
  - Grbl_Esp32/src/GCode.cpp:1608
- Explication: le parseur mentionne explicitement que la fonction % (start/end program) n'est pas installee. Le commentaire indique un impact potentiel sur la reprise de programme.
- Interet possible: fiabiliser les scenarios de reprise et de pilotage "programme vs manuel".

- Element: Conditions de sync M3/mouvements zero-length
- Statut: Partiel
- Preuve:
  - Grbl_Esp32/src/GCode.cpp:1307
- Explication: TODO sur les conditions de synchronisation pour certains mouvements sans entree planner.
- Interet possible: coherence laser/spindle sur cas limites de trajectoire.

## 2) Settings / Runtime reconfiguration

- Element: Callbacks settings incomplets (homing/limits/spindle)
- Statut: Partiel
- Preuves:
  - Grbl_Esp32/src/SettingsDefinitions.cpp:18
  - Grbl_Esp32/src/SettingsDefinitions.cpp:34
  - Grbl_Esp32/src/SettingsDefinitions.cpp:36
  - Grbl_Esp32/src/SettingsDefinitions.cpp:40
  - Grbl_Esp32/src/SettingsDefinitions.cpp:379
  - Grbl_Esp32/src/SettingsDefinitions.cpp:391
  - Grbl_Esp32/src/SettingsDefinitions.cpp:394
  - Grbl_Esp32/src/SettingsDefinitions.cpp:396
  - Grbl_Esp32/src/SettingsDefinitions.cpp:402
- Explication: plusieurs TODO indiquent que certains changements de settings devraient declencher des re-initialisations (limits_init, step invert masks, spindle init) mais ne sont pas totalement relies.
- Interet possible: appliquer immediatement et correctement des changements de config sans redemarrage.

- Element: Cas jog potentiellement special a parser
- Statut: Partiel
- Preuve:
  - Grbl_Esp32/src/ProcessSettings.cpp:89
- Explication: TODO indiquant un besoin de traitement special de jog dans le parser/settings.
- Interet possible: robustesse du comportement jog apres modifications de settings.

## 3) WebUI / API

- Element: Authentification ADMIN_ONLY/default
- Statut: Partiel
- Preuve:
  - Grbl_Esp32/src/WebUI/Authentication.cpp:5
- Explication: TODO explicite sur politique ADMIN_ONLY et comportement par defaut sans parametre.
- Interet possible: securite et clarte des droits API.

- Element: listDirLocalFS remonte erreurs SD au lieu FS
- Statut: Partiel
- Preuves:
  - Grbl_Esp32/src/WebUI/WebSettings.cpp:818
  - Grbl_Esp32/src/WebUI/WebSettings.cpp:823
- Explication: FIXME explicite sur la taxonomie d'erreurs. Le code reutilise des erreurs SD pour FS local.
- Interet possible: diagnostics plus precis cote web/clients.

- Element: Parsing commandes web inefficace O(n^2)
- Statut: Partiel
- Preuve:
  - Grbl_Esp32/src/WebUI/WebServer.cpp:497
- Explication: TODO explicite sur la complexite et proposition d'alternative plus lineaire.
- Interet possible: meilleures perfs avec gros payloads multi-lignes.

- Element: Validation email notifications absente
- Statut: Partiel
- Preuves:
  - Grbl_Esp32/src/WebUI/NotificationsService.cpp:295
  - Grbl_Esp32/src/WebUI/NotificationsService.cpp:309
- Explication: TODO explicite "check valid email" manquant.
- Interet possible: eviter configs invalides et faux positifs de mise en service.

- Element: Nettoyage constantes WiFiConfig
- Statut: Partiel
- Preuve:
  - Grbl_Esp32/src/WebUI/WifiConfig.h:28
- Explication: TODO indique que certaines constantes ne devraient pas etre dans ce module.
- Interet possible: maintenance plus simple et separation des responsabilites.

## 4) Spindle

- Element: Piecewise linear spindle (activation par config)
- Statut: Inactif par defaut + Partiel en implementation runtime
- Preuves:
  - Grbl_Esp32/src/Config.h:591
  - Grbl_Esp32/src/Config.h:595
  - Grbl_Esp32/src/Spindles/PWMSpindle.cpp:149
  - Grbl_Esp32/src/Spindles/PWMSpindle.cpp:194
- Explication: le mode est desactive par macro, et meme quand chemin active, le calcul piecewise est note TODO (warning "not implemented yet").
- Interet possible: linearisation reelle puissance/RPM pour gravure plus fidele.

- Element: VFD speed modifiers / linearization
- Statut: Partiel
- Preuve:
  - Grbl_Esp32/src/Spindles/VFDSpindle.cpp:563
- Explication: TODO explicite pour ajouter override/linearisation de vitesse.
- Interet possible: comportement VFD coherent avec les autres modes de spindle.

- Element: YL620/H2A traitement etat vitesse
- Statut: Partiel
- Preuves:
  - Grbl_Esp32/src/Spindles/YL620Spindle.cpp:229
  - Grbl_Esp32/src/Spindles/H2ASpindle.cpp:142
- Explication: TODO sur la destination de l'info de vitesse (sys.spindle_speed, etat vfd, etc.).
- Interet possible: telemetrie et reporting vitesse fiables.

## 5) Motion / Limits / Stepper

- Element: Abstraction pin-specific limits
- Statut: Partiel
- Preuves:
  - Grbl_Esp32/src/MotionControl.cpp:316
  - Grbl_Esp32/src/Limits.cpp:78
- Explication: TODO de portabilite pour deplacer appels pin-specifiques dans une abstraction commune.
- Interet possible: portage machine/board plus propre.

- Element: Cycle probe et cycle-start non-auto
- Statut: Partiel
- Preuve:
  - Grbl_Esp32/src/MotionControl.cpp:397
- Explication: TODO explicite sur l'obeissance au mode non-auto cycle start.
- Interet possible: predictibilite des sequences de probing.

- Element: Divers TODO stepper/ISR
- Statut: Partiel
- Preuves:
  - Grbl_Esp32/src/Stepper.cpp:53
  - Grbl_Esp32/src/Stepper.cpp:195
  - Grbl_Esp32/src/Stepper.cpp:433
  - Grbl_Esp32/src/Stepper.cpp:646
- Explication: plusieurs points de dette technique (variables a retirer, gestion ISR/positions, cas de deceleration).
- Interet possible: robustesse temps reel et clarte maintenance.

## 6) Motors / Drivers

- Element: Trinamic UART fonctions utilite potentiellement non exploitees
- Statut: Partiel / a confirmer
- Preuves:
  - Grbl_Esp32/src/Motors/TrinamicUartDriver.h:103
  - Grbl_Esp32/src/Motors/TrinamicUartDriver.h:126
  - Grbl_Esp32/src/Motors/TrinamicUartDriverClass.cpp:81
  - Grbl_Esp32/src/Motors/TrinamicUartDriverClass.cpp:233
- Explication: TODO explicites sur utilite verifiee et configurations StallGuard/microsteps.
- Interet possible: diagnostics drivers et anti-perte de pas.

- Element: StandardStepper enable pin
- Statut: Partiel
- Preuve:
  - Grbl_Esp32/src/Motors/StandardStepper.cpp:6
- Explication: TODO "Add an enable pin".
- Interet possible: controle energisation moteur plus fin.

## 7) Points de code tiers (information)

- Element: Certaines remarques "unused" dans bibliotheques vendorees
- Statut: Information seulement
- Preuves exemples:
  - libraries/TFT_eSPI/TFT_eSPI.h:205
  - libraries/lvgl/src/src/lv_themes/lv_theme_templ.c:348
- Explication: ces lignes appartiennent a des libs externes et ne representent pas forcement une dette de votre firmware applicatif.
- Interet possible: ignorer sauf si fork local volontaire.

## 8) Resume decisionnel

- Priorite haute (si objectif fiabilite production):
  - SettingsDefinitions callbacks runtime
  - WebSettings/WebServer erreurs et efficacite
  - GCode pourcentage % si workflow GCode en depend

- Priorite moyenne (si objectif qualite gravure/spindle):
  - Piecewise linear spindle + VFD modifiers
  - YL620/H2A reporting vitesse

- Priorite faible (maintenance/architecture):
  - Nettoyage TODO structurels moteurs/stepper/wifi constants

## 9) Notes de prudence
- Beaucoup de TODO sont des dettes techniques, pas des pannes immediates.
- Les activations de fonctions doivent etre accompagnees de tests materiels (spindle, homing, limits, web auth).
- Pour rester incremental: valider un lot fonctionnel a la fois (ex: Settings runtime, puis WebUI, puis Spindle).
