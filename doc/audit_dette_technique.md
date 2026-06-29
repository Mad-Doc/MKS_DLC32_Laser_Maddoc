# Audit des dettes techniques — MKS-DLC32 Firmware

Date: 2026-05-02
Périmètre: Grbl_Esp32/src (hors module draw infile)
Objectif: identifier les dettes techniques structurelles, c'est-à-dire les endroits où la conception
actuelle crée des risques réels, des comportements incorrects ou une fragilité de maintenance,
indépendamment de la présence ou non d'un commentaire TODO.

---

## Définition retenue

Une dette technique est un choix de conception (ou une omission de conception) qui fonctionne
dans un contexte précis mais qui, dans d'autres conditions légitimes d'utilisation, produit un
comportement incorrect, silencieux, ou difficile à diagnostiquer.
Elle se distingue d'un simple commentaire TODO par le fait qu'elle a une conséquence observable.

---

## DETTE 1 — Settings sans effet runtime (couplage absent entre couche config et sous-systèmes)

### Nature de la dette
Modifier un réglage via $22, $21, $30, $31, $23 depuis la console ou WebUI
persiste la valeur en NVS mais ne déclenche aucune réinitialisation du sous-système concerné.
Les callbacks associés (`checkSpindleChange`, etc.) valident la valeur mais n'appellent pas
les fonctions d'init correspondantes.

### Fichiers concernés
- `Grbl_Esp32/src/SettingsDefinitions.cpp` lignes 34, 36, 40, 391, 394, 396
- `Grbl_Esp32/src/SettingsDefinitions.cpp` lignes 391–402 (rpm_min, rpm_max, homing_enable,
  hard_limits — commentaires TODO explicites mais callbacks absents)

### Conséquences concrètes
| Setting modifié | Ce qui aurait dû se passer | Ce qui se passe réellement |
|---|---|---|
| $22 homing_enable | `limits_init()` rechargé | L'ISR limite reste dans l'état précédent jusqu'au reboot |
| $21 hard_limits | Vérification que homing_enable est actif | Hard limits activable sans homing → alarme imprévisible dès un triggering |
| $30/$31 rpm_max/min | `my_spindle->init()` rappelé | La spindle continue avec les anciennes valeurs de plage jusqu'au reboot |
| $23 homing_dir_mask | `st_generate_step_invert_masks()` | Direction d'homing incorrecte sans reboot |

### Niveau de risque
**Élevé** — silencieux. L'utilisateur croit avoir appliqué un réglage ; le firmware l'a enregistré
mais ne l'applique pas. Aucun message d'erreur n'est émis.

---

## DETTE 2 — hard_limits utilisable sans homing_enable (invariant absent)

### Nature de la dette
`hard_limits` ($21) et `homing_enable` ($22) sont deux settings indépendants sans relation
d'ordre forcée. Or hard_limits sans homing est conceptuellement incohérent : la machine n'a
pas de référentiel absolu, donc une alarme hard limit ne peut pas être résolue proprement.

### Fichier concerné
- `Grbl_Esp32/src/SettingsDefinitions.cpp` ligne 36 :
  `// TODO Settings - need to check for HOMING_ENABLE`
- Le callback de `hard_limits` est `NULL` (ligne 402 de facto).

### Conséquences concrètes
- Activer $21=1 avec $22=0 place la machine dans un état où le premier effleurement
  d'une fin de course génère une alarme HardLimit (ExecAlarm::HardLimit).
- La seule sortie est un reset + homing — mais homing est désactivé → cycle infini.
- Sur une machine débutante avec des fins de course bruyantes, cela bloque la machine
  définitivement jusqu'à correction manuelle par console série.

### Niveau de risque
**Élevé** — machine potentiellement inopérable sans intervention externe.

---

## DETTE 3 — Piecewise linear spindle : branche active mais toujours cassée

### Nature de la dette
Le mode `_piecewide_linear` dans `PWMSpindle` est une branche exécutable (non commentée,
non conditionnée par un `#ifdef`) mais dont le corps produit toujours `pwm_value = 0`
accompagné d'un log "Warning: Linear fit not implemented yet."
Si `_piecewide_linear` vaut `true` à l'exécution, toute commande M3/S génère une puissance
nulle — le laser/spindle est coupé silencieusement.

### Fichier concerné
- `Grbl_Esp32/src/Spindles/PWMSpindle.cpp` lignes 194–196 :
  ```cpp
  if (_piecewide_linear) {
      //pwm_value = piecewise_linear_fit(rpm); TODO
      pwm_value = 0;
      grbl_msg_sendf(CLIENT_ALL, MsgLevel::Info, "Warning: Linear fit not implemented yet.");
  ```

### Conséquences concrètes
- Si une configuration machine active ce mode (possible via Config.h), toute gravure
  produit zéro puissance sans alarme, sans arrêt, sans message d'erreur bloquant.
- Le travail semble se dérouler normalement (mouvements OK) mais rien n'est gravé.
- Le log "Warning" ne remonte que sur CLIENT_ALL (série + WebUI) et peut être noyé
  dans le flux de messages normaux.

### Niveau de risque
**Élevé** — échec silencieux de production.

---

## DETTE 4 — VFD : overrides de vitesse non appliqués

### Nature de la dette
`VFDSpindle::set_speed()` reçoit le RPM cible mais n'applique pas les overrides de vitesse
(`sys.spindle_speed_ovr`) ni aucune linéarisation avant d'envoyer la commande Modbus.
Le commentaire le documente explicitement.

### Fichier concerné
- `Grbl_Esp32/src/Spindles/VFDSpindle.cpp` ligne 563 :
  `// TODO add the speed modifiers override, linearization, etc.`

### Conséquences concrètes
- Les commandes de type `M3 S1000` avec un override actif (ex. 80%) envoient 1000 RPM
  au VFD au lieu de 800 RPM.
- Les boucles de compensation de vitesse du planner (spindle_speed_ovr) n'ont aucun effet
  sur les axes VFD : le comportement diffère de la PWM spindle de façon non documentée.
- En mode laser piloté par VFD, cela provoque une puissance incorrecte et une passe
  sur-éclairée ou sous-éclairée selon la direction de l'override.

### Niveau de risque
**Moyen-élevé** — incorrect mais non bloquant. Préjudiciable sur toute machine VFD.

---

## DETTE 5 — Authentification WebUI : niveaux partiellement appliqués

### Nature de la dette
Le système d'authentification distingue GUEST / USER / ADMIN mais la politique
`ADMIN_ONLY` pour certaines commandes est marquée TODO et non implémentée.
Résultat : certaines commandes protégées en intention sont accessibles au niveau USER.

### Fichier concerné
- `Grbl_Esp32/src/WebUI/Authentication.cpp` ligne 5 :
  `// TODO Settings - need ADMIN_ONLY and if it is called without a parameter it sets the default`

### Conséquences concrètes
- Un utilisateur authentifié en mode USER peut exécuter des commandes réservées ADMIN
  (modification de settings critiques, upload firmware, etc.) si la vérification
  `ADMIN_ONLY` n'est pas présente sur chaque handler.
- Sur un réseau local ouvert (machine atelier), cela signifie que tout utilisateur
  connecté au WiFi peut modifier la configuration machine.

### Niveau de risque
**Moyen** — dépend de la topologie réseau. Sur WiFi ouvert : élevé.

---

## DETTE 6 — WebServer : parsing de commandes en O(n²)

### Nature de la dette
La fonction `get_Splited_Value()` appelée dans la boucle de dispatch des G-codes web
est de complexité O(n²) : pour chaque index `sindex`, elle re-parcourt la chaîne entière
depuis le début. Sur un payload de N lignes de G-code, cela produit N²/2 scans de chaîne.

### Fichier concerné
- `Grbl_Esp32/src/WebUI/WebServer.cpp` lignes 497–501 :
  ```cpp
  // TODO Settings - this is very inefficient.  get_Splited_Value() is O(n^2)
  for (uint8_t sindex = 0; (scmd = get_Splited_Value(cmd, '\n', sindex)) != ""; sindex++) {
  ```

### Conséquences concrètes
- Un envoi de 200 lignes de G-code via WebUI provoque ~20 000 scans de la chaîne entière
  en RAM sur le cœur Web (Core 0), bloquant la tâche HTTP pendant potentiellement plusieurs
  dizaines de millisecondes.
- Sur ESP32 avec watchdog HTTP, un payload suffisamment grand peut déclencher un timeout
  et une réponse HTTP 500 alors que le contenu était valide.
- En pratique : limit opérationnelle non documentée sur la taille des payloads WebUI.

### Niveau de risque
**Moyen** — dégradation progressive, non bloquant sur petits fichiers.

---

## DETTE 7 — Probe cycle : non-auto cycle start ignoré

### Nature de la dette
`mc_probe_cycle()` ne vérifie pas si le firmware est en mode "non-auto cycle start"
avant de démarrer le mouvement de probing. Ce mode permet normalement à l'opérateur
de valider manuellement chaque démarrage de cycle.

### Fichier concerné
- `Grbl_Esp32/src/MotionControl.cpp` ligne 397 :
  `// TODO: Need to update this cycle so it obeys a non-auto cycle start.`

### Conséquences concrètes
- En mode manuel, un G38.x (probe) démarre immédiatement sans attente de validation
  opérateur, contrairement aux mouvements normaux G0/G1.
- Comportement incohérent : l'opérateur pense tenir un contrôle manuel mais la sonde
  part toute seule.
- Risque de collision si l'opérateur positionne un outil entre la sonde et la surface
  pendant la pause manuelle d'un autre cycle.

### Niveau de risque
**Moyen** — comportement surprenant mais rarement rencontré sur machines laser légères.

---

## DETTE 8 — `mks_draw_freaure()` appelée sans définition (draw infile)

### Nature de la dette
Dans `MKS_draw_inFile.cpp`, la fonction `mks_draw_freaure()` est appelée mais n'est
définie nulle part dans le projet. Le build ne lève pas d'erreur car la définition
est probablement attendue dans un fichier Custom ou une autre unité de compilation
qui ne contient pas cette fonction.

### Conséquences concrètes
- Selon le linker, soit l'appel résout un symbole faible aléatoire, soit il est
  éliminé par le linker ESP32 (dead code elimination), soit il provoque un crash
  au runtime si le pointeur de fonction est résolu à une adresse invalide.
- Non reproductible facilement : le comportement dépend de la disposition finale
  de la section .text par le linker xtensa-esp32.

### Niveau de risque
**Moyen-élevé** — crash potentiel non déterministe à l'exécution.

---

## Synthèse par priorité d'action

| Priorité | Dette | Raison |
|---|---|---|
| 1 — Critique | DETTE 1 : Settings sans effet runtime | Silencieux, trompe l'utilisateur sur l'état réel |
| 1 — Critique | DETTE 2 : hard_limits sans homing | Peut rendre la machine inopérable |
| 1 — Critique | DETTE 3 : Piecewise linear = pwm 0 | Produit zéro sans alarme |
| 2 — Élevé | DETTE 8 : mks_draw_freaure() indéfinie | Crash non déterministe possible |
| 2 — Élevé | DETTE 4 : VFD overrides ignorés | Puissance incorrecte sur toute machine VFD |
| 3 — Moyen | DETTE 5 : Auth ADMIN_ONLY absente | Sécurité insuffisante sur réseau ouvert |
| 3 — Moyen | DETTE 6 : O(n²) WebServer parsing | Latence et timeout sur gros payloads |
| 4 — Faible | DETTE 7 : Probe sans non-auto check | Comportement surprenant, rarement bloquant |

---

## Notes de prudence

- Les dettes 1, 2, 3 sont les seules qui peuvent produire des erreurs silencieuses
  en production normale (pas de message bloquant, comportement apparemment correct).
- Corriger la dette 1 (callbacks runtime) demande d'appeler les bonnes fonctions
  d'init dans chaque callback : risque de double-init à gérer.
- La dette 2 se corrige par une validation croisée dans le callback de `hard_limits`
  qui refuse l'activation si `homing_enable->get()` est false.
- La dette 3 se corrige soit en désactivant la branche (`if (false)` ou suppression),
  soit en implémentant réellement `piecewise_linear_fit()`.
